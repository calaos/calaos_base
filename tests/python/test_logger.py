# T1.8 — logger.py: CALAOS_LOG_DOMAINS domain filtering must actually be
# wired (was dead code: root logger forced to DEBUG, should_log never called)
# and Calaos levels (0-4) must be mapped to Python logging levels (50-10).
import io
import logging
import re

import pytest

colorama = pytest.importorskip("colorama")

from calaos_extern_proc import logger as clog


@pytest.fixture()
def capture(monkeypatch):
    """Configure the logger and capture its output in a StringIO."""

    def _configure(level="4", domains=""):
        monkeypatch.setenv("CALAOS_LOG_LEVEL", level)
        if domains:
            monkeypatch.setenv("CALAOS_LOG_DOMAINS", domains)
        else:
            monkeypatch.delenv("CALAOS_LOG_DOMAINS", raising=False)
        clog.CalaosLogger.DOMAIN_LEVELS.clear()
        clog.configure_logger()
        stream = io.StringIO()
        root = logging.getLogger("CALAOS")
        for h in root.handlers:
            h.setStream(stream)
        return stream

    yield _configure
    # cleanup: drop handlers so later tests reconfigure cleanly
    logging.getLogger("CALAOS").handlers.clear()
    clog.CalaosLogger.DOMAIN_LEVELS.clear()


def test_default_level_4_logs_debug(capture):
    stream = capture(level="4")
    clog.cDebugDom("mydom")("debug-visible")
    assert "debug-visible" in stream.getvalue()


def test_domain_filtering_suppresses_verbose_domain(capture):
    # domain "noisy" capped at Calaos level 2 (warnings and up)
    stream = capture(level="4", domains="noisy:2")
    clog.cDebugDom("noisy")("noisy-debug")
    clog.cInfoDom("noisy")("noisy-info")
    out = stream.getvalue()
    assert "noisy-debug" not in out
    assert "noisy-info" not in out
    clog.cWarningDom("noisy")("noisy-warn")
    clog.cErrorDom("noisy")("noisy-err")
    out = stream.getvalue()
    assert "noisy-warn" in out
    assert "noisy-err" in out


def test_domain_filtering_leaves_other_domains_alone(capture):
    stream = capture(level="4", domains="noisy:1")
    clog.cDebugDom("other")("other-debug")
    assert "other-debug" in stream.getvalue()


def test_global_level_applies_python_units(capture):
    # Calaos level 2 == warnings and up, for every domain
    stream = capture(level="2")
    clog.cInfoDom("anydom")("info-hidden")
    assert "info-hidden" not in stream.getvalue()
    clog.cErrorDom("anydom")("err-visible")
    assert "err-visible" in stream.getvalue()


class _TtyStream(io.StringIO):
    def isatty(self):
        return True


@pytest.fixture()
def journal(monkeypatch, capture):
    """capture() with the logging variables of a sidecar set explicitly."""

    def _configure(journal_var, force_color=None):
        monkeypatch.delenv("CALAOS_FORCE_COLOR", raising=False)
        monkeypatch.delenv("CALAOS_JOURNAL_PRIORITY", raising=False)
        if journal_var is not None:
            monkeypatch.setenv("CALAOS_JOURNAL_PRIORITY", journal_var)
        if force_color is not None:
            monkeypatch.setenv("CALAOS_FORCE_COLOR", force_color)
        return capture(level="4")

    return _configure


def _emit_every_level():
    clog.cCriticalDom("dom")("critical")
    clog.cErrorDom("dom")("error")
    clog.cWarningDom("dom")("warning")
    clog.cInfoDom("dom")("info")
    clog.cDebugDom("dom")("debug")


def _strip_positions(text):
    return re.sub(r"\(test_logger\.py:\d+\)", "(f)", text)


def test_journal_mode_prefixes_each_level_with_its_priority(journal):
    stream = journal("1")
    _emit_every_level()
    assert _strip_positions(stream.getvalue()) == (
        "<2>[CRI] dom (f) critical\n"
        "<3>[ERR] dom (f) error\n"
        "<4>[WRN] dom (f) warning\n"
        "<6>[INF] dom (f) info\n"
        "<7>[DBG] dom (f) debug\n"
    )


def test_journal_mode_prefixes_every_line_of_a_message(journal):
    stream = journal("1")
    clog.cErrorDom("dom")("first\nsecond")
    assert _strip_positions(stream.getvalue()) == "<3>[ERR] dom (f) first\n<3>second\n"


@pytest.mark.parametrize("value", [None, "0", "", "yes"])
def test_no_prefix_and_no_ansi_without_the_exact_value(journal, value):
    stream = journal(value)
    _emit_every_level()
    assert _strip_positions(stream.getvalue()) == (
        "[CRI] dom (f) critical\n"
        "[ERR] dom (f) error\n"
        "[WRN] dom (f) warning\n"
        "[INF] dom (f) info\n"
        "[DBG] dom (f) debug\n"
    )


def test_journal_mode_wins_over_forced_color(journal):
    stream = journal("1", force_color="1")
    clog.cErrorDom("dom")("boom")
    assert _strip_positions(stream.getvalue()) == "<3>[ERR] dom (f) boom\n"


def test_forced_color_without_journal_mode_keeps_the_colors(journal):
    stream = journal(None, force_color="1")
    clog.cErrorDom("dom")("boom")
    out = stream.getvalue()
    assert out.startswith(colorama.Fore.RED + "[ERR]" + colorama.Fore.CYAN + " dom")
    assert "<3>" not in out


def test_a_terminal_shows_colors_and_no_prefix(monkeypatch, journal):
    monkeypatch.setattr(clog.sys, "stdout", _TtyStream())
    stream = journal("1")
    clog.cErrorDom("dom")("boom")
    out = stream.getvalue()
    assert out.startswith(colorama.Fore.RED + "[ERR]" + colorama.Fore.CYAN + " dom")
    assert "<3>" not in out
