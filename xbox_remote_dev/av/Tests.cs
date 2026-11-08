using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Net.Sockets;
using System.Text;
using System.Threading;

namespace XboxRemoteDev.AV {
    internal static class Tests {
        private static int passed;
        private static void Assert(bool condition,string message) {if(!condition)throw new Exception(message);}
        private static void Test(string name,Action f) {f();passed++;Console.WriteLine("PASS "+name);}
        private static void Fails(Action f) {
            try{f();}catch(InvalidDataException){return;}catch(EndOfStreamException){return;}
            throw new Exception("Expected protocol rejection");
        }
        private static void Put16(byte[] b,int n,int value){b[n]=(byte)(value>>8);b[n+1]=(byte)value;}
        private static void Put32(byte[] b,int n,uint value){b[n]=(byte)(value>>24);b[n+1]=(byte)(value>>16);b[n+2]=(byte)(value>>8);b[n+3]=(byte)value;}
        private static byte[] Wire(int codec,int w,int h,int q,byte[] payload,uint sequence,uint cap) {
            byte[] b=new byte[28+payload.Length];Put32(b,0,0x48593337);b[4]=(byte)codec;
            Put16(b,6,w);Put16(b,8,h);Put16(b,10,q);Put32(b,12,(uint)payload.Length);
            Put32(b,16,sequence);Put32(b,20,cap);Buffer.BlockCopy(payload,0,b,28,payload.Length);return b;
        }
        private static Packet Read(byte[] b){using(MemoryStream s=new MemoryStream(b))return Hy37.Read(s);}
        private static byte[] Jpeg() {
            using(Bitmap b=new Bitmap(16,16))using(Graphics g=Graphics.FromImage(b))using(MemoryStream s=new MemoryStream()) {
                g.Clear(Color.Lime);b.Save(s,ImageFormat.Jpeg);return s.ToArray();
            }
        }
        private static byte[] Raw() {
            byte[] b=new byte[16*16*3/2];for(int i=0;i<256;i++)b[i]=81;
            for(int i=256;i<320;i++)b[i]=90;for(int i=320;i<b.Length;i++)b[i]=240;return b;
        }
        private static void Wait(Func<bool> ready) {
            DateTime until=DateTime.UtcNow.AddSeconds(4);
            while(!ready()){if(DateTime.UtcNow>until)throw new Exception("Timed out");Thread.Sleep(10);}
        }
        private static void NetworkSession() {
            Receiver r=new Receiver();r.Muted=true;
            try {
                r.Start("127.0.0.1","127.0.0.1",false);
                using(TcpClient c=new TcpClient("127.0.0.1",36000)) {
                    c.ReceiveTimeout=4000;c.SendTimeout=4000;NetworkStream s=c.GetStream();
                    byte[] expected=Encoding.ASCII.GetBytes("@640,360,45,1,4,10,0,0\nJLO");
                    byte[] got=Hy37.Exact(s,expected.Length,false);
                    Assert(Encoding.ASCII.GetString(got)==Encoding.ASCII.GetString(expected),"Initial controls mismatch");
                    byte[][] packets={Wire(0,0,0,0,Encoding.ASCII.GetBytes("AV_STREAM_V38_CONNECTED"),0,0),
                        Wire(1,16,16,45,Jpeg(),UInt32.MaxValue,2000),Wire(2,16,16,45,Raw(),0,3000),
                        Wire(3,2,16,48000,new byte[1024],1,0x12345678)};
                    foreach(byte[] p in packets)for(int i=0;i<p.Length;i+=7)s.Write(p,i,Math.Min(7,p.Length-i));
                    Wait(delegate{return (long)r.Snapshot()["decoded_video_frames"]==2 && (long)r.Snapshot()["audio_packets"]==1;});
                    r.PrepareUnload();Assert(s.ReadByte()=='X',"Prepare command absent");
                    byte[] ack=Wire(0,0,0,0,Encoding.ASCII.GetBytes("SAFE_TO_UNLOAD"),0,0);s.Write(ack,0,ack.Length);
                    Wait(delegate{return r.UnloadAcknowledged;});
                    c.Close();Wait(delegate{return !r.IsConnected;});
                    Assert((long)r.Snapshot()["sequence_gaps_including_startup"]==0,"Sequence wrap was counted as loss");
                    Assert((bool)r.Snapshot()["simulation"],"Loopback was not marked simulation");
                    Assert(!(bool)r.Snapshot()["game_session_verified"],"Simulation became hardware proof");
                    Assert(File.Exists(r.SaveReport()),"Missing session report");
                }
                using(TcpClient c=new TcpClient("127.0.0.1",36000)) {
                    c.ReceiveTimeout=4000;NetworkStream s=c.GetStream();
                    Hy37.Exact(s,Hy37.Profile(false).Length+3,false);
                    Assert(!r.UnloadAcknowledged,"Reconnect inherited old unload acknowledgement");
                    byte[] incomplete=Wire(3,2,16,48000,new byte[1024],0,1);
                    s.Write(incomplete,0,incomplete.Length-1);c.Close();
                    Wait(delegate{return !r.IsConnected;});
                    Assert((long)r.Snapshot()["audio_packets"]==0,"Partial PCM counted as complete");
                    Assert(((string)r.Snapshot()["error"]).Contains("1023/1024"),"EOF byte counts missing");
                    Assert(!r.UnloadAcknowledged,"Disconnect became unload acknowledgement");
                }
            } finally {r.Stop();}
        }
        private static int Main() {
            try {
                Test("fixed network-order audio vector",delegate {
                    string hex="48593337030000020010BB8000000400000000011234567800000000";
                    byte[] b=new byte[28+1024];for(int i=0;i<28;i++)b[i]=Convert.ToByte(hex.Substring(i*2,2),16);
                    Packet p=Read(b);Assert(p.Quality==48000 && p.CaptureUs==0x12345678 && p.Payload.Length==1024,"Bad endian/field mapping");
                });
                Test("clean EOF",delegate{Assert(Read(new byte[0])==null,"Clean EOF failed");});
                Test("empty status payload remains valid",delegate{Assert(Read(Wire(0,0,0,0,new byte[0],0,0)).Payload.Length==0,"Empty status rejected");});
                Test("partial header",delegate{Fails(delegate{Read(new byte[27]);});});
                Test("partial payload",delegate{byte[] b=Wire(3,2,16,48000,new byte[1024],1,1);Array.Resize(ref b,b.Length-1);Fails(delegate{Read(b);});});
                Test("oversized length rejected before allocation",delegate{byte[] b=Wire(0,0,0,0,new byte[1],0,0);Put32(b,12,UInt32.MaxValue);Fails(delegate{Read(b);});});
                Test("unknown codec",delegate{Fails(delegate{Read(Wire(9,16,16,45,new byte[1],0,0));});});
                Test("invalid audio channels/rate/alignment",delegate {
                    Fails(delegate{Read(Wire(3,6,16,48000,new byte[1024],0,0));});
                    Fails(delegate{Read(Wire(3,2,16,16000,new byte[1024],0,0));});
                    Fails(delegate{Read(Wire(3,2,16,48000,new byte[3],0,0));});
                });
                Test("I420 exact size and even dimensions",delegate {
                    Fails(delegate{Read(Wire(2,17,16,45,Raw(),0,0));});
                    Fails(delegate{Read(Wire(2,16,16,45,new byte[383],0,0));});
                });
                Test("I420 BT601 red pixel",delegate {
                    using(Bitmap image=Hy37.Decode(Read(Wire(2,16,16,45,Raw(),0,0)))) {
                        Color c=image.GetPixel(8,8);Assert(c.R>245 && c.G<5 && c.B<5,"Wrong YUV colour order");
                    }
                });
                Test("JPEG decode and dimension mismatch",delegate {
                    byte[] jpg=Jpeg();using(Bitmap image=Hy37.Decode(Read(Wire(1,16,16,45,jpg,0,0))))Assert(image.Width==16,"Decode failed");
                    Fails(delegate{using(Bitmap image=Hy37.Decode(Read(Wire(1,32,16,45,jpg,0,0)))){}});
                });
                Test("truncated JPEG rejected",delegate {
                    byte[] jpg=Jpeg();Array.Resize(ref jpg,jpg.Length-1);
                    Fails(delegate{using(Bitmap image=Hy37.Decode(Read(Wire(1,16,16,45,jpg,0,0)))){}});
                });
                Test("unload needs explicit request and exact acknowledgement",delegate {
                    UnloadGate g=new UnloadGate();g.Status("SAFE_TO_UNLOAD");Assert(!g.Acknowledged,"Unsolicited status accepted");
                    g.Request();g.Status("STOPPING_WORKERS");Assert(!g.Acknowledged,"Premature acknowledgement");
                    g.Status("SAFE_TO_UNLOAD");Assert(g.Acknowledged,"Missing acknowledgement");
                    g=new UnloadGate();Assert(!g.Acknowledged,"New connection inherited old acknowledgement");
                });
                Test("full receiver loopback, fragmented JPEG/I420/PCM and unload",NetworkSession);
                Console.WriteLine(passed+" tests passed. Simulation only; no Xbox capture validated.");return 0;
            } catch(Exception ex){Console.Error.WriteLine(ex);return 1;}
        }
    }
}
