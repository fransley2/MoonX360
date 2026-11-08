"""Read-only backup of explicitly named console baseline files over XBDM.

Candidates are not assumed to be the active DashLaunch configuration. Never
prints file content or writes to the console.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path

from xbdm import Client, XbdmError


ROOTS = ("HDD:\\", "Hdd:\\", "Hdd1:\\", "Usb0:\\", "Usb1:\\")
XEX = "HDD:\\Apps\\MoonX360\\default.xex"


def query(host, command):
    with Client(host) as client:
        reply = client.send(command)
    return {"code": reply.code, "message": reply.message, "lines": reply.lines}


def fetch(host, remote, path):
    with Client(host) as client, path.open("wb") as destination:
        result = client.getfile(remote, destination)
    if path.stat().st_size != result["expected_bytes"]:
        raise OSError("local length mismatch")
    return result


def error_data(error):
    result = {"error": str(error)}
    if isinstance(error, XbdmError):
        result.update({"reason": error.reason, "expected_bytes": error.expected,
                       "received_bytes": error.received})
    return result


def collect(host, output):
    output.mkdir(parents=True, exist_ok=False)
    result = {"host": host, "collected_utc": datetime.now(timezone.utc).isoformat(),
              "before": {}, "roots": {}, "files": {}}
    for cmd in ("getexecstate", "modules"):
        try:
            result["before"][cmd] = query(host, cmd)
        except (OSError, ValueError) as exc:
            result["before"][cmd] = error_data(exc)
    for index, root in enumerate(ROOTS):
        try:
            listing = query(host, 'dirlist name="' + root + '"')
            result["roots"][root] = listing
        except (OSError, ValueError) as exc:
            result["roots"][root] = error_data(exc)
            continue
        if any(line.lower().startswith('name="launch.ini" ') for line in listing["lines"]):
            remote = root + "launch.ini"
            local = output / (f"{index:02d}-" + root.split(":", 1)[0] + "-launch.ini")
            try:
                result["files"][remote] = fetch(host, remote, local)
                result["files"][remote]["local_name"] = local.name
            except (OSError, ValueError) as exc:
                result["files"][remote] = error_data(exc)
                if local.exists() and local.stat().st_size == 0:
                    local.unlink()
    local = output / "MoonX360-installed-default.xex"
    try:
        result["files"][XEX] = fetch(host, XEX, local)
        result["files"][XEX]["local_name"] = local.name
    except (OSError, ValueError) as exc:
        result["files"][XEX] = error_data(exc)
        if local.exists() and local.stat().st_size == 0:
            local.unlink()
    for cmd in ("getexecstate", "modules"):
        try:
            result.setdefault("after", {})[cmd] = query(host, cmd)
        except (OSError, ValueError) as exc:
            result.setdefault("after", {})[cmd] = error_data(exc)
    (output / "baseline.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("host")
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    report = collect(args.host, args.output)
    print(json.dumps({"roots": {k: {"code": v.get("code"), "error": v.get("error")}
                                for k, v in report["roots"].items()},
                      "files": report["files"]}, indent=2))
