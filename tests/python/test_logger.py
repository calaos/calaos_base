# T1.8 — logger.py: CALAOS_LOG_DOMAINS domain filtering must actually be
# wired (was dead code: root logger forced to DEBUG, should_log never called)
# and Calaos levels (0-4) must be mapped to Python logging levels (50-10).
import io
import logging

import pytest

pytest.importorskip("colorama")

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
