from __future__ import annotations

import importlib.util
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = REPO_ROOT / "tools" / "verify_dx11_native_ui_log.py"
SPEC = importlib.util.spec_from_file_location("verify_dx11_native_ui_log", MODULE_PATH)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class DX11NativeUILogTest(unittest.TestCase):
    def test_verified_login_fixture_passes(self) -> None:
        fixture = (REPO_ROOT / "tests" / "fixtures" / "dx11_native_ui_login.txt").read_text(
            encoding="utf-8"
        )
        self.assertEqual([], MODULE.validate_log(fixture))

    def test_legacy_device_and_failed_widget_heartbeat_are_rejected(self) -> None:
        fixture = (REPO_ROOT / "tests" / "fixtures" / "dx11_native_ui_login.txt").read_text(
            encoding="utf-8"
        )
        broken = fixture.replace("legacy_dx9_device=0", "legacy_dx9_device=1").replace(
            "fail=0", "fail=3"
        )
        errors = MODULE.validate_log(broken)
        self.assertIn("missing: no legacy DX9 device", errors)
        self.assertIn("missing: successful native widget heartbeat", errors)

    def test_critical_runtime_failure_is_rejected(self) -> None:
        fixture = (REPO_ROOT / "tests" / "fixtures" / "dx11_native_ui_login.txt").read_text(
            encoding="utf-8"
        )
        errors = MODULE.validate_log(fixture + "\nDX11_STRICT_BEGIN_FRAME_FAIL\n")
        self.assertIn("forbidden marker: DX11_STRICT_BEGIN_FRAME_FAIL", errors)


if __name__ == "__main__":
    unittest.main()
