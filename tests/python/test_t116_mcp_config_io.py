# T1.16 — regression tests for config.py rate-limit normalisation and
# tools/io.py numeric value validation.
#
# Run with: python3 -m unittest discover -s tests/python -p 'test_t116_*.py'

import os
import sys
import tempfile
import unittest

_PKG_DIR = os.path.join(
    os.path.dirname(os.path.abspath(__file__)),
    "..", "..", "src", "bin", "calaos_mcp", "python",
)
sys.path.insert(0, os.path.abspath(_PKG_DIR))

from calaos_mcp.config import get_config, RATE_LIMIT_DISABLED  # noqa: E402
from calaos_mcp.tools.io import _validate_value  # noqa: E402

_XML_TMPL = """<?xml version="1.0" encoding="UTF-8"?>
<calaos:config xmlns:calaos="http://www.calaos.fr">
    <calaos:option name="mcp_token" value="tok" />
    <calaos:option name="mcp_service_token" value="svc" />
    {extra}
</calaos:config>
"""


class ConfigRateLimitTest(unittest.TestCase):
    def _config_with(self, extra_options: str):
        get_config.cache_clear()
        tmp = tempfile.mkdtemp(prefix="t116_cfg_")
        with open(os.path.join(tmp, "local_config.xml"), "w") as f:
            f.write(_XML_TMPL.format(extra=extra_options))
        old_env = dict(os.environ)
        os.environ["CALAOS_CONFIG_PATH"] = tmp
        os.environ["CALAOS_MCP_SOCKET"] = "/tmp/t116.sock"
        try:
            return get_config()
        finally:
            os.environ.clear()
            os.environ.update(old_env)
            get_config.cache_clear()

    def test_rate_limit_zero_disables_instead_of_blocking(self):
        cfg = self._config_with('<calaos:option name="mcp_rate_limit" value="0" />')
        self.assertEqual(cfg.rate_limit, RATE_LIMIT_DISABLED)
        # auth.py blocks when len(window) >= rate_limit — must stay permissive
        self.assertGreater(cfg.rate_limit, 0)

    def test_rate_limit_negative_disables(self):
        cfg = self._config_with('<calaos:option name="mcp_rate_limit" value="-5" />')
        self.assertEqual(cfg.rate_limit, RATE_LIMIT_DISABLED)

    def test_rate_limit_normal_value_kept(self):
        cfg = self._config_with('<calaos:option name="mcp_rate_limit" value="42" />')
        self.assertEqual(cfg.rate_limit, 42)

    def test_rate_limit_default(self):
        cfg = self._config_with("")
        self.assertEqual(cfg.rate_limit, 300)

    def test_ban_failures_zero_still_allowed(self):
        # 0 legitimately disables banning — must NOT be remapped.
        cfg = self._config_with('<calaos:option name="mcp_ban_failures" value="0" />')
        self.assertEqual(cfg.ban_failures, 0)


class IoNumericValidationTest(unittest.TestCase):
    def _io(self, var_type):
        return {"id": "io_test", "var_type": var_type}

    def test_rejects_non_finite_floats(self):
        for bad in ("inf", "-inf", "nan", "NaN", "Infinity", "1e999", "-1e999"):
            for vt in ("float", "int", "double"):
                with self.assertRaises(ValueError, msg=f"{bad!r} for {vt}"):
                    _validate_value(bad, self._io(vt))

    def test_rejects_non_numeric(self):
        with self.assertRaises(ValueError):
            _validate_value("abc", self._io("float"))

    def test_accepts_finite_numbers(self):
        self.assertEqual(_validate_value("75", self._io("float")), "75")
        self.assertEqual(_validate_value("-3.5", self._io("double")), "-3.5")
        self.assertEqual(_validate_value("42", self._io("int")), "42")

    def test_int_requires_integral_value(self):
        with self.assertRaises(ValueError):
            _validate_value("1.5", self._io("int"))
        # Integral-valued floats are accepted for int IOs
        self.assertEqual(_validate_value("3.0", self._io("int")), "3.0")

    def test_bool_unaffected(self):
        self.assertEqual(_validate_value("on", self._io("bool")), "true")
        with self.assertRaises(ValueError):
            _validate_value("maybe", self._io("bool"))


if __name__ == "__main__":
    unittest.main()
