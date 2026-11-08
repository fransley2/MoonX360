using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using System.Web.Script.Serialization;

namespace XboxRemoteDev.AV {
    internal sealed class Receiver {
        private readonly object stateLock=new object(), writeLock=new object(), frameLock=new object(), reportLock=new object();
        private readonly Queue<string> messages=new Queue<string>();
        private readonly HashSet<uint> drivers=new HashSet<uint>();
        private readonly Queue<double> capture=new Queue<double>(), encode=new Queue<double>();
        private TcpListener listener;
        private TcpClient client;
        private NetworkStream stream;
        private Thread worker;
        private Bitmap latest;
        private volatile bool running;
        internal volatile bool Muted;
        private bool connected,hasDriver;
        private uint selectedDriver;
        private UnloadGate gate=new UnloadGate();
        private string bind,expected,profile,sessionPath,startUtc,lastError="",lastStatus="",format="",audioError="";
        private long videoPackets,decodedFrames,audioPackets,audioBytes,wireBytes,videoGaps,audioDrops,previewDrops,audioSubmittedBytes,audioSubmittedPackets;
        private uint? lastSequence;
        private DateTime started;
        private DateTime? ended;
        private int sessionNumber;
        internal bool IsRunning {get{return running;}}
        internal bool IsConnected {get{lock(stateLock)return connected;}}
        internal bool UnloadAcknowledged {get{lock(stateLock)return gate.Acknowledged;}}
        internal bool NeedsUnloadAcknowledgement {get{lock(stateLock)return started!=default(DateTime) && !gate.Acknowledged;}}
        internal readonly string EvidenceRoot=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"XboxRemoteDev","av-evidence");

        private void Log(string message) {
            lock(stateLock) {
                if(messages.Count>=128)messages.Dequeue();
                messages.Enqueue(DateTime.Now.ToString("HH:mm:ss")+" "+message.Replace("\r"," ").Replace("\n"," "));
            }
        }
        internal string[] DrainLog() { lock(stateLock) { string[] r=messages.ToArray();messages.Clear();return r; } }
        internal Bitmap TakeFrame() {lock(frameLock){Bitmap r=latest;latest=null;return r;}}
        private static bool LocalRange(IPAddress ip) {
            byte[] b=ip.GetAddressBytes();
            return b.Length==4 && (b[0]==10 || b[0]==127 || (b[0]==192 && b[1]==168) || (b[0]==172 && b[1]>=16 && b[1]<=31));
        }
        internal void Start(string local,string console,bool p480) {
            if(running)throw new InvalidOperationException("Receiver already running");
            IPAddress ip=IPAddress.Parse(local),peer=IPAddress.Parse(console);
            if(!LocalRange(ip) || !LocalRange(peer))throw new ArgumentException("Use specific private LAN IPv4 addresses");
            bind=local;expected=console;profile=p480?"640x480 10 FPS Q45 1 worker C4":"640x360 10 FPS Q45 1 worker C4";
            Directory.CreateDirectory(EvidenceRoot);
            listener=new TcpListener(ip,36000);listener.Start(1);running=true;
            worker=new Thread(delegate(){Listen(peer,p480);});worker.IsBackground=true;worker.Start();
            Log("Listening on "+local+":36000; accepted peer "+console);
        }
        private void ResetSession() {
            lock(stateLock) {
                connected=true;gate=new UnloadGate();hasDriver=false;selectedDriver=0;drivers.Clear();
                capture.Clear();encode.Clear();lastSequence=null;
                videoPackets=decodedFrames=audioPackets=audioBytes=wireBytes=videoGaps=audioDrops=previewDrops=audioSubmittedBytes=audioSubmittedPackets=0;
                lastStatus=lastError=format=audioError="";started=DateTime.UtcNow;ended=null;startUtc=started.ToString("o");
                sessionNumber++;
                sessionPath=Path.Combine(EvidenceRoot,started.ToString("yyyyMMdd-HHmmss-fff")+"-"+sessionNumber+".json");
            }
            lock(frameLock) { if(latest!=null)latest.Dispose();latest=null; }
        }
        private void Listen(IPAddress peer,bool p480) {
            try {
                while(running) {
                    TcpClient accepted=listener.AcceptTcpClient();
                    if(!((IPEndPoint)accepted.Client.RemoteEndPoint).Address.Equals(peer)) {
                        accepted.Close();Log("Rejected unexpected peer");continue;
                    }
                    AudioOutput audio=null;
                    uint? outputDriver=null;
                    bool audioFailed=false;
                    try {
                        accepted.NoDelay=true;accepted.ReceiveTimeout=5000;accepted.SendTimeout=1500;
                        accepted.ReceiveBufferSize=256*1024;
                        lock(writeLock){client=accepted;stream=accepted.GetStream();}
                        ResetSession();Log("Connected; requesting "+profile+" and stereo 48 kHz/16-bit PCM");
                        Send(Hy37.Profile(p480));Send(Encoding.ASCII.GetBytes("JLO"));
                        while(running) {
                            Packet p=Hy37.Read(stream);
                            if(p==null) {Log("Remote EOF between packets");break;}
                            lock(stateLock)wireBytes+=Hy37.HeaderSize+p.Payload.Length;
                            if(p.Codec==0) {
                                string text=Encoding.ASCII.GetString(p.Payload).TrimEnd('\0','\r','\n');
                                lock(stateLock){lastStatus=text;gate.Status(text);}
                                Log("Module status: "+text);continue;
                            }
                            if(p.Codec==3) {
                                uint use;
                                lock(stateLock) {
                                    if(!drivers.Contains(p.CaptureUs) && drivers.Count>=32)throw new InvalidDataException("Audio driver count exceeds limit");
                                    audioPackets++;audioBytes+=p.Payload.Length;drivers.Add(p.CaptureUs);
                                    if(!hasDriver){hasDriver=true;selectedDriver=p.CaptureUs;}
                                    use=selectedDriver;
                                }
                                if(Muted || (outputDriver.HasValue && outputDriver.Value!=use)) {
                                    if(audio!=null){audio.Dispose();audio=null;}outputDriver=null;
                                }
                                if(!Muted && !audioFailed && p.CaptureUs==use) {
                                    try {
                                        if(audio==null){audio=new AudioOutput();outputDriver=use;}
                                        long before=audio.DroppedPackets;int submitted=audio.Submit(p);
                                        lock(stateLock){audioDrops+=audio.DroppedPackets-before;audioSubmittedBytes+=submitted;if(submitted>0)audioSubmittedPackets++;}
                                    } catch(Exception ex) {audioFailed=true;lock(stateLock)audioError=ex.Message;Log("Audio playback failed: "+ex.Message);}
                                }
                                continue;
                            }
                            lock(stateLock) {
                                videoPackets++;
                                if(lastSequence.HasValue) {
                                    uint delta=unchecked(p.Sequence-lastSequence.Value);
                                    if(delta>1 && delta<0x80000000)videoGaps+=delta-1;
                                }
                                lastSequence=p.Sequence;
                                if(capture.Count>=600)capture.Dequeue();capture.Enqueue(p.CaptureUs/1000.0);
                                if(encode.Count>=600)encode.Dequeue();encode.Enqueue(p.EncodeUs/1000.0);
                                format=p.Width+"x"+p.Height+(p.Codec==1?" JPEG":" I420");
                            }
                            Bitmap image=Hy37.Decode(p);
                            lock(frameLock) {
                                if(latest!=null){latest.Dispose();lock(stateLock)previewDrops++;}
                                latest=image;
                            }
                            lock(stateLock)decodedFrames++;
                        }
                    } catch(Exception ex) {
                        if(running){lock(stateLock)lastError=ex.GetType().Name+": "+ex.Message;Log("Session error: "+ex.Message);}
                    } finally {
                        if(audio!=null)try{audio.Dispose();}catch(Exception ex){Log("Audio cleanup: "+ex.Message);}
                        lock(writeLock){accepted.Close();client=null;stream=null;}
                        lock(stateLock){connected=false;ended=DateTime.UtcNow;}
                        try{SaveReport();}catch(Exception ex){Log("Report error: "+ex.Message);}
                        Log("Session ended. Disconnect alone does not confirm unload readiness.");
                    }
                }
            } catch(Exception ex) {if(running)Log("Listener error: "+ex.Message);}
            finally {running=false;if(listener!=null)listener.Stop();}
        }
        private void Send(byte[] b) {
            lock(writeLock) {
                if(stream==null)throw new InvalidOperationException("No active stream");
                stream.Write(b,0,b.Length);
            }
        }
        internal void PrepareUnload() {
            lock(writeLock) {
                if(stream==null)throw new InvalidOperationException("No active stream; cannot request preparation");
                lock(stateLock)gate.Request();
                stream.WriteByte((byte)'X');
            }
            Log("Prepare requested. Wait for the module's SAFE_TO_UNLOAD acknowledgement.");
        }
        internal void NextAudioDriver() {
            lock(stateLock) {
                List<uint> list=new List<uint>(drivers);list.Sort();
                if(list.Count==0)return;
                int n=list.IndexOf(selectedDriver);selectedDriver=list[(n+1)%list.Count];hasDriver=true;
            }
        }
        private static object Timing(Queue<double> values) {
            double[] b=values.ToArray();Array.Sort(b);double sum=0;foreach(double d in b)sum+=d;
            return new {samples=b.Length,mean_ms=b.Length==0?0:sum/b.Length,p95_ms=b.Length==0?0:b[(int)Math.Ceiling(b.Length*0.95)-1]};
        }
        internal Dictionary<string,object> Snapshot() {
            lock(stateLock) {
                double seconds=started==default(DateTime)?0:Math.Max(0.001,((ended??DateTime.UtcNow)-started).TotalSeconds);
                return new Dictionary<string,object> {
                    {"measurement_origin", "HY37 TCP receiver; source provenance and game session require physical confirmation"},
                    {"simulation",expected=="127.0.0.1"},{"game_session_verified",false},
                    {"started_utc",startUtc},{"ended_utc",ended.HasValue?ended.Value.ToString("o"):null},
                    {"receiver_ip",bind},{"expected_console_ip",expected},
                    {"connected",connected},{"requested_profile",profile},{"received_format",format},
                    {"complete_video_packets",videoPackets},{"decoded_video_frames",decodedFrames},
                    {"audio_packets",audioPackets},{"audio_bytes",audioBytes},{"wire_bytes",wireBytes},
                    {"elapsed_seconds",seconds},{"average_decoded_fps",seconds==0?0:decodedFrames/seconds},
                    {"sequence_gaps_including_startup",videoGaps},{"preview_frames_replaced",previewDrops},
                    {"audio_playback_packets_dropped_or_trimmed",audioDrops},
                    {"audio_bytes_submitted_to_winmm",audioSubmittedBytes},{"audio_packets_submitted_to_winmm",audioSubmittedPackets},
                    {"audio_playback_error",audioError},{"audio_audibility_verified",false},
                    {"pc_audio_muted",Muted},
                    {"audio_source",hasDriver?"0x"+selectedDriver.ToString("X8"):"none"},
                    {"audio_source_count",drivers.Count},{"capture_reported_by_module",Timing(capture)},
                    {"encode_reported_by_module",Timing(encode)},{"last_status",lastStatus},{"error",lastError},
                    {"prepare_requested",gate.Requested},{"safe_to_unload_reported_by_module",gate.Acknowledged},
                    {"av_sync_measured",false},{"end_to_end_latency_measured",false}
                };
            }
        }
        internal string SaveReport() {
            string path;Dictionary<string,object> snapshot;
            lock(stateLock){path=sessionPath;snapshot=Snapshot();}
            if(path==null)throw new InvalidOperationException("No session to save");
            lock(reportLock)File.WriteAllText(path,new JavaScriptSerializer().Serialize(snapshot),Encoding.UTF8);
            return path;
        }
        internal void Stop() {
            running=false;
            if(listener!=null)listener.Stop();
            lock(writeLock)if(client!=null)client.Close();
            if(worker!=null)worker.Join(1500);
            lock(frameLock){if(latest!=null)latest.Dispose();latest=null;}
        }
    }
}
