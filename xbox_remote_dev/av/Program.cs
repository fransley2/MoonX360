using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Windows.Forms;

namespace XboxRemoteDev.AV {
    internal sealed class Viewer : Form {
        private readonly Receiver receiver=new Receiver();
        private readonly ComboBox local=new ComboBox(),profile=new ComboBox();
        private readonly TextBox console=new TextBox(),log=new TextBox();
        private readonly PictureBox picture=new PictureBox();
        private readonly Label status=new Label(),metrics=new Label();
        private readonly Button start=new Button(),prepare=new Button();
        private readonly Timer timer=new Timer();
        private string displayedSession;
        internal Viewer() {
            Text="Xbox Remote Dev - game A/V capture test";ClientSize=new Size(1000,760);MinimumSize=new Size(850,650);
            Font=new Font("Segoe UI",9);BackColor=Color.FromArgb(24,26,29);ForeColor=Color.White;
            Label intro=new Label {Text="360Stream HY37 receiver - external Xbox module required",AutoSize=true,Left=14,Top=12};Controls.Add(intro);
            local.SetBounds(14,40,160,26);local.DropDownStyle=ComboBoxStyle.DropDownList;
            foreach(NetworkInterface nic in NetworkInterface.GetAllNetworkInterfaces()) {
                if(nic.OperationalStatus!=OperationalStatus.Up)continue;
                foreach(UnicastIPAddressInformation ip in nic.GetIPProperties().UnicastAddresses)
                    if(ip.Address.AddressFamily==AddressFamily.InterNetwork && !IPAddress.IsLoopback(ip.Address))local.Items.Add(ip.Address.ToString());
            }
            if(local.Items.Count>0)local.SelectedIndex=0;
            if(local.Items.Contains("192.168.15.8"))local.SelectedItem="192.168.15.8";
            console.SetBounds(184,40,140,26);console.Text="192.168.15.10";
            profile.SetBounds(334,40,200,26);profile.DropDownStyle=ComboBoxStyle.DropDownList;
            profile.Items.AddRange(new object[]{"360p / 10 FPS / Q45","480p / 10 FPS / Q45"});profile.SelectedIndex=0;
            start.SetBounds(544,39,105,28);start.Text="Listen";
            start.Click+=delegate {
                try{receiver.Start(local.Text,console.Text.Trim(),profile.SelectedIndex==1);start.Enabled=false;local.Enabled=console.Enabled=profile.Enabled=false;}
                catch(Exception ex){MessageBox.Show(this,ex.Message,"Cannot listen");}
            };
            CheckBox mute=new CheckBox {Text="Mute PC",Left=665,Top=43,Width=95};mute.CheckedChanged+=delegate{receiver.Muted=mute.Checked;};
            Button source=new Button {Text="Next audio source",Left=770,Top=39,Width=160,Height=28};source.Click+=delegate{receiver.NextAudioDriver();};
            status.SetBounds(14,78,960,24);status.Text="Select this PC's LAN IPv4, then Listen. The Xbox connects to TCP 36000.";
            metrics.SetBounds(14,104,960,42);
            picture.SetBounds(14,150,972,430);picture.BackColor=Color.Black;picture.SizeMode=PictureBoxSizeMode.Zoom;
            picture.Anchor=AnchorStyles.Top|AnchorStyles.Bottom|AnchorStyles.Left|AnchorStyles.Right;
            prepare.SetBounds(14,594,200,30);prepare.Text="Prepare module unload";
            prepare.Anchor=AnchorStyles.Bottom|AnchorStyles.Left;
            prepare.Click+=delegate{try{receiver.PrepareUnload();}catch(Exception ex){MessageBox.Show(this,ex.Message);}};
            Button save=new Button {Text="Save displayed frame",Left=224,Top=594,Width=180,Height=30,Anchor=AnchorStyles.Bottom|AnchorStyles.Left};
            save.Click+=delegate {
                if(picture.Image==null)return;
                string path=Path.Combine(receiver.EvidenceRoot,"frame-"+DateTime.UtcNow.ToString("yyyyMMdd-HHmmss-fff")+".png");
                try{picture.Image.Save(path,ImageFormat.Png);MessageBox.Show(this,path,"Frame saved");}
                catch(Exception ex){MessageBox.Show(this,ex.Message);}
            };
            Button report=new Button {Text="Save report",Left=414,Top=594,Width=130,Height=30,Anchor=AnchorStyles.Bottom|AnchorStyles.Left};
            report.Click+=delegate{try{MessageBox.Show(this,receiver.SaveReport(),"Report saved");}catch(Exception ex){MessageBox.Show(this,ex.Message);}};
            Label warning=new Label {Text="Unload acknowledgement comes from the module. Never change games before preparing and manually unloading it.",
                Left=14,Top=630,Width=970,Height=20,Anchor=AnchorStyles.Bottom|AnchorStyles.Left};
            log.SetBounds(14,654,972,92);log.Multiline=true;log.ReadOnly=true;log.ScrollBars=ScrollBars.Vertical;
            log.Anchor=AnchorStyles.Bottom|AnchorStyles.Left|AnchorStyles.Right;
            Controls.AddRange(new Control[]{local,console,profile,start,mute,source,status,metrics,picture,prepare,save,report,warning,log});
            foreach(Control control in Controls) {
                if(control is Button || control is ComboBox || control is TextBox)control.ForeColor=Color.Black;
                Button button=control as Button;
                if(button!=null){button.BackColor=Color.FromArgb(225,225,225);button.UseVisualStyleBackColor=false;}
            }
            timer.Interval=50;timer.Tick+=delegate {
                var m=receiver.Snapshot();
                string session=(string)m["started_utc"];
                if(session!=displayedSession) {
                    displayedSession=session;
                    if(picture.Image!=null){picture.Image.Dispose();picture.Image=null;}
                }
                Bitmap frame=receiver.TakeFrame();if(frame!=null){Image old=picture.Image;picture.Image=frame;if(old!=null)old.Dispose();}
                foreach(string s in receiver.DrainLog())log.AppendText(s+Environment.NewLine);
                if(log.TextLength>16000)log.Text=log.Text.Substring(log.TextLength-12000);
                status.Text=receiver.UnloadAcknowledged?"SAFE_TO_UNLOAD received from this session's module; unload manually in your loader.":
                    receiver.IsConnected?"Receiving - unload acknowledgement pending":receiver.IsRunning?
                    "Waiting for Xbox connection; any displayed frame is from the previous session":"Stopped - select addresses and click Listen";
                status.ForeColor=receiver.UnloadAcknowledged?Color.LightGreen:Color.White;
                metrics.Text=String.Format("{0} | decoded {1} frames | audio {2} packets / {3} bytes | source {4}\r\nWire {5} bytes | sequence gaps {6} | audio playback drops {7}",
                    m["received_format"],m["decoded_video_frames"],m["audio_packets"],m["audio_bytes"],m["audio_source"],m["wire_bytes"],m["sequence_gaps_including_startup"],m["audio_playback_packets_dropped_or_trimmed"]);
                prepare.Enabled=receiver.IsConnected && !receiver.UnloadAcknowledged;
                if(!receiver.IsRunning){start.Enabled=true;local.Enabled=console.Enabled=profile.Enabled=true;}
            };timer.Start();
            FormClosing+=delegate(object sender,FormClosingEventArgs e) {
                if(receiver.NeedsUnloadAcknowledgement && MessageBox.Show(this,
                    "The Xbox module has not acknowledged SAFE_TO_UNLOAD. Closing this viewer does not unload it. Close anyway?",
                    "Module still active",MessageBoxButtons.YesNo,MessageBoxIcon.Warning)!=DialogResult.Yes){e.Cancel=true;return;}
                timer.Stop();receiver.Stop();if(picture.Image!=null)picture.Image.Dispose();
            };
        }
    }
    internal static class Program {
        [STAThread] private static void Main() {
            Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);Application.Run(new Viewer());
        }
    }
}
