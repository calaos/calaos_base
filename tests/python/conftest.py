# T1.8 — Python sidecar test setup.
# Makes the two in-tree Python packages importable without installation:
#   calaos_mcp          (src/bin/calaos_mcp/python)
#   calaos_extern_proc  (src/lib/calaos-python)
import os
import sys

_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(_ROOT, "src", "bin", "calaos_mcp", "python"))
sys.path.insert(0, os.path.join(_ROOT, "src", "lib", "calaos-python"))
