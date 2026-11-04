// SPDX-License-Identifier: MIT
// diskOS Disco! for Windows - step 4 page: make a signed SD-card update with your own keys.
using System;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Net;
using System.Threading;
using System.Windows.Forms;

namespace DiskosDisco
{
    class UpdateForm : Form, IPage
    {
        public bool Busy { get { return busy; } }
        public void LetGo() { }
        // The shell shows this tab: look for cards again (the Disc may have been switched to USB storage meanwhile).
        internal void Arrived() { if (!busy) FindCards(); }

        readonly RadioButton latest, ownFile;
        readonly Label uiLabel, keysLabel, sdLabel, status;
        readonly Button uiButton, keysButton, sdRefresh, sdOther, make;
        readonly ComboBox sd;
        readonly ProgressBar progress;
        readonly TextBox log;
        string uiFile, keysDir, otherFolder;
        bool busy;

        public UpdateForm()
        {
            Theme.Style(this);
            MinimumSize = new Size(680, 600);

            var root = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(28, 18, 28, 18), ColumnCount = 1, RowCount = 7 };
            for (int i = 0; i < 5; i++) root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            Controls.Add(root); root.BringToFront();

            root.Controls.Add(new Label
            {
                Text = "Once diskOS Disco! is on your Disc, new versions install from the SD card: no cable, no flashing. This page " +
                       "signs a version with your keys and puts it on the card. It works for any Disc installed with these keys, " +
                       "from this PC or another; it only needs diskOS's Linux environment (the Setup tab).",
                AutoSize = true, ForeColor = Theme.Muted, MaximumSize = new Size(700, 0), Margin = new Padding(0, 0, 0, 14)
            }, 0, 0);

            // 1. Which version
            latest = new RadioButton { Text = "The latest release (downloaded from GitHub)", AutoSize = true, Checked = true };
            ownFile = new RadioButton { Text = "An mq_ui file on this PC", AutoSize = true };
            uiButton = MainForm.MakeButton("Choose…", false);
            uiButton.Click += (s, e) => ChooseUi();
            uiLabel = Hint("");
            latest.CheckedChanged += (s, e) => Refresh2();
            var uiBox = new TableLayoutPanel { AutoSize = true, ColumnCount = 1, Margin = new Padding(0) };
            var uiRow = new FlowLayoutPanel { AutoSize = true, Margin = new Padding(0) };
            uiRow.Controls.Add(latest); uiRow.Controls.Add(ownFile);
            uiBox.Controls.Add(uiRow, 0, 0); uiBox.Controls.Add(uiLabel, 0, 1);
            root.Controls.Add(Section("1.  Version", uiBox, uiButton), 0, 1);

            // 2. Keys
            keysButton = MainForm.MakeButton("Choose…", false);
            keysButton.Click += (s, e) => ChooseKeys();
            keysLabel = Hint("");
            root.Controls.Add(Section("2.  Your update keys", keysLabel, keysButton), 0, 2);

            // 3. SD card
            sd = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Width = 420, BackColor = Theme.Card, ForeColor = Theme.Text, FlatStyle = FlatStyle.Flat, Margin = new Padding(0, 4, 0, 0) };
            sd.SelectedIndexChanged += (s, e) => Refresh2();
            sdLabel = Hint("Put the Disc's SD card in this PC (or connect the Disc in USB storage mode).");
            var sdBox = new TableLayoutPanel { AutoSize = true, ColumnCount = 1, Margin = new Padding(0) };
            sdBox.Controls.Add(sdLabel, 0, 0); sdBox.Controls.Add(sd, 0, 1);
            sdRefresh = MainForm.MakeButton("Look again", false);
            sdRefresh.Click += (s, e) => FindCards();
            root.Controls.Add(Section("3.  SD card", sdBox, sdRefresh), 0, 3);

            var go = new FlowLayoutPanel { AutoSize = true, WrapContents = false, Margin = new Padding(0, 10, 0, 0) };
            make = MainForm.MakeButton("Make the update", true);
            make.Click += (s, e) => Make();
            sdOther = MainForm.MakeButton("Save to a folder instead…", false);
            sdOther.Click += (s, e) => ChooseFolder();
            progress = new ProgressBar { Style = ProgressBarStyle.Marquee, Width = 160, Visible = false, Margin = new Padding(12, 12, 0, 0) };
            status = new Label { AutoSize = true, ForeColor = Theme.Text, Margin = new Padding(12, 11, 0, 0), MaximumSize = new Size(360, 0) };
            go.Controls.Add(make); go.Controls.Add(sdOther); go.Controls.Add(progress); go.Controls.Add(status);
            root.Controls.Add(go, 0, 4);

            log = Theme.Log();
            root.Controls.Add(log, 0, 5);

            var bar = new FlowLayoutPanel { FlowDirection = FlowDirection.RightToLeft, Dock = DockStyle.Fill, AutoSize = true, Margin = new Padding(0, 12, 0, 0) };
            var copy = MainForm.MakeButton("Copy report", false);
            copy.Click += (s, e) => { try { Clipboard.SetText("diskOS Disco! for Windows " + Program.Version + "\r\n" + status.Text + "\r\n\r\n" + log.Text); } catch { } };
            bar.Controls.Add(copy);                                 // (no Back: the tabs move between pages)
            root.Controls.Add(bar, 0, 6);

            string def = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments), "diskOS Keys");
            if (File.Exists(Path.Combine(def, "leaf.key"))) keysDir = def;
            FindCards();
            if (Demo.On)
            {
                keysDir = @"C:\Users\you\Documents\diskOS Keys";
                sd.Items.Add(new Card { Root = "E:\\", Text = "E:  SNOWSKY  (128 GB)" }); sd.SelectedIndex = 0;
                sdLabel.Text = "The update goes to the top of this card, in a folder called diskos-update.";
            }
            Refresh2();
            FormClosing += (s, e) => { if (busy) { e.Cancel = true; MessageBox.Show(this, "Please wait until the update is on the card.", Text); } };
        }

        Label Hint(string text)
        {
            return new Label { Text = text, AutoSize = true, ForeColor = Theme.Muted, MaximumSize = new Size(480, 0), Margin = new Padding(0, 2, 0, 0) };
        }

        Control Section(string title, Control body, Button action)
        {
            var t = new TableLayoutPanel { AutoSize = true, ColumnCount = 2, Dock = DockStyle.Fill, Margin = new Padding(0, 0, 0, 12) };
            t.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
            t.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
            var box = new TableLayoutPanel { AutoSize = true, ColumnCount = 1, Margin = new Padding(0) };
            box.Controls.Add(new Label { Text = title, AutoSize = true, ForeColor = Theme.Text, Font = new Font(Font, FontStyle.Bold) }, 0, 0);
            box.Controls.Add(body, 0, 1);
            t.Controls.Add(box, 0, 0);
            action.Margin = new Padding(12, 4, 0, 0);
            t.Controls.Add(action, 1, 0);
            return t;
        }

        class Card { public string Root, Text; public override string ToString() { return Text; } }

        void FindCards()
        {
            sd.Items.Clear();
            foreach (var d in DriveInfo.GetDrives())
            {
                try
                {
                    if (d.DriveType != DriveType.Removable || !d.IsReady) continue;
                    string name = d.VolumeLabel.Length > 0 ? d.VolumeLabel : "Removable disk";
                    sd.Items.Add(new Card { Root = d.RootDirectory.FullName, Text = d.Name.TrimEnd('\\') + "  " + name + "  (" + d.TotalSize / (1024L * 1024 * 1024) + " GB)" });
                }
                catch { }
            }
            if (otherFolder != null) sd.Items.Add(new Card { Root = otherFolder, Text = "Folder: " + otherFolder });
            if (sd.Items.Count > 0) sd.SelectedIndex = otherFolder != null ? sd.Items.Count - 1 : 0;   // a chosen folder, else the first card
            sdLabel.Text = sd.Items.Count == 0 ? "No SD card found. Put the Disc's card in this PC (or connect the Disc in USB storage mode) and click Look again."
                                               : "The update goes to the top of this card, in a folder called diskos-update.";
            Refresh2();
        }

        void ChooseFolder()
        {
            using (var d = new FolderBrowserDialog { Description = "Where should the diskos-update folder go?" })
            {
                if (d.ShowDialog(this) != DialogResult.OK) return;
                otherFolder = d.SelectedPath;
                FindCards();
            }
        }

        void ChooseUi()
        {
            using (var d = new OpenFileDialog { Title = "Choose an mq_ui file", Filter = "mq_ui|mq_ui;mq_ui*|All files|*.*" })
            {
                if (d.ShowDialog(this) != DialogResult.OK) return;
                uiFile = d.FileName; ownFile.Checked = true; Refresh2();
            }
        }

        void ChooseKeys()
        {
            using (var d = new FolderBrowserDialog { Description = "Choose the folder with your diskOS keys (it contains leaf.key)" })
            {
                if (keysDir != null) d.SelectedPath = keysDir;
                if (d.ShowDialog(this) != DialogResult.OK) return;
                keysDir = d.SelectedPath; Refresh2();
            }
        }

        bool KeysOk { get { return keysDir != null && (Demo.On || new[] { "leaf.key", "leafpub.pem", "keyauthz", "keyauthz.sig" }.All(f => File.Exists(Path.Combine(keysDir, f)))); } }

        void Refresh2()
        {
            if (latest.Checked) { uiLabel.Text = "Downloaded and checked against the release's checksums."; uiLabel.ForeColor = Theme.Muted; }
            else if (uiFile == null) { uiLabel.Text = "Click Choose… and pick the mq_ui file."; uiLabel.ForeColor = Theme.Muted; }
            else { uiLabel.Text = "✔  " + uiFile; uiLabel.ForeColor = Theme.Green; }
            if (keysDir == null) { keysLabel.Text = "Choose the keys folder you installed with (it contains leaf.key). It's usually Documents\\diskOS Keys."; keysLabel.ForeColor = Theme.Muted; }
            else if (KeysOk) { keysLabel.Text = "✔  " + keysDir; keysLabel.ForeColor = Theme.Green; }
            else { keysLabel.Text = "✖  " + keysDir + " doesn't have leaf.key, leafpub.pem, keyauthz and keyauthz.sig."; keysLabel.ForeColor = Theme.Red; }
            uiButton.Enabled = keysButton.Enabled = sdRefresh.Enabled = sdOther.Enabled = latest.Enabled = ownFile.Enabled = sd.Enabled = !busy;
            make.Enabled = !busy && KeysOk && sd.SelectedItem != null && (latest.Checked || uiFile != null);
            progress.Visible = busy;
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

        void Make()
        {
            if (!Demo.On && !Env.Exists())
            {
                if (MessageBox.Show(this, "Signing runs in diskOS's own Linux environment, which isn't set up on this PC yet. " +
                                    "Set it up now on the Setup tab? (No Disc needed; it takes a minute or two.)", Text,
                                    MessageBoxButtons.YesNo, MessageBoxIcon.Information) == DialogResult.Yes)
                    Shell.Current.Go("setup");
                return;
            }
            var card = (Card)sd.SelectedItem;
            bool download = latest.Checked;
            string file = uiFile, keys = keysDir;
            busy = true; Refresh2(); log.Clear();
            new Thread(() =>
            {
                bool ok = false; string end;
                try { ok = Build(download, file, keys, card.Root, out end); }
                catch (Exception e) { end = "Something went wrong: " + e.Message; }
                BeginInvoke(new Action(() =>
                {
                    busy = false; Refresh2(); Say(end, ok ? Theme.Green : Theme.Red);
                    if (ok)
                        MessageBox.Show(this,
                            "The update is on the card.\n\n1. Eject the card safely and put it back in the Disc.\n" +
                            "2. On the Disc: Settings › System › Maintenance › Update from SD Card › Update, then restart.\n" +
                            "3. Play some music for a few minutes: that keeps the new version.\n\nHappy listening!",
                            Text, MessageBoxButtons.OK, MessageBoxIcon.Information);
                }));
            }) { IsBackground = true }.Start();
        }

        bool Build(bool download, string file, string keys, string target, out string end)
        {
            string work = Path.Combine(Env.Home, "update"), label = "custom";
            Directory.CreateDirectory(work);
            if (download)
            {
                Say("Downloading the latest release…", Theme.Text);
                string tag, err;
                if (!Latest.Fetch(work, Append, out file, out tag, out err)) { end = err; return false; }
                label = tag.TrimStart('v');
            }
            if (Env.ToLinux(file) == null || Env.ToLinux(keys) == null)
            {
                end = "Please copy the mq_ui file and the keys folder to this PC (not a network drive) and try again.";
                return false;
            }
            Say("Signing with your keys…", Theme.Text);
            string outDir = Path.Combine(work, "diskos-update");
            int rc = Env.Script("sh /opt/diskos/tools/diskos-sign.sh " + Env.Q(Env.ToLinux(file)) + " " + Env.Q(label) + " " +
                                Env.Q(Env.ToLinux(keys)) + " " + Env.Q(Env.ToLinux(outDir)), Append);
            if (rc != 0) { end = "Signing didn't work (code " + rc + "). Copy report and send it."; return false; }

            Say("Copying to the card…", Theme.Text);
            // The six files one by one, overwriting the previous run's (never the folder as a whole: on some cards
            // deleting the old folder silently fails and the new one ends up nested inside it - docs/UPDATING.md).
            string dest = Path.Combine(target, "diskos-update");
            Directory.CreateDirectory(dest);
            foreach (string name in new[] { "mq_ui", "update.manifest", "update.manifest.sig", "leafpub.pem", "keyauthz", "keyauthz.sig" })
            {
                string src = Path.Combine(outDir, name), dst = Path.Combine(dest, name);
                File.Copy(src, dst, true);
                byte[] a = File.ReadAllBytes(src), b = File.ReadAllBytes(dst);
                bool same = a.Length == b.Length;
                for (int i = 0; same && i < a.Length; i++) same = a[i] == b[i];
                if (!same) { end = name + " on the card doesn't match the signed file. Copy it again."; return false; }
                Append("Copied " + name);
            }
            Append("Wrote " + dest);
            end = "Done: the update is on the card.";
            return true;
        }

    }
}
