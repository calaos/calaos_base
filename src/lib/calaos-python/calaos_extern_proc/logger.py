import logging
import os
import sys
import colorama
from logging import Logger, LogRecord
from typing import Dict

colorama.init(strip=False)

def _stdout_is_tty() -> bool:
    return sys.stdout.isatty() if hasattr(sys.stdout, 'isatty') else False

class CalaosFormatter(logging.Formatter):
    COLOR_MAP = {
        'CRITICAL': colorama.Fore.MAGENTA,
        'ERROR': colorama.Fore.RED,
        'WARNING': colorama.Fore.YELLOW,
        'INFO': colorama.Fore.GREEN,
        'DEBUG': colorama.Fore.BLUE,
    }

    # Same contract as Logger.cpp: journald reads <n> at the head of each line
    JOURNAL_PRIORITY = {
        'CRITICAL': 2,
        'ERROR': 3,
        'WARNING': 4,
        'INFO': 6,
        'DEBUG': 7,
    }

    def __init__(self):
        super().__init__()
        self.journal = os.environ.get('CALAOS_JOURNAL_PRIORITY') == '1' and not _stdout_is_tty()
        self.force_color = os.environ.get('CALAOS_FORCE_COLOR') == '1'
        self.is_terminal = not self.journal and (self.force_color or _stdout_is_tty())

    def _get_color(self, level_name: str) -> str:
        if not self.is_terminal:
            return ''
        return self.COLOR_MAP.get(level_name, '')

    def _format_domain(self, record: LogRecord) -> str:
        parts = record.name.split('.')
        return parts[-1] if len(parts) > 1 else 'default'

    def format(self, record: LogRecord) -> str:
        color = self._get_color(record.levelname)
        reset = colorama.Style.RESET_ALL if color else ''
        domain = self._format_domain(record)
        filename = os.path.basename(record.pathname)
        cyan = colorama.Fore.CYAN if self.is_terminal else ''
        bright = colorama.Style.BRIGHT if self.is_terminal else ''

        level_marker = {
            'DEBUG': '[DBG]',
            'INFO': '[INF]',
            'WARNING': '[WRN]',
            'ERROR': '[ERR]',
            'CRITICAL': '[CRI]'
        }.get(record.levelname, '[???]')

        text = (
            f"{color}{level_marker}"
            f"{cyan} {domain}{reset} "
            f"{bright}({filename}:{record.lineno}){reset} "
            f"{record.getMessage()}{reset}"
        )

        if not self.journal:
            return text

        prefix = f"<{self.JOURNAL_PRIORITY.get(record.levelname, 6)}>"
        return prefix + text.replace('\n', '\n' + prefix)

# Calaos debug levels (0-4, higher = more verbose) mapped to Python logging
# levels (10-50, higher = more severe). Level N shows messages of the mapped
# severity and above.
_CALAOS_TO_PYTHON_LEVEL = {
    0: logging.CRITICAL,
    1: logging.ERROR,
    2: logging.WARNING,
    3: logging.INFO,
    4: logging.DEBUG,
}

def _calaos_to_python_level(calaos_level: int) -> int:
    return _CALAOS_TO_PYTHON_LEVEL.get(max(0, min(4, calaos_level)), logging.DEBUG)

class CalaosLogger(Logger):
    # Minimum Python logging level per domain (from CALAOS_LOG_DOMAINS)
    DOMAIN_LEVELS: Dict[str, int] = {}
    DEFAULT_LEVEL = logging.DEBUG

    def __init__(self, name: str):
        super().__init__(name, logging.DEBUG)

    @classmethod
    def domain_min_level(cls, domain: str) -> int:
        return cls.DOMAIN_LEVELS.get(domain, cls.DEFAULT_LEVEL)

    def should_log(self, level: int) -> bool:
        domain = self.name.split('.')[-1] if '.' in self.name else 'default'
        return level >= self.domain_min_level(domain)

class _DomainLevelFilter(logging.Filter):
    """Enforces per-domain levels (CALAOS_LOG_DOMAINS) on the handler."""

    def filter(self, record: LogRecord) -> bool:
        parts = record.name.split('.')
        domain = parts[-1] if len(parts) > 1 else 'default'
        return record.levelno >= CalaosLogger.domain_min_level(domain)

def get_debug_level() -> int:
    try:
        # Try environment variable
        env_level = os.environ.get('CALAOS_LOG_LEVEL')
        if env_level and env_level.isdigit():
            return int(env_level)

        # Default fallback
        return 4
    except (ValueError, TypeError):
        return 4

def configure_logger():
    debug_level = get_debug_level()
    domains_config = os.environ.get('CALAOS_LOG_DOMAINS', '')

    # Calaos levels (0-4) are converted to Python logging levels here so the
    # filter compares like units (was: raw 0-4 compared to 10-50).
    CalaosLogger.DEFAULT_LEVEL = _calaos_to_python_level(debug_level)
    for part in domains_config.split(','):
        if ':' in part:
            try:
                domain, level = part.split(':', 1)
                if level.strip().isdigit():
                    CalaosLogger.DOMAIN_LEVELS[domain.strip()] = \
                        _calaos_to_python_level(int(level))
            except (ValueError, TypeError):
                continue

    root_logger = logging.getLogger('CALAOS')

    # Clear any existing handlers
    root_logger.handlers.clear()

    # Add our console handler with per-domain filtering
    handler = logging.StreamHandler(stream=sys.stdout)
    handler.setFormatter(CalaosFormatter())
    handler.addFilter(_DomainLevelFilter())

    logging.setLoggerClass(CalaosLogger)
    root_logger.addHandler(handler)
    root_logger.setLevel(logging.DEBUG)

def get_logger(domain: str) -> CalaosLogger:
    return logging.getLogger(f'CALAOS.{domain}')

# Helpers
cDebugDom = lambda domain: get_logger(domain).debug
cInfoDom = lambda domain: get_logger(domain).info
cWarningDom = lambda domain: get_logger(domain).warning
cErrorDom = lambda domain: get_logger(domain).error
cCriticalDom = lambda domain: get_logger(domain).critical
