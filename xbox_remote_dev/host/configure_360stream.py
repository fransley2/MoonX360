"""Prepare or apply the explicitly requested USB launch.ini edit, with recovery.

Never launches a title, loads a module, or reboots the console. --apply is
only for a user-requested startup configuration experiment; the external
capture DLL's DashLaunch compatibility remains unverified.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import io
import json
import os
from pathlib import Path
import re

from xbdm import Client

ACTIVE = r"Usb0:\launch.ini"
PLUGIN = rb"Usb:\Xbox360Stream.xex"
XEX_SHA = "f9ca633356554a0d5f8c2fd5a027ca075d5ac6db91dae6fc5b3c4527c59374fb"


def digest(data):
    return hashlib.sha256(data).hexdigest()


def candidate(data):
    if len(data) > 65536:
        raise ValueError("launch.ini exceeds size limit")
    sections = list(re.finditer(rb"(?im)^(?:\xef\xbb\xbf)?[ \t]*\[Plugins\][ \t]*\r?$", data))
    if len(sections) != 1:
        raise ValueError("expected exactly one Plugins section")
    start = sections[0].end()
    next_section = re.search(rb"(?m)^\[", data[start:])
    end = start + next_section.start() if next_section else len(data)
    entries = list(re.finditer(rb"(?im)^([ \t]*plugin3[ \t]*=[ \t]*)([^\r\n]*)", data[start:end]))
    if len(entries) != 1 or entries[0][2].strip():
        raise ValueError("plugin3 must exist once and be empty; refusing replacement")
    match = entries[0]
    offset = start + match.start(2)
    return data[:offset] + PLUGIN + data[start + match.end(2):]


class Remote:
    def __init__(self, host):
        self.host = host

    def read(self, path):
        result = io.BytesIO()
        with Client(self.host, timeout=8) as client:
            client.getfile(path, result)
        return result.getvalue()

    def exists(self, path):
        directory, name = path.rsplit("\\", 1)
        with Client(self.host, timeout=5) as client:
            lines = client.send(f'dirlist name="{directory}\\"').lines
        return any(re.search(r'^name="' + re.escape(name) + r'"(?: |$)', line, re.I) for line in lines)

    def rename(self, old, new):
        with Client(self.host, timeout=8) as client:
            reply = client.send(f'rename name="{old}" newname="{new}"')
            if reply.code != 200:
                raise OSError(f"rename status {reply.code}")

    def upload_new(self, path, data):
        if self.exists(path):
            raise ValueError("refusing to overwrite staged file")
        with Client(self.host, timeout=8) as client:
            reply = client.send(f'sendfile name="{path}" length=0x{len(data):x}')
            if reply.code != 204:
                raise OSError(f"sendfile status {reply.code}")
            client.sock.sendall(data)
            if client.status().code != 200:
                raise OSError("sendfile completion was not 200")


def apply(remote, before, after, backup, staged, failed):
    if any(remote.exists(p) for p in (backup, staged, failed)):
        raise ValueError("recovery filename already exists")
    if remote.read(ACTIVE) != before:
        raise ValueError("launch.ini changed since preparation")
    remote.upload_new(staged, after)
    if remote.read(staged) != after:
        raise OSError("staged INI readback mismatch; original is untouched")
    if remote.read(ACTIVE) != before:
        raise ValueError("launch.ini changed while staging; refusing activation")
    try:
        remote.rename(ACTIVE, backup)
        remote.rename(staged, ACTIVE)
        if remote.read(ACTIVE) != after or remote.read(backup) != before:
            raise OSError("active or backup readback mismatch")
    except Exception as error:
        if remote.exists(backup):
            if remote.read(backup) != before:
                raise OSError("backup mismatch: manual recovery required") from error
            # Resolve an acknowledgement lost after a successful promotion.
            if remote.exists(ACTIVE) and remote.read(ACTIVE) == after:
                return "active and backup verified after command error"
            if remote.exists(ACTIVE):
                remote.rename(ACTIVE, failed)
            remote.rename(backup, ACTIVE)
            if remote.read(ACTIVE) != before:
                raise OSError("rollback unverified: manual recovery required") from error
            raise OSError("activation failed; original INI restored and verified") from error
        raise
    return "active and remote backup verified"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("host")
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
    output = Path(os.environ["LOCALAPPDATA"]) / "XboxRemoteDev" / "recovery" / ("360stream-" + stamp)
    output.mkdir(parents=True, exist_ok=False)
    remote = Remote(args.host)
    before = remote.read(ACTIVE)
    after = candidate(before)
    (output / "launch.before.ini").write_bytes(before)
    (output / "launch.candidate.ini").write_bytes(after)
    if digest(remote.read(r"Usb0:\Xbox360Stream.xex")) != XEX_SHA:
        raise ValueError("remote capture XEX differs from audited binary")
    report = {"host": args.host, "target": ACTIVE, "before_sha256": digest(before),
              "after_sha256": digest(after), "plugin3": PLUGIN.decode(),
              "local_recovery": str(output), "applied": False,
              "module_load_or_reboot_requested": False, "capture_verified": False}
    try:
        if args.apply:
            backup = rf"Usb0:\launch-xrd-bak-{stamp}.ini"
            report["remote_backup"] = backup
            report["result"] = apply(remote, before, after, backup,
                                     rf"Usb0:\launch-xrd-new-{stamp}.ini",
                                     rf"Usb0:\launch-xrd-bad-{stamp}.ini")
            report["applied"] = True
    except Exception as error:
        report["error"] = str(error)
        raise
    finally:
        (output / "configuration-report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
