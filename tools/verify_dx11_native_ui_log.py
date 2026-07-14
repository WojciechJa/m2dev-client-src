#!/usr/bin/env python3
"""Verify that a client log proves the strict native DX11 login UI path."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


REQUIRED_PATTERNS = {
    "strict native configuration": re.compile(
        r"DX11_CONFIG_STATE.*strict_native_only=1.*disable_dx9_compat_device=1"
    ),
    "no legacy DX9 device": re.compile(
        r"DX11_RUNTIME_COMPAT legacy_dx9_device=0 reason=dx11_strict_only"
    ),
    "native UI state": re.compile(r"DX11_UI_STATE_GUARD interface_render_state_dx11_only"),
    "first UI submission": re.compile(r"DX11_STARTUP_TIMELINE event=first_ui_submit"),
    "native text pipeline": re.compile(
        r"DX11_PIPELINE_STATE_PARITY pass=ui_text_strict path=dx11_native"
    ),
    "first successful present": re.compile(
        r"DX11_STARTUP_TIMELINE event=first_present_success"
    ),
}

FORBIDDEN_MARKERS = (
    "DX11_STRICT_BEGIN_FRAME_FAIL",
    "DX11 UI native test draw call failed",
    "DX11_NATIVE_PRESENT result=fail",
)

TEXT_PATTERN = re.compile(
    r"DX11_TEXT_RENDER_DX11 glyphs_submitted=(\d+) glyphs_emitted=(\d+).*srv_failures_persistent=(\d+)"
)
WIDGET_PATTERN = re.compile(
    r"DX11_UI_WIDGET_HEARTBEAT image=(\d+) expanded=(\d+) mark=(\d+) fail=(\d+)"
)


def validate_log(text: str) -> list[str]:
    errors: list[str] = []
    for label, pattern in REQUIRED_PATTERNS.items():
        if not pattern.search(text):
            errors.append(f"missing: {label}")

    if not any(
        int(submitted) > 0 and int(emitted) > 0 and int(persistent_failures) == 0
        for submitted, emitted, persistent_failures in TEXT_PATTERN.findall(text)
    ):
        errors.append("missing: successful native glyph submission")

    if not any(
        int(image) + int(expanded) + int(mark) > 0 and int(failures) == 0
        for image, expanded, mark, failures in WIDGET_PATTERN.findall(text)
    ):
        errors.append("missing: successful native widget heartbeat")

    for marker in FORBIDDEN_MARKERS:
        if marker in text:
            errors.append(f"forbidden marker: {marker}")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path, help="Path to syserr.txt")
    args = parser.parse_args()
    text = args.log.read_text(encoding="utf-8", errors="replace")
    errors = validate_log(text)
    if errors:
        print("DX11 native UI log: FAIL")
        for error in errors:
            print(f"- {error}")
        return 1
    print("DX11 native UI log: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
