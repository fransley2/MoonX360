"""Bounded XBDM transport. One command per connection, TCP port 730.

The protocol framing follows the public OXDK xbdm client and the XBDM
responses observed in docs/XBOX_REMOTE_VALIDATION_20261104.md.
"""
from __future__ import annotations

from dataclasses import dataclass
import hashlib
import re
import socket
import struct
from typing import BinaryIO


STATUS = re.compile(rb"^(\d{3})[- ]([^\r\n]*)\r\n$")
MAX_LINE = 1024
MAX_LINES = 512
MAX_BINARY = 32 * 1024 * 1024


class XbdmError(OSError):
    def __init__(self, message: str, *, expected: int = 0, received: int = 0,
                 reason: str = "protocol"):
        super().__init__(message)
        self.expected = expected
        self.received = received
        self.reason = reason


@dataclass(frozen=True)
class Reply:
    code: int
    message: str
    lines: tuple[str, ...] = ()


class Client:
    def __init__(self, host: str, port: int = 730, timeout: float = 10):
        self.host, self.port, self.timeout = host, port, timeout
        self.sock: socket.socket | None = None
        self.received = 0

    def __enter__(self):
        self.sock = socket.create_connection((self.host, self.port), timeout=self.timeout)
        try:
            self.sock.settimeout(self.timeout)
            greeting = self.status()
            if greeting.code != 201:
                raise XbdmError(f"XBDM greeting: {greeting.code} {greeting.message}")
        except BaseException:
            self.sock.close()
            self.sock = None
            raise
        return self

    def __exit__(self, *_):
        if self.sock:
            self.sock.close()
        self.sock = None

    def line(self) -> bytes:
        data = bytearray()
        try:
            while len(data) < MAX_LINE:
                part = self.sock.recv(1)
                if not part:
                    raise XbdmError(f"remote EOF in line after {len(data)} bytes", reason="eof")
                data.extend(part)
                if data.endswith(b"\r\n"):
                    return bytes(data)
        except socket.timeout as exc:
            raise XbdmError(f"timeout in line after {len(data)} bytes", reason="timeout") from exc
        raise XbdmError("XBDM line exceeds limit")

    def status(self) -> Reply:
        raw = self.line()
        match = STATUS.fullmatch(raw)
        if not match:
            raise XbdmError(f"malformed XBDM status: {raw[:120]!r}")
        return Reply(int(match[1]), match[2].decode("ascii", "replace"))

    def send(self, command: str) -> Reply:
        if not command.isascii() or any(c in command for c in "\r\n"):
            raise ValueError("XBDM command must be one ASCII line")
        self.sock.sendall(command.encode("ascii") + b"\r\n")
        reply = self.status()
        if reply.code >= 400:
            raise XbdmError(f"{command.split(' ', 1)[0]}: {reply.code} {reply.message}",
                            reason=f"status-{reply.code}")
        if reply.code == 202:
            lines = []
            for _ in range(MAX_LINES):
                line = self.line()
                if line == b".\r\n":
                    return Reply(reply.code, reply.message, tuple(lines))
                if line.startswith(b".."):
                    line = line[1:]
                lines.append(line[:-2].decode("ascii", "replace"))
            raise XbdmError("XBDM multiline response exceeds limit")
        return reply

    def receive(self, output: BinaryIO, expected: int) -> dict:
        if not 0 <= expected <= MAX_BINARY:
            raise XbdmError(f"binary length outside limit: {expected}")
        digest = hashlib.sha256()
        received = 0
        try:
            while received < expected:
                chunk = self.sock.recv(min(65536, expected - received))
                if not chunk:
                    raise XbdmError(f"remote EOF after {received}/{expected} bytes",
                                    expected=expected, received=received, reason="eof")
                output.write(chunk)
                digest.update(chunk)
                received += len(chunk)
        except socket.timeout as exc:
            raise XbdmError(f"timeout after {received}/{expected} bytes",
                            expected=expected, received=received, reason="timeout") from exc
        self.received += received
        return {"expected_bytes": expected, "received_bytes": received,
                "sha256": digest.hexdigest()}

    def getfile(self, path: str, output: BinaryIO) -> dict:
        if not path.isascii() or any(c in path for c in '"\r\n'):
            raise ValueError("invalid remote path")
        reply = self.send(f'getfile name="{path}"')
        if reply.code != 203:
            raise XbdmError(f"getfile expected 203, got {reply.code}")
        length = struct.unpack("<I", self._exact(4))[0]
        return self.receive(output, length)

    def _exact(self, length: int) -> bytes:
        data = bytearray()
        while len(data) < length:
            try:
                chunk = self.sock.recv(length - len(data))
            except socket.timeout as exc:
                raise XbdmError("timeout in binary header", expected=length,
                                received=len(data), reason="timeout") from exc
            if not chunk:
                raise XbdmError("remote EOF in binary header", expected=length,
                                received=len(data), reason="eof")
            data.extend(chunk)
        return bytes(data)

    def screenshot(self, output: BinaryIO) -> dict:
        reply = self.send("screenshot")
        if reply.code != 203:
            raise XbdmError(f"screenshot expected 203, got {reply.code}")
        header = self.line().decode("ascii", "replace").strip()
        fields = {k: int(v, 16) for k, v in re.findall(r"(\w+)=(0x[0-9a-fA-F]+)", header)}
        required = ("pitch", "width", "height", "framebuffersize", "format")
        if any(k not in fields for k in required):
            raise XbdmError(f"incomplete screenshot metadata: {header}")
        width, height, pitch, size = (fields[k] for k in
                                      ("width", "height", "pitch", "framebuffersize"))
        if not (0 < width <= 4096 and 0 < height <= 2160 and
                width * 4 <= pitch <= 32768 and pitch * height <= size <= MAX_BINARY):
            raise XbdmError(f"invalid screenshot dimensions: {header}")
        transfer = self.receive(output, size)
        return {"metadata": fields, "header": header, **transfer}
