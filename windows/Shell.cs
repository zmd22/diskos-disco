// SPDX-License-Identifier: MIT
// diskOS Disco! for Windows - the one window: the header with its tabs, and the page shown below it.
// Every tab works on its own at any time (Update doesn't wait for an install from this PC); a page that is busy
// keeps its tab until it finishes, and the window can't close while the Disc is being written.
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Windows.Forms;

namespace DiskosDisco
{
    // A page that can be working (flashing, signing, setting up): the shell asks before leaving or closing.
    interface IPage
    {
        bool Busy { get; }
        void LetGo();        // the window is closing: let go of anything held (USB attach, keep-alive)
    }

    class Shell : Form
    {
        public static Shell Current;
        public static readonly string[] Keys = { "check", "setup", "install", "update", "restore" };
        public static readonly string[] Names = { "Check", "Setup", "Install", "Update", "Restore" };
        static readonly string[] Titles = { "Can this PC install diskOS?", "diskOS's own Linux environment",
                                            "Install on your Disc", "Update from the SD card", "Back to stock" };

        readonly Header header;
        readonly Panel body;
        readonly Label title;
        readonly Dictionary<string, Form> pages = new Dictionary<string, Form>();
        Form shown;
        string key;

        public Shell(string start)
        {
            Current = this;
            Theme.Style(this);
            StartPosition = FormStartPosition.CenterScreen;
            ClientSize = new Size(820, 820);
            MinimumSize = new Size(740, 700);

            body = new Panel { Dock = DockStyle.Fill, BackColor = Theme.Bg };
            title = new Label { Dock = DockStyle.Top, Height = 56, Padding = new Padding(26, 18, 26, 0), ForeColor = Theme.Text,
                                Font = new Font("Segoe UI Semibold", 13f), BackColor = Theme.Bg };
            header = new Header(start);
            header.Go += Go;
            // docking runs from the last control added: header on top, then the page title, then the page
            Controls.Add(body); Controls.Add(title); Controls.Add(header);
            Load += (s, e) => Go(start);
            FormClosing += (s, e) =>
            {
                foreach (var f in pages.Values)
                {
                    var p = f as IPage;
                    if (p != null && p.Busy)
                    {
                        e.Cancel = true;
                        MessageBox.Show(this, "Please wait until this step finishes. Don't unplug the Disc.", Text);
                        return;
                    }
                }
                foreach (var f in pages.Values) { var p = f as IPage; if (p != null) p.LetGo(); }
            };
        }

        public void Go(string k)
        {
            int i = Array.IndexOf(Keys, k);
            if (i < 0 || k == key) return;
            var busy = shown as IPage;
            if (busy != null && busy.Busy)
            {
                MessageBox.Show(this, "Please wait until this step finishes.", Text);
                return;
            }
            Form f;
            if (!pages.TryGetValue(k, out f))
            {
                f = Make(k);
                f.TopLevel = false;
                f.FormBorderStyle = FormBorderStyle.None;
                f.Dock = DockStyle.Fill;
                body.Controls.Add(f);
                pages[k] = f;
            }
            if (shown != null) shown.Hide();
            shown = f; key = k;
            title.Text = Titles[i];
            header.Current = k;
            f.Show();
            f.BringToFront();
            var check = f as MainForm;
            if (check != null) check.Recheck();          // coming back after installing something: look again
            var update = f as UpdateForm;
            if (update != null) update.Arrived();
        }

        static Form Make(string k)
        {
            switch (k)
            {
                case "setup": return new PrepareForm();
                case "install": return new InstallForm(false);
                case "update": return new UpdateForm();
                case "restore": return new InstallForm(true);
                default: return new MainForm();
            }
        }
    }
}
