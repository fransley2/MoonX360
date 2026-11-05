import io
import socket
import struct
import threading
import unittest
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "host"))
from screenshot import convert
from xbdm import Client, XbdmError


class Server:
    def __init__(self, responses):
        self.responses = responses
        self.listener = socket.socket()
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen(1)
        self.port = self.listener.getsockname()[1]
        self.command = None
        self.worker = threading.Thread(target=self.run, daemon=True)
        self.worker.start()

    def run(self):
        with self.listener:
            conn, _ = self.listener.accept()
            with conn:
                conn.sendall(b"201- hello\r\n")
                incoming = bytearray()
                while not incoming.endswith(b"\r\n"):
                    incoming.extend(conn.recv(1))
                self.command = bytes(incoming)
                for response in self.responses:
                    conn.sendall(response)

    def finish(self):
        self.worker.join(2)


class ProtocolTests(unittest.TestCase):
    def test_multiline_and_dot_unescape(self):
        server = Server([b"202- list follows\r\nfirst\r\n..escaped\r\n.\r\n"])
        with Client("127.0.0.1", server.port) as client:
            reply = client.send("modules")
        server.finish()
        self.assertEqual(server.command, b"modules\r\n")
        self.assertEqual(reply.lines, ("first", ".escaped"))

    def test_414_is_reported(self):
        server = Server([b"414- access denied\r\n"])
        with Client("127.0.0.1", server.port) as client:
            with self.assertRaises(XbdmError) as caught:
                client.getfile("Hdd1:\\Apps\\MoonX360\\default.xex", io.BytesIO())
        server.finish()
        self.assertEqual(caught.exception.reason, "status-414")

    def test_getfile_exact_length(self):
        server = Server([b"203- binary\r\n" + struct.pack("<I", 5) + b"hello"])
        output = io.BytesIO()
        with Client("127.0.0.1", server.port) as client:
            transfer = client.getfile("HDD:\\Apps\\MoonX360\\note.txt", output)
        server.finish()
        self.assertEqual(transfer["received_bytes"], 5)
        self.assertEqual(output.getvalue(), b"hello")

    def test_short_screenshot_keeps_byte_count_and_refuses_png(self):
        header = (b"pitch=0x10 width=0x4 height=0x4 format=0x100 "
                  b"framebuffersize=0x40\r\n")
        server = Server([b"203- binary\r\n" + header + b"X" * 12])
        output = io.BytesIO()
        with Client("127.0.0.1", server.port) as client:
            with self.assertRaises(XbdmError) as caught:
                client.screenshot(output)
        server.finish()
        self.assertEqual((caught.exception.reason, caught.exception.expected,
                          caught.exception.received), ("eof", 64, 12))
        self.assertEqual(caught.exception.metadata["framebuffersize"], 64)
        self.assertEqual(len(output.getvalue()), 12)
        with self.assertRaises(ValueError):
            convert(output.getvalue(), {"width": 4, "height": 4,
                                        "format": 0x100, "framebuffersize": 64})

    def test_complete_binary_and_png(self):
        header = (b"pitch=0x80 width=0x20 height=0x20 format=0x100 "
                  b"framebuffersize=0x1000\r\n")
        server = Server([b"203- binary\r\n" + header + b"\x00" * 4096])
        output = io.BytesIO()
        with Client("127.0.0.1", server.port) as client:
            transfer = client.screenshot(output)
        server.finish()
        self.assertEqual(transfer["received_bytes"], 4096)
        self.assertTrue(convert(output.getvalue(), transfer["metadata"]).startswith(b"\x89PNG"))


if __name__ == "__main__":
    unittest.main()
