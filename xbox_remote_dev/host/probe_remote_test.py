"""Upload and observe only the standalone RemoteTest title under a new name.

Never overwrites default.xex, launch.ini, or any existing remote file. Requires
Aurora and debugger state=start before changing titles.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import io
import json
from pathlib import Path
import re
import time

from xbdm import Client


ROOT = "HDD:\\Apps\\RemoteAgent\\"
AURORA = "HDD:\\Apps\\Aurora\\Aurora.xex"


def command(host, line):
    with Client(host) as client:
        reply = client.send(line)
    return {"code": reply.code, "message": reply.message, "lines": reply.lines}


def title(host):
    modules = command(host, "modules")["lines"]
    for line in modules:
        if "base=0x82000000" in line:
            match = re.search(r'name="([^"]+)"', line)
            return match.group(1) if match else None
    return None


def remote_size(host, filename):
    lines = command(host, 'dirlist name="' + ROOT[:-1] + '"')["lines"]
    for line in lines:
        match = re.search(r'name="([^"]+)" sizehi=0x([0-9a-fA-F]+) sizelo=0x([0-9a-fA-F]+)', line)
        if match and match.group(1).lower() == filename.lower():
            return (int(match.group(2), 16) << 32) | int(match.group(3), 16)
    return None


def upload_new(host, local, filename):
    if not re.fullmatch(r"RemoteTest-[0-9]{8}(-[0-9]+)?\.xex", filename):
        raise ValueError("remote name must be a dated RemoteTest XEX")
    if remote_size(host, filename) is not None:
        raise ValueError("remote file exists; refusing overwrite")
    data = local.read_bytes()
    if len(data) < 4096 or data[:4] != b"XEX2":
        raise ValueError("local RemoteTest is not a valid XEX2")
    remote = ROOT + filename
    with Client(host, timeout=30) as client:
        reply = client.send(f'sendfile name="{remote}" length=0x{len(data):x}')
        if reply.code != 204:
            raise OSError(f"sendfile expected 204, got {reply.code}")
        client.sock.sendall(data)
        completion = client.status()
        if completion.code != 200:
            raise OSError(f"sendfile completion: {completion.code} {completion.message}")
    size = remote_size(host, filename)
    if size != len(data):
        raise OSError(f"remote size {size} differs from local {len(data)}")
    result = {"remote": remote, "bytes": size,
              "local_sha256": hashlib.sha256(data).hexdigest(), "remote_hash_verified": False,
              "sendfile_completion": {"code": completion.code, "message": completion.message}}
    try:
        with Client(host) as client:
            buffer = io.BytesIO()
            transfer = client.getfile(remote, buffer)
        result["remote_sha256"] = transfer["sha256"]
        result["remote_hash_verified"] = transfer["sha256"] == result["local_sha256"]
        if not result["remote_hash_verified"]:
            raise OSError("remote hash differs from local")
    except OSError as error:
        result["remote_hash_error"] = str(error)
    return result


def launch(host, path):
    directory = path.rsplit("\\", 1)[0] + "\\"
    return command(host, f'magicboot title="{path}" directory="{directory}"')


def run(host, local, output, filename, use_existing=False, accept_size_only=False,
        receipt_path=None):
    if output.exists():
        raise ValueError("use a new evidence path")
    if not re.fullmatch(r"RemoteTest-[0-9]{8}(-[0-9]+)?\.xex", filename):
        raise ValueError("remote name must be a dated RemoteTest XEX")
    local_bytes = local.read_bytes()
    if len(local_bytes) < 4096 or local_bytes[:4] != b"XEX2":
        raise ValueError("local RemoteTest is not XEX2")
    report = {"host": host, "collected_utc": datetime.now(timezone.utc).isoformat(),
              "source_sha256": hashlib.sha256(local_bytes).hexdigest()}
    try:
        report["state_before"] = command(host, "getexecstate")
        report["title_before"] = title(host)
        if report["state_before"]["message"].strip() != "start" or report["title_before"] != "Aurora.xex":
            raise OSError("expected Aurora.xex and debugger start; refusing launch")
        if use_existing:
            if receipt_path is None:
                raise ValueError("existing upload requires the original probe receipt")
            receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
            previous = receipt.get("upload", {})
            if (previous.get("remote") != ROOT + filename or
                    previous.get("bytes") != len(local_bytes) or
                    previous.get("local_sha256") != report["source_sha256"]):
                raise ValueError("upload receipt does not match file and local hash")
            size = remote_size(host, filename)
            if size != local.stat().st_size:
                raise OSError(f"existing remote size {size} differs from local {local.stat().st_size}")
            report["upload"] = {"remote": ROOT + filename, "bytes": size,
                                "local_sha256": report["source_sha256"],
                                "remote_hash_verified": False, "existing_remote_file": True,
                                "receipt": str(receipt_path)}
        else:
            report["upload"] = upload_new(host, local, filename)
        if not report["upload"]["remote_hash_verified"]:
            if not accept_size_only:
                raise OSError("remote XEX hash unverified; refusing launch")
            report["remote_hash_unverified_acknowledged"] = True
        report["launch_reply"] = launch(host, report["upload"]["remote"])
        report["observations"] = []
        start = time.monotonic()
        for target in (0.5, 1, 3, 6, 10, 14):
            time.sleep(max(0, target - (time.monotonic() - start)))
            observed = {"elapsed_seconds": round(time.monotonic() - start, 3)}
            try:
                observed["title"] = title(host)
                observed["exec_state"] = command(host, "getexecstate")["message"].strip()
            except OSError as error:
                observed["error"] = str(error)
            report["observations"].append(observed)
        report["test_loaded"] = any(x.get("title") == filename for x in report["observations"])
        report["title_after"] = title(host)
        if report["title_after"] in (filename, None):
            report["restore_reply"] = launch(host, AURORA)
            time.sleep(2)
            report["restored_title"] = title(host)
        report["state_after"] = command(host, "getexecstate")
    except Exception as error:
        report["error"] = str(error)
    finally:
        if "launch_reply" in report and "restore_reply" not in report:
            try:
                current = title(host)
                if current in (filename, None):
                    report["restore_reply"] = launch(host, AURORA)
                    time.sleep(2)
                    report["restored_title"] = title(host)
            except OSError as restore_error:
                report["restore_error"] = str(restore_error)
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("host")
    parser.add_argument("local", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--remote-name", default="RemoteTest-20261108.xex")
    parser.add_argument("--use-existing-upload", action="store_true",
                        help="Use an already uploaded file only if its size matches; never overwrite")
    parser.add_argument("--accept-size-only", action="store_true",
                        help="Allow title test after matching upload completion/size when getfile is broken")
    parser.add_argument("--receipt", type=Path,
                        help="Original probe JSON proving successful new-name upload")
    args = parser.parse_args()
    result = run(args.host, args.local, args.output, args.remote_name,
                 args.use_existing_upload, args.accept_size_only, args.receipt)
    print(json.dumps(result, indent=2))
    if "error" in result or result.get("test_loaded") is not True:
        raise SystemExit(1)
