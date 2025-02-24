from .extern_proc import ExternProcClient
from .message import ExternProcMessage, MessageType, MessageState

__version__ = '1.0.0'

from .logger import (
    configure_logger,
    get_logger,
    cDebugDom,
    cInfoDom,
    cWarningDom,
    cErrorDom,
    cCriticalDom
)

# Configure logger by default
configure_logger()

__all__ = [
    'configure_logger',
    'get_logger',
    'cDebugDom',
    'cInfoDom',
    'cWarningDom',
    'cErrorDom',
    'cCriticalDom'
]