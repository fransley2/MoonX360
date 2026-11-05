using System;
using System.Drawing;
using System.IO;
using System.Threading;
using System.Windows.Forms;

namespace XboxRemoteDev {
    internal sealed class MainForm : Form {
        private readonly TextBox host = new TextBox();
        private readonly TextBox directory = new TextBox();
        private readonly TextBox terminal = new TextBox();
        private readonly TextBox output = new TextBox();
        private readonly Label state = new Label();
        private readonly Button connect = new Button();
        private readonly Button modules = new Button();
        private readonly Button browse = new Button();
        private readonly Button run = new Button();
        private bool busy;

        internal MainForm() {
            Text = "Xbox Remote Dev — read-only XBDM";
            ClientSize = new Size(840, 590);
            MinimumSize = new Size(700, 450);
            Font = new Font("Segoe UI", 9F);

            host.SetBounds(14, 15, 180, 26);
            host.Text = "192.168.15.10";
            connect.SetBounds(202, 14, 95, 27);
            connect.Text = "Connect";
            connect.Click += delegate { Execute("getexecstate"); };
            state.SetBounds(310, 18, 500, 25);
            state.Text = "Disconnected";
            modules.SetBounds(14, 54, 125, 28);
            modules.Text = "Modules";
            modules.Click += delegate { Execute("modules"); };
            directory.SetBounds(151, 55, 435, 26);
            directory.Text = "HDD:\\Apps\\MoonX360";
            browse.SetBounds(594, 54, 95, 28);
            browse.Text = "Browse";
            browse.Click += delegate {
                if (directory.Text.IndexOfAny(new char[] {'"', '\r', '\n'}) >= 0) {
                    MessageBox.Show("Invalid path"); return;
                }
                Execute("dirlist name=\"" + directory.Text + "\"");
            };
            Label mode = new Label();
            mode.SetBounds(700, 59, 130, 22);
            mode.Text = "Read-only mode";
            terminal.SetBounds(14, 95, 572, 26);
            terminal.Text = "help";
            run.SetBounds(594, 94, 95, 28);
            run.Text = "Run";
            run.Click += delegate {
                string command = terminal.Text.Trim().ToLowerInvariant();
                if (command != "help" && command != "modules" &&
                    command != "getexecstate") {
                    MessageBox.Show("Terminal allows help, modules, getexecstate only.");
                    return;
                }
                Execute(command);
            };
            output.SetBounds(14, 132, 812, 442);
            output.Multiline = true;
            output.ReadOnly = true;
            output.ScrollBars = ScrollBars.Both;
            output.WordWrap = false;
            output.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;
            Controls.AddRange(new Control[] {host, connect, state, modules, directory,
                                              browse, mode, terminal, run, output});
            output.Text = "Read-only XBDM diagnostics. Privileged pairing, launch, file writes, and remote input are unavailable in this stage.\r\n";
        }

        private void Execute(string command) {
            if (busy) return;
            busy = true;
            connect.Enabled = modules.Enabled = browse.Enabled = run.Enabled = false;
            state.Text = "Connecting…";
            string target = host.Text.Trim();
            ThreadPool.QueueUserWorkItem(delegate {
                string message;
                bool success;
                try {
                    XbdmReply reply = XbdmClient.Send(target, command);
                    message = command + " → " + reply.Code + " " + reply.Message +
                              "\r\n" + String.Join("\r\n", reply.Lines.ToArray()) + "\r\n";
                    success = true;
                } catch (Exception error) {
                    message = command + " → " + error.Message + "\r\n";
                    success = false;
                }
                if (!IsDisposed && IsHandleCreated) BeginInvoke((MethodInvoker)delegate {
                    output.AppendText(message);
                    state.Text = success ? "XBDM reachable; title state unverified" : "Disconnected / error";
                    connect.Enabled = modules.Enabled = browse.Enabled = run.Enabled = true;
                    busy = false;
                });
            });
        }
    }

    internal static class Program {
        [STAThread]
        private static void Main() {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new MainForm());
        }
    }
}
