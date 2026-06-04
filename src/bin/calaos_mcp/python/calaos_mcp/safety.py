"""Anti-prompt-injection sanitisation (S3).

Strips control characters and ANSI escape sequences from strings returned
to the LLM. Wraps untrusted device names/values in a typed structure so
the model treats them as data rather than instructions.
"""

from __future__ import annotations

import re

_ANSI_RE = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")
_CTRL_RE = re.compile(r"[\x00-\x08\x0b\x0c\x0e-\x1f\x7f]")

_NAME_MAX = 256
_DESC_MAX = 1024


def _strip(text: str, max_len: int) -> str:
    text = _ANSI_RE.sub("", text)
    text = _CTRL_RE.sub("", text)
    return text[:max_len]


def safe_name(text: str) -> str:
    return _strip(text, _NAME_MAX)


def safe_value(text: str) -> str:
    return _strip(text, _DESC_MAX)


def wrap_untrusted(value: str) -> dict:
    """Wrap an untrusted string so the LLM sees it as data, not instructions."""
    return {"untrusted_text": safe_value(str(value))}
