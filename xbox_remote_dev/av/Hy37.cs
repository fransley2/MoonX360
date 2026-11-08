// Independently implemented from the observable HY37 wire format.
// No 360Stream source or binary is included in this project.
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Runtime.InteropServices;

namespace XboxRemoteDev.AV {
    internal sealed class Packet {
        internal int Codec, Mode, Width, Height, Quality;
        internal uint Sequence, CaptureUs, EncodeUs;
        internal byte[] Payload;
    }

    internal static class Hy37 {
        internal const int HeaderSize = 28, MaxPayload = 2 * 1024 * 1024;
        internal static int U16(byte[] b, int p) { return (b[p] << 8) | b[p+1]; }
        internal static uint U32(byte[] b, int p) {
            return ((uint)b[p] << 24) | ((uint)b[p+1] << 16) | ((uint)b[p+2] << 8) | b[p+3];
        }
        internal static byte[] Exact(Stream stream, int size, bool allowCleanEof) {
            if (size < 0 || size > MaxPayload) throw new InvalidDataException("Length exceeds limit");
            byte[] b = new byte[size];
            int n = 0;
            while (n < size) {
                int count = stream.Read(b, n, size - n);
                if (count == 0) {
                    if (allowCleanEof && n == 0) return null;
                    throw new EndOfStreamException("Remote EOF after " + n + "/" + size + " bytes");
                }
                n += count;
            }
            return b;
        }
        internal static Packet Read(Stream stream) {
            byte[] b = Exact(stream, HeaderSize, true);
            if (b == null) return null;
            if (U32(b, 0) != 0x48593337) throw new InvalidDataException("Bad HY37 magic");
            uint length = U32(b, 12);
            Packet p = new Packet { Codec=b[4], Mode=b[5], Width=U16(b,6), Height=U16(b,8),
                Quality=U16(b,10), Sequence=U32(b,16), CaptureUs=U32(b,20), EncodeUs=U32(b,24) };
            if (length > MaxPayload || (length == 0 && p.Codec != 0)) throw new InvalidDataException("Invalid payload length " + length);
            if (p.Codec == 0) {
                if (length > 4096) throw new InvalidDataException("Status exceeds limit");
            } else if (p.Codec == 3) {
                if (p.Width != 2 || (p.Height != 8 && p.Height != 16) ||
                    (p.Quality != 12000 && p.Quality != 24000 && p.Quality != 44100 && p.Quality != 48000))
                    throw new InvalidDataException("Unsupported PCM format");
                int align = p.Width * p.Height / 8;
                if (length % align != 0 || length > p.Quality * align)
                    throw new InvalidDataException("Invalid PCM size/alignment");
            } else if (p.Codec == 1 || p.Codec == 2) {
                if (p.Width < 16 || p.Width > 1920 || p.Height < 16 || p.Height > 1080 || p.Quality > 100)
                    throw new InvalidDataException("Invalid video dimensions/quality");
                if (p.Codec == 2 && (((p.Width | p.Height) & 1) != 0 || length != p.Width * p.Height * 3 / 2))
                    throw new InvalidDataException("Incomplete or odd-sized I420 frame");
            } else throw new InvalidDataException("Unknown codec " + p.Codec);
            p.Payload = Exact(stream, (int)length, false);
            return p;
        }
        private static void CheckJpeg(Packet p) {
            byte[] b=p.Payload;
            if (b.Length < 6 || b[0]!=255 || b[1]!=216 || b[b.Length-2]!=255 || b[b.Length-1]!=217)
                throw new InvalidDataException("Incomplete JPEG markers");
            int i=2;
            while (i < b.Length-1) {
                if (b[i++] != 255) throw new InvalidDataException("Invalid JPEG marker");
                while (i < b.Length && b[i]==255) i++;
                if (i >= b.Length) break;
                int marker=b[i++];
                if (marker==0xDA || marker==0xD9) break;
                if (marker==0x01 || (marker>=0xD0 && marker<=0xD8)) continue;
                if (i+2>b.Length) break;
                int size=U16(b,i);
                if (size<2 || i+size>b.Length) throw new InvalidDataException("Truncated JPEG segment");
                if (marker>=0xC0 && marker<=0xCF && marker!=0xC4 && marker!=0xC8 && marker!=0xCC) {
                    if (size<8 || U16(b,i+3)!=p.Height || U16(b,i+5)!=p.Width)
                        throw new InvalidDataException("JPEG dimensions differ from header");
                    return;
                }
                i+=size;
            }
            throw new InvalidDataException("JPEG has no supported size marker");
        }
        private static byte Clip(int n) { return (byte)(n<0?0:n>255?255:n); }
        internal static Bitmap Decode(Packet p) {
            if (p.Codec==1) {
                CheckJpeg(p);
                using (MemoryStream ms=new MemoryStream(p.Payload, false))
                using (Image decoded=Image.FromStream(ms, true, true)) {
                    if (decoded.Width!=p.Width || decoded.Height!=p.Height)
                        throw new InvalidDataException("JPEG decoded size mismatch");
                    return new Bitmap(decoded);
                }
            }
            if (p.Codec!=2) throw new InvalidDataException("Not a video packet");
            Bitmap result=new Bitmap(p.Width,p.Height,PixelFormat.Format24bppRgb);
            BitmapData data=null;
            try {
                data=result.LockBits(new Rectangle(0,0,p.Width,p.Height),ImageLockMode.WriteOnly,PixelFormat.Format24bppRgb);
                int ysize=p.Width*p.Height, uvsize=ysize/4;
                byte[] row=new byte[data.Stride];
                for (int y=0;y<p.Height;y++) {
                    for(int x=0;x<p.Width;x++) {
                        int uv=(y/2)*(p.Width/2)+x/2, c=p.Payload[y*p.Width+x]-16;
                        int d=p.Payload[ysize+uv]-128,e=p.Payload[ysize+uvsize+uv]-128;
                        row[x*3]=Clip((298*c+516*d+128)>>8);
                        row[x*3+1]=Clip((298*c-100*d-208*e+128)>>8);
                        row[x*3+2]=Clip((298*c+409*e+128)>>8);
                    }
                    Marshal.Copy(row,0,IntPtr.Add(data.Scan0,y*data.Stride),row.Length);
                }
                result.UnlockBits(data); data=null;
                return result;
            } catch { if(data!=null)result.UnlockBits(data); result.Dispose(); throw; }
        }
        internal static byte[] Profile(bool p480) {
            return System.Text.Encoding.ASCII.GetBytes(p480 ? "@640,480,45,1,4,10,0,0\n" : "@640,360,45,1,4,10,0,0\n");
        }
    }

    internal sealed class UnloadGate {
        internal bool Requested {get; private set;}
        internal bool Acknowledged {get; private set;}
        internal void Request() { Requested=true; Acknowledged=false; }
        internal void Status(string s) { if(Requested && s=="SAFE_TO_UNLOAD")Acknowledged=true; }
    }
}
