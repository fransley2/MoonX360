"""Read-only XBDM inventory. Writes a new evidence directory; no console changes."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path

from screenshot import convert
from xbdm import Client, XbdmError


def request(host, command):
    with Client(host) as client:
        reply = client.send(command)
        return {"code": reply.code, "message": reply.message, "lines": reply.lines}


def record_error(error):
    value = {"error": str(error)}
    if isinstance(error, XbdmError):
        value.update({"reason": error.reason, "expected_bytes": error.expected,
                      "received_bytes": error.received})
    return value


def diagnose(host: str, output: Path, capture: bool = False):
    output.mkdir(parents=True, exist_ok=False)
    result = {"host": host, "collected_utc": datetime.now(timezone.utc).isoformat(),
              "commands": {}}
    for command in ("getexecstate", "modules",
                    'dirlist name="HDD:\\Apps\\MoonX360"',
                    'dirlist name="Hdd1:\\Apps\\MoonX360"'):
        try:
            result["commands"][command] = request(host, command)
        except (OSError, ValueError) as exc:
            result["commands"][command] = record_error(exc)
    if capture:
        partial = output / "screenshot.partial"
        try:
            with Client(host, timeout=30) as client, partial.open("wb") as destination:
                transfer = client.screenshot(destination)
            raw = partial.read_bytes()
            if len(raw) != transfer["expected_bytes"]:
                raise XbdmError("screenshot length mismatch", expected=transfer["expected_bytes"],
                                received=len(raw))
            result["screenshot"] = transfer
            try:
                (output / "screenshot.png").write_bytes(convert(raw, transfer["metadata"]))
                partial.rename(output / "screenshot.raw")
            except ValueError as exc:
                result["screenshot"]["conversion_error"] = str(exc)
        except (OSError, ValueError) as exc:
            result["screenshot"] = record_error(exc)
            result["screenshot"]["partial_file_bytes"] = partial.stat().st_size if partial.exists() else 0
        try:
            result["exec_state_after_screenshot"] = request(host, "getexecstate")
        except (OSError, ValueError) as exc:
            result["exec_state_after_screenshot"] = record_error(exc)
    (output / "diagnosis.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("host")
    parser.add_argument("output", type=Path)
    parser.add_argument("--screenshot", action="store_true")
    args = parser.parse_args()
    print(json.dumps(diagnose(args.host, args.output, args.screenshot), indent=2))
