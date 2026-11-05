using System;
using System.Collections.Generic;
using System.IO;
using System.Net.Sockets;
using System.Text;

namespace XboxRemoteDev {
    internal sealed class XbdmReply {
        internal int Code;
        internal string Message;
        internal List<string> Lines = new List<string>();
    }

    internal static class XbdmClient {
        private static string ReadLine(NetworkStream stream) {
            MemoryStream bytes = new MemoryStream();
            while (bytes.Length < 1024) {
                int b = stream.ReadByte();
                if (b < 0) throw new IOException("Remote EOF in XBDM line");
                bytes.WriteByte((byte)b);
                int n = (int)bytes.Length;
                byte[] buffer = bytes.GetBuffer();
                if (n >= 2 && buffer[n - 2] == 13 && buffer[n - 1] == 10)
                    return Encoding.ASCII.GetString(buffer, 0, n - 2);
            }
            throw new IOException("XBDM line exceeds 1024 bytes");
        }

        private static XbdmReply Status(NetworkStream stream) {
            string line = ReadLine(stream);
            int code;
            if (line.Length < 4 || !Int32.TryParse(line.Substring(0, 3), out code) ||
                (line[3] != '-' && line[3] != ' '))
                throw new IOException("Malformed XBDM status: " + line);
            return new XbdmReply { Code = code, Message = line.Substring(4) };
        }

        internal static XbdmReply Send(string host, string command) {
            if (command.IndexOfAny(new char[] {'\r', '\n'}) >= 0 ||
                Encoding.ASCII.GetString(Encoding.ASCII.GetBytes(command)) != command)
                throw new ArgumentException("Invalid XBDM command");
            using (TcpClient tcp = new TcpClient()) {
                IAsyncResult pending = tcp.BeginConnect(host, 730, null, null);
                if (!pending.AsyncWaitHandle.WaitOne(3000)) {
                    tcp.Close();
                    throw new IOException("XBDM connection timeout");
                }
                tcp.EndConnect(pending);
                tcp.ReceiveTimeout = 8000;
                tcp.SendTimeout = 8000;
                using (NetworkStream stream = tcp.GetStream()) {
                    if (Status(stream).Code != 201) throw new IOException("Unexpected XBDM greeting");
                    byte[] request = Encoding.ASCII.GetBytes(command + "\r\n");
                    stream.Write(request, 0, request.Length);
                    XbdmReply reply = Status(stream);
                    if (reply.Code >= 400) throw new IOException(reply.Code + " " + reply.Message);
                    if (reply.Code == 202) {
                        for (int i = 0; i < 512; i++) {
                            string line = ReadLine(stream);
                            if (line == ".") return reply;
                            reply.Lines.Add(line.StartsWith("..") ? line.Substring(1) : line);
                        }
                        throw new IOException("XBDM multiline response exceeds limit");
                    }
                    return reply;
                }
            }
        }
    }
}
