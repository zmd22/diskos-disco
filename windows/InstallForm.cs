// SPDX-License-Identifier: MIT
// diskOS Disco! for Windows - the Install tab (firmware, version, update keys, connect, install) and, in restore mode,
// the Restore tab (connect, put FiiO's stock system back) - both need the same Disc connection.
using System;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;
using System.Windows.Forms;

namespace DiskosDisco
{
    class InstallForm : Form, IPage
    {
        readonly bool restore;
        public bool Busy { get { return busy; } }
        public void LetGo() { Release(); }

        // Restore uses the saved backup of the Disc's original system (kept in a Windows folder at install time).
        static bool HaveBackup { get { return Directory.Exists(Path.Combine(Env.BackupDir, "stock")); } }

        [DllImport("kernel32.dll")] static extern uint SetThreadExecutionState(uint flags);
        const uint ES_CONTINUOUS = 0x80000000, ES_SYSTEM_REQUIRED = 0x00000001;

        readonly Label fwLabel, uiLabel, keysLabel, discLabel, status;
        readonly RadioButton keysNew, keysOld, uiLatest, uiOwn;
        readonly Button fwButton, uiButton, keysButton, findButton, installButton, openKeys, next;
        readonly ProgressBar progress;
        readonly TextBox log;
        string firmware, keysDir, busid, uiFile;
        bool discReady, busy;
        Process keepAlive, attach;

        static string DefaultKeys
        {
            get { return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments), "diskOS Keys"); }
        }

        public InstallForm(bool restore)
        {
            this.restore = restore;
            Theme.Style(this);
            MinimumSize = new Size(680, 600);

            var root = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(28, 22, 28, 18), ColumnCount = 1, RowCount = 8 };
            for (int i = 0; i < 6; i++) root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            Controls.Add(root); root.BringToFront();

            var head = new FlowLayoutPanel { FlowDirection = FlowDirection.TopDown, AutoSize = true, WrapContents = false };
            root.Controls.Add(head, 0, 0);

            // 1. Firmware
            fwButton = MainForm.MakeButton("Choose…", false);
            fwButton.Click += (s, e) => ChooseFirmware();
            fwLabel = Hint(restore
                ? (HaveBackup ? "The backup of your Disc's original system, saved when you installed, is used. Choose a " +
                                "firmware ZIP only to use that instead."
                              : "No backup of your Disc's original system on this PC: choose FiiO's update ZIP for V2.57 " +
                                "(SNOWSKY_DISC_update_…_v257.zip) to rebuild it.")
                : "FiiO's official update ZIP for firmware V2.57 (SNOWSKY_DISC_update_…_v257.zip). Your Disc's own " +
                  "system is built from it; diskOS never shares or uploads it. Start with the Disc on stock V2.57, " +
                  "not another diskOS build.");
            root.Controls.Add(Section(restore ? "1.  Stock system" : "1.  Firmware", fwLabel, fwButton), 0, 1);

            // 2. Version: the latest release from GitHub, unless the user picks an mq_ui file of their own
            uiLatest = new RadioButton { Text = "The latest release (downloaded from GitHub)", AutoSize = true, Checked = true };
            uiOwn = new RadioButton { Text = "An mq_ui file on this PC", AutoSize = true };
            uiButton = MainForm.MakeButton("Choose…", false);
            uiButton.Click += (s, e) => ChooseUi();
            uiLabel = Hint("");
            uiLatest.CheckedChanged += (s, e) => UpdateUi();
            var uiRow = new FlowLayoutPanel { AutoSize = true, WrapContents = true, Margin = new Padding(0) };
            uiRow.Controls.Add(uiLatest); uiRow.Controls.Add(uiOwn);
            var uiBox = new TableLayoutPanel { AutoSize = true, ColumnCount = 1, Margin = new Padding(0) };
            uiBox.Controls.Add(uiRow, 0, 0); uiBox.Controls.Add(uiLabel, 0, 1);
            if (!restore) root.Controls.Add(Section("2.  Version", uiBox, uiButton), 0, 2);

            // 3. Keys
            keysNew = new RadioButton { Text = "Create new update keys (first install)", AutoSize = true, Checked = true };
            keysOld = new RadioButton { Text = "Use my existing keys folder", AutoSize = true };
            keysButton = MainForm.MakeButton("Choose…", false);
            keysButton.Click += (s, e) => ChooseKeys();
            keysLabel = Hint("");
            keysNew.CheckedChanged += (s, e) => UpdateKeys();
            var keyRow = new FlowLayoutPanel { AutoSize = true, WrapContents = true, Margin = new Padding(0) };
            keyRow.Controls.Add(keysNew); keyRow.Controls.Add(keysOld);
            var keyBox = new TableLayoutPanel { AutoSize = true, ColumnCount = 1, Margin = new Padding(0) };
            keyBox.Controls.Add(keyRow, 0, 0); keyBox.Controls.Add(keysLabel, 0, 1);
            if (!restore) root.Controls.Add(Section("3.  Update keys", keyBox, keysButton), 0, 3);

            // 4. Connect
            findButton = MainForm.MakeButton("Find my Disc", false);
            findButton.Click += (s, e) => FindDisc();
            discLabel = Hint("Turn the Disc off. Hold Volume Down and connect it to this PC with a USB data cable, then " +
                             "click Find my Disc. The screen stays black: that's normal.");
            root.Controls.Add(Section(restore ? "2.  Connect the Disc" : "4.  Connect the Disc", discLabel, findButton), 0, 4);

            // Install
            var go = new FlowLayoutPanel { AutoSize = true, WrapContents = false, Margin = new Padding(0, 14, 0, 0) };
            installButton = MainForm.MakeButton(restore ? "Restore stock" : "Install", true);
            installButton.Click += (s, e) => { if (restore) Restore(); else Install(); };
            progress = new ProgressBar { Style = ProgressBarStyle.Marquee, Width = 180, Visible = false, Margin = new Padding(12, 12, 0, 0) };
            status = new Label { AutoSize = true, ForeColor = MainForm.Ink, Margin = new Padding(12, 11, 0, 0), MaximumSize = new Size(420, 0) };
            go.Controls.Add(installButton); go.Controls.Add(progress); go.Controls.Add(status);
            root.Controls.Add(go, 0, 5);

            log = Theme.Log();
            root.Controls.Add(log, 0, 6);

            var bar = new FlowLayoutPanel { FlowDirection = FlowDirection.RightToLeft, Dock = DockStyle.Fill, AutoSize = true, Margin = new Padding(0, 12, 0, 0) };
            var copy = MainForm.MakeButton("Copy report", false);
            copy.Click += (s, e) => { try { Clipboard.SetText("diskOS Disco! for Windows " + Program.Version + "\r\n" + status.Text + "\r\n\r\n" + log.Text); } catch { } };
            openKeys = MainForm.MakeButton("Open keys folder", false);
            openKeys.Click += (s, e) => { if (keysDir != null && Directory.Exists(keysDir)) Sys.Open(keysDir); };
            next = MainForm.MakeButton("Next", true);
            next.Click += (s, e) => Shell.Current.Go("update");
            if (!restore) bar.Controls.Add(next);
            bar.Controls.Add(copy);                                 // (no Back: the tabs move between pages)
            if (!restore) bar.Controls.Add(openKeys);
            root.Controls.Add(bar, 0, 7);

            // Keys made by an earlier run are found again.
            if (File.Exists(Path.Combine(DefaultKeys, "diskos-root.pub.pem"))) { keysOld.Checked = true; keysDir = DefaultKeys; }
            UpdateKeys();
            UpdateUi();
            UpdateButtons();
            if (Demo.On) Load += (s, e) =>
            {
                firmware = @"C:\Users\you\Downloads\SNOWSKY_DISC_update_20260909_v257.zip";
                fwLabel.Text = "✔  " + Path.GetFileName(firmware); fwLabel.ForeColor = MainForm.Good;
                keysDir = DefaultKeys; keysOld.Checked = true; keysLabel.Text = "✔  " + @"C:\Users\you\Documents\diskOS Keys"; keysLabel.ForeColor = MainForm.Good;
                discReady = true; discLabel.Text = "✔  Disc connected and ready."; discLabel.ForeColor = MainForm.Good;
                busy = true; UpdateButtons(); Say(restore ? "Restoring stock… keep the Disc connected." : "Installing… about 20 minutes. Keep the Disc connected.", MainForm.Ink);
                foreach (string l in new[] { "Disc found on USB 2-1 (shared).", "usbipd: Using WSL distribution 'diskOS-Disco' to attach.",
                                             "Disc attached to diskOS's environment." }) Append(l);
            };

            // (closing is the shell's: it refuses while busy and calls LetGo, which releases the Disc)
        }

        Label Hint(string text)
        {
            return new Label { Text = text, AutoSize = true, ForeColor = MainForm.Muted, MaximumSize = new Size(480, 0), Margin = new Padding(0, 2, 0, 0) };
        }

        Control Section(string title, Control body, Button action)
        {
            var t = new TableLayoutPanel { AutoSize = true, ColumnCount = 2, Dock = DockStyle.Fill, Margin = new Padding(0, 0, 0, 12) };
            t.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
            t.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
            var box = new TableLayoutPanel { AutoSize = true, ColumnCount = 1, Margin = new Padding(0) };
            box.Controls.Add(new Label { Text = title, AutoSize = true, ForeColor = MainForm.Ink, Font = new Font(Font, FontStyle.Bold) }, 0, 0);
            box.Controls.Add(body, 0, 1);
            t.Controls.Add(box, 0, 0);
            action.Margin = new Padding(12, 4, 0, 0);
            t.Controls.Add(action, 1, 0);
            return t;
        }

        void Append(string line)
        {
            if (InvokeRequired) { BeginInvoke(new Action<string>(Append), line); return; }
            log.AppendText((log.TextLength > 0 ? "\r\n" : "") + line);
        }

        void Say(string text, Color color)
        {
            if (InvokeRequired) { BeginInvoke(new Action<string, Color>(Say), text, color); return; }
            status.Text = text; status.ForeColor = color;
        }

        void UpdateButtons()
        {
            bool keysOk = keysNew.Checked || (keysDir != null && File.Exists(Path.Combine(keysDir, "diskos-root.pub.pem")));
            bool uiOk = uiLatest.Checked || (uiFile != null && File.Exists(uiFile));
            fwButton.Enabled = keysButton.Enabled = findButton.Enabled = keysNew.Enabled = keysOld.Enabled = !busy;
            uiLatest.Enabled = uiOwn.Enabled = !busy; uiButton.Enabled = !busy && uiOwn.Checked;
            next.Enabled = !busy;
            installButton.Enabled = restore ? !busy && discReady && (firmware != null || HaveBackup)
                                            : !busy && firmware != null && uiOk && keysOk && discReady;
            openKeys.Visible = keysDir != null && Directory.Exists(keysDir);
            progress.Visible = busy;
        }

        void ChooseFirmware()
        {
            using (var d = new OpenFileDialog { Title = "Choose the Snowsky Disc firmware ZIP", Filter = "Firmware ZIP (*.zip)|*.zip" })
            {
                if (d.ShowDialog(this) != DialogResult.OK) return;
                if (Env.ToLinux(d.FileName) == null)
                {
                    MessageBox.Show(this, "Please copy the ZIP to this PC first (for example to Downloads) and choose it there.", Text);
                    return;
                }
                firmware = d.FileName;
                fwLabel.Text = "✔  " + Path.GetFileName(firmware);
                fwLabel.ForeColor = MainForm.Good;
                UpdateButtons();
            }
        }

        void ChooseUi()
        {
            using (var d = new OpenFileDialog { Title = "Choose an mq_ui file", Filter = "mq_ui|mq_ui;mq_ui*|All files|*.*" })
            {
                if (d.ShowDialog(this) != DialogResult.OK) return;
                if (Env.ToLinux(d.FileName) == null)
                {
                    MessageBox.Show(this, "Please copy the file to this PC first (not a network drive) and choose it there.", Text);
                    return;
                }
                uiFile = d.FileName; uiOwn.Checked = true;
                UpdateUi();
            }
        }

        void UpdateUi()
        {
            if (uiLatest.Checked) { uiLabel.Text = "Downloaded when you click Install and checked against the release's checksums."; uiLabel.ForeColor = MainForm.Muted; }
            else if (uiFile == null) { uiLabel.Text = "Click Choose… and pick the mq_ui file."; uiLabel.ForeColor = MainForm.Muted; }
            else { uiLabel.Text = "✔  " + uiFile; uiLabel.ForeColor = MainForm.Good; }
            UpdateButtons();
        }

        void ChooseKeys()
        {
            using (var d = new FolderBrowserDialog { Description = "Choose the folder with your diskOS keys (it contains diskos-root.pub.pem)" })
            {
                if (keysDir != null) d.SelectedPath = keysDir;
                if (d.ShowDialog(this) != DialogResult.OK) return;
                keysDir = d.SelectedPath;
                keysOld.Checked = true;
                UpdateKeys();
            }
        }

        void UpdateKeys()
        {
            if (keysNew.Checked)
            {
                if (File.Exists(Path.Combine(DefaultKeys, "root.key")))
                {
                    keysLabel.Text = "There are already keys in " + DefaultKeys + ". Use them instead (a Disc only accepts updates from the keys it was installed with).";
                    keysLabel.ForeColor = MainForm.Bad;
                }
                else
                {
                    keysLabel.Text = "Made in Documents\\diskOS Keys during the install. They sign your future updates; you'll be asked to back them up.";
                    keysLabel.ForeColor = MainForm.Muted;
                }
            }
            else if (keysDir == null)
            {
                keysLabel.Text = "Click Choose… and pick the folder with diskos-root.pub.pem (the keys you installed with before).";
                keysLabel.ForeColor = MainForm.Muted;
            }
            else if (File.Exists(Path.Combine(keysDir, "diskos-root.pub.pem")))
            {
                keysLabel.Text = "✔  " + keysDir; keysLabel.ForeColor = MainForm.Good;
            }
            else
            {
                keysLabel.Text = "✖  No diskos-root.pub.pem in " + keysDir + "."; keysLabel.ForeColor = MainForm.Bad;
            }
            UpdateButtons();
        }

        // Find the Disc in mask-ROM mode, share it with WSL (administrator, once) and attach it to diskOS's environment.
        void FindDisc()
        {
            busy = true; discReady = false; UpdateButtons();
            Say("Looking for the Disc…", MainForm.Ink);
            new Thread(() =>
            {
                string msg; bool ok = false;
                try { ok = Connect(out msg); } catch (Exception e) { msg = "Something went wrong: " + e.Message; }
                BeginInvoke(new Action(() =>
                {
                    busy = false; discReady = ok;
                    discLabel.Text = (ok ? "✔  " : "✖  ") + msg;
                    discLabel.ForeColor = ok ? MainForm.Good : MainForm.Bad;
                    status.Text = "";
                    UpdateButtons();
                }));
            }) { IsBackground = true }.Start();
        }

        bool Connect(out string msg)
        {
            string usbipd = UsbBridgeCheck.Find(), o;
            if (usbipd == null) { msg = "The USB bridge (usbipd-win) isn't installed: see the Check tab."; return false; }
            if (!Demo.On && !Env.Exists()) { msg = "diskOS's Linux environment isn't set up yet: see the Setup tab."; return false; }
            Sys.Run(usbipd, "list", out o);
            string line = null;
            foreach (string l in o.Split('\n')) if (l.ToLowerInvariant().Contains("a108:eaef")) { line = l.Trim(); break; }
            if (line == null)
            {
                msg = "No Disc found. Turn it off, hold Volume Down, then plug in the cable (a data cable, straight into the PC), and try again.";
                return false;
            }
            busid = line.Split(new[] { ' ', '\t' }, StringSplitOptions.RemoveEmptyEntries)[0];
            Append("Disc found on USB " + busid + " (" + (line.Contains("Not shared") ? "not shared yet" : line.Contains("Attached") ? "attached" : "shared") + ").");
            if (line.Contains("Not shared"))
            {
                Say("Windows will ask for permission to share the Disc with diskOS…", MainForm.Ink);
                int rc;
                if (!Sys.RunAdmin(usbipd, "bind --busid " + busid, out rc)) { msg = "Sharing needs administrator permission. Click Find my Disc and answer Yes."; return false; }
                if (rc != 0) { msg = "Couldn't share the Disc with diskOS (code " + rc + ")."; return false; }
                Append("Shared with WSL.");
            }
            if (keepAlive == null || keepAlive.HasExited) keepAlive = Env.KeepAlive();
            Thread.Sleep(1500);
            if (attach == null || attach.HasExited)
                attach = Sys.BackgroundLines(usbipd, "attach --wsl --busid " + busid + " --auto-attach", l => Append("usbipd: " + l.Replace("usbipd: ", "")));
            Say("Connecting the Disc to diskOS…", MainForm.Ink);
            for (int i = 0; i < 20; i++)
            {
                Thread.Sleep(1000);
                if (Env.Script("/opt/diskos/.venv/bin/python -c \"import sys, usb.core; sys.exit(0 if usb.core.find(idVendor=0xa108, idProduct=0xeaef) else 1)\"", out o) == 0)
                {
                    msg = "Disc connected and ready.";
                    Append("Disc attached to diskOS's environment.");
                    return true;
                }
            }
            msg = "The Disc was found but didn't reach diskOS (the log below says why). Unplug it, turn it off, hold Volume Down, " +
                  "plug it in again and click Find my Disc.";
            return false;
        }

        void Release()
        {
            try { if (attach != null && !attach.HasExited) attach.Kill(); } catch { }
            try { if (keepAlive != null && !keepAlive.HasExited) keepAlive.Kill(); } catch { }
            string usbipd = UsbBridgeCheck.Find(), o;
            if (usbipd != null && busid != null) Sys.Run(usbipd, "detach --busid " + busid, out o);
            if (keepAlive != null) Env.Stop();                 // ends the background "sleep" and frees the memory
        }

        // Restore: the saved backup (or the chosen firmware ZIP) goes back on the Disc through diskos restore-stock.
        void Restore()
        {
            string what = "Put FiiO's stock system back on the connected Disc?\n\n" +
                          (firmware != null ? "From: " + Path.GetFileName(firmware) : "From: the backup saved when you installed") + "\n\n" +
                          "Your music and the Disc's data folder stay. Keep the Disc connected and this PC on until it says Done.";
            if (MessageBox.Show(this, what, Text, MessageBoxButtons.OKCancel, MessageBoxIcon.Question) != DialogResult.OK) return;
            busy = true; UpdateButtons();
            log.Clear();
            SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED);
            string fw = firmware != null ? Env.ToLinux(firmware) : null;
            new Thread(() =>
            {
                bool ok = false; string end;
                try
                {
                    string o;
                    if (fw == null) { Say("Getting the backup ready…", MainForm.Ink); Env.RestoreBackup(out o); if (o.Length > 0) Append(o.Trim()); }
                    Say("Restoring stock… keep the Disc connected.", MainForm.Ink);
                    int rc = Env.Script("diskos restore-stock --yes" + (fw != null ? " --firmware " + Env.Q(fw) : ""), line =>
                    {
                        Append(line);
                        string t = line.Trim();
                        if (t.StartsWith("▶")) Say(t.TrimStart('▶', ' ') + "…", MainForm.Ink);
                    });
                    ok = rc == 0;
                    end = ok ? "Done. Unplug the Disc and turn it on: it starts FiiO's system." :
                          rc == 3 ? "Stopped before anything was written to the Disc." : "Restoring didn't finish (code " + rc + "). Copy report and send it.";
                }
                catch (Exception e) { end = "Something went wrong: " + e.Message; }
                BeginInvoke(new Action(() =>
                {
                    SetThreadExecutionState(ES_CONTINUOUS);
                    busy = false; Say(end, ok ? MainForm.Good : MainForm.Bad); UpdateButtons();
                }));
            }) { IsBackground = true }.Start();
        }

        void Install()
        {
            string keys = keysNew.Checked ? DefaultKeys : keysDir;
            if (keysNew.Checked && File.Exists(Path.Combine(keys, "root.key")))
            {
                MessageBox.Show(this, "There are already keys in " + keys + ". Choose \"Use my existing keys folder\".", Text);
                return;
            }
            string what = "Install diskOS Disco! on the connected Disc?\n\n" +
                          "Firmware: " + Path.GetFileName(firmware) + "\n" +
                          "Update keys: " + (keysNew.Checked ? "new, in " + keys : keys) + "\n\n" +
                          "This takes about 20 minutes. Keep the Disc connected and this PC on until it says Done. " +
                          "A backup of the Disc's original system is kept, so you can go back to stock later.\n\n" +
                          "Recommended: the Disc should be on FiiO's stock V2.57 firmware. Installing over upstream diskOS " +
                          "or another diskOS build is not supported and can be buggy.\n\n" +
                          "Windows is unsupported, but this works as intended: users have confirmed it several times.";
            if (MessageBox.Show(this, what, Text, MessageBoxButtons.OKCancel, MessageBoxIcon.Question) != DialogResult.OK) return;

            busy = true; UpdateButtons();
            log.Clear();
            SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED);   // no sleep while flashing
            bool makeKeys = keysNew.Checked, latestUi = uiLatest.Checked; string ownUi = uiFile;
            string fw = Env.ToLinux(firmware), kl = Env.ToLinux(keys);
            new Thread(() =>
            {
                bool ok = false; string end;
                try { ok = RunInstall(makeKeys, latestUi, ownUi, fw, kl, out end); }
                catch (Exception e) { end = "Something went wrong: " + e.Message; }
                BeginInvoke(new Action(() =>
                {
                    SetThreadExecutionState(ES_CONTINUOUS);
                    busy = false;
                    keysDir = keys; keysOld.Checked = true; UpdateKeys();
                    Say(end, ok ? MainForm.Good : MainForm.Bad);
                    UpdateButtons();
                    if (ok)
                        MessageBox.Show(this,
                            "Done! Unplug the Disc and turn it on. diskOS Disco! installs itself on the first start.\n\n" +
                            "Important: copy the \"diskOS Keys\" folder (in Documents) to a USB stick or another safe place. " +
                            "Your Disc only accepts updates signed with these keys.\n\nHappy listening!", Text, MessageBoxButtons.OK, MessageBoxIcon.Information);
                }));
            }) { IsBackground = true }.Start();
        }

        bool RunInstall(bool makeKeys, bool latestUi, string ownUi, string fw, string keys, out string end)
        {
            string o;
            // The interface: the latest release unless the user chose a file. Offline, the copy built into this app.
            string ui = null;
            if (latestUi)
            {
                Say("Downloading the latest release…", MainForm.Ink);
                string file, tag, err;
                if (Latest.Fetch(Path.Combine(Env.Home, "install-ui"), Append, out file, out tag, out err)) ui = Env.ToLinux(file);
                else
                {
                    Append(err);
                    bool useBuiltIn = false;
                    Invoke(new Action(() => useBuiltIn = MessageBox.Show(this,
                        err + "\n\nInstall the version built into this app instead?", Text,
                        MessageBoxButtons.YesNo, MessageBoxIcon.Question) == DialogResult.Yes));
                    if (!useBuiltIn) { end = err; return false; }
                    Append("Using the version built into this app.");
                }
            }
            else ui = Env.ToLinux(ownUi);
            if (makeKeys)
            {
                Say("Making your update keys…", MainForm.Ink);
                int k = Env.Script("rm -rf /tmp/diskos-keys\nsh /opt/diskos/tools/diskos-keys.sh /tmp/diskos-keys\n" +
                                   "mkdir -p " + Env.Q(keys) + "\ncp /tmp/diskos-keys/* " + Env.Q(keys) + "/\nrm -rf /tmp/diskos-keys", Append);
                if (k != 0) { end = "Making the keys failed (code " + k + ")."; return false; }
            }
            Say("Installing… about 20 minutes. Keep the Disc connected.", MainForm.Ink);
            int rc = Env.Script("diskos install --variant public --yes " + (ui != null ? "--ui " + Env.Q(ui) + " " : "") + "--firmware " + Env.Q(fw) +
                                " --ota-key " + Env.Q(keys + "/diskos-root.pub.pem"), line =>
            {
                Append(line);
                string t = line.Trim();
                if (t.StartsWith("▶")) Say(t.TrimStart('▶', ' ') + "…", MainForm.Ink);
            });
            Env.SaveBackup(out o); Append(o);
            if (rc == 0) { end = "Done. Unplug the Disc and turn it on."; return true; }
            end = rc == 3 ? "Stopped before anything was written to the Disc." : "The install didn't finish (code " + rc + "). Copy report and send it.";
            return false;
        }
    }
}
