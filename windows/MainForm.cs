// SPDX-License-Identifier: MIT
// diskOS Disco! for Windows - the window. Step 1 shows the checks; later steps join as wizard pages.
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Windows.Forms;

namespace DiskosDisco
{
    static class Program
    {
        public const string Version = "2.0.0";
        public static string ShortVersion { get { return "version " + Version; } }

        [DllImport("user32.dll")] static extern bool SetProcessDPIAware();

        [STAThread]
        static void Main(string[] args)
        {
            try { SetProcessDPIAware(); } catch { }
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            // --prepare-page, --install-page, --about: straight there (for testing and screenshots)
            if (Array.IndexOf(args, "--disclaimer") >= 0) { Application.Run(new Disclaimer()); return; }
            if (args.Length == 0)
            {
                using (var d = new Disclaimer())
                    if (d.ShowDialog() != DialogResult.OK) return;
            }
            // one window with tabs; --prepare-page / --install-page / --update-page / --restore-page open that tab
            string start = "check";
            if (Array.IndexOf(args, "--prepare-page") >= 0) start = "setup";
            else if (Array.IndexOf(args, "--install-page") >= 0) start = "install";
            else if (Array.IndexOf(args, "--update-page") >= 0) start = "update";
            else if (Array.IndexOf(args, "--restore-page") >= 0) start = "restore";
            if (Array.IndexOf(args, "--about") >= 0) { Application.Run(new AboutForm()); return; }
            Application.Run(new Shell(start));
        }
    }

    class Row
    {
        public Check Check;
        public Label Icon, Title, Detail;
        public Button Action;
        public CheckResult Result;
    }

    class MainForm : Form, IPage
    {
        public bool Busy { get { return false; } }     // checks can be left at any time
        public void LetGo() { }
        bool started;
        // The shell shows this tab: check now (first visit), or again if something was missing (back from installing it).
        internal void Recheck()
        {
            if (running != 0) return;
            if (!started || rows.Exists(r => r.Result != null && r.Result.State == State.Missing)) { started = true; RunChecks(); }
        }
        internal static readonly Color Ink = Theme.Text, Muted = Theme.Muted;
        internal static readonly Color Good = Theme.Green, Bad = Theme.Red, Warn = Theme.Orange, Accent = Theme.Cyan;

        readonly List<Row> rows = new List<Row>();
        readonly Button recheck, next, copy;
        readonly Label summary;
        readonly TableLayoutPanel table;
        int running;

        public MainForm()
        {
            Theme.Style(this);
            StartPosition = FormStartPosition.CenterScreen;
            ClientSize = new Size(800, 760);
            MinimumSize = new Size(700, 600);

            var root = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(28, 22, 28, 18), ColumnCount = 1, RowCount = 4 };
            root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            Controls.Add(root); root.BringToFront();

            var head = new FlowLayoutPanel { FlowDirection = FlowDirection.TopDown, AutoSize = true, WrapContents = false, Dock = DockStyle.Fill };
            head.Controls.Add(new Label
            {
                Text = "diskOS needs a few free Microsoft and open-source tools. This page checks for them and shows what to click " +
                       "for anything missing. Nothing on your PC is changed until you click a button.",
                ForeColor = Muted, MaximumSize = new Size(690, 0), AutoSize = true, Margin = new Padding(0, 0, 0, 12)
            });
            root.Controls.Add(head, 0, 0);

            table = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 3, AutoScroll = true };
            table.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 34));
            table.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
            table.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
            table.Resize += (s, e) => FitDetails();
            root.Controls.Add(table, 0, 1);

            Check[] checks = { new WindowsCheck(), new VirtualizationCheck(), new WslCheck(), new UsbBridgeCheck(), new SpaceCheck(), new RestartCheck() };
            foreach (var c in checks) AddRow(c);

            summary = new Label { AutoSize = true, ForeColor = Ink, Margin = new Padding(0, 10, 0, 10), Font = new Font(Font, FontStyle.Bold) };
            root.Controls.Add(summary, 0, 2);

            var bar = new FlowLayoutPanel { FlowDirection = FlowDirection.RightToLeft, Dock = DockStyle.Fill, AutoSize = true };
            next = MakeButton("Next", true);
            next.Click += (s, e) => Shell.Current.Go("setup");
            recheck = MakeButton("Check again", false);
            recheck.Click += (s, e) => RunChecks();
            copy = MakeButton("Copy report", false);
            copy.Click += (s, e) => { try { Clipboard.SetText(Report()); summary.Text = "Report copied. Paste it into a message or GitHub issue."; } catch { } };
            bar.Controls.Add(next); bar.Controls.Add(recheck); bar.Controls.Add(copy);
            bar.Controls.Add(new Label { Text = "Version " + Program.Version, ForeColor = Muted, AutoSize = true, Margin = new Padding(0, 12, 24, 0) });
            root.Controls.Add(bar, 0, 3);

            Activated += (s, e) => { if (running == 0 && rows.Exists(r => r.Result != null && r.Result.State == State.Missing)) RunChecks(); };   // back from an installer
        }

        internal static Button MakeButton(string text, bool primary)
        {
            return new PillButton(text, primary);
        }

        static Button OldButton(string text, bool primary)
        {
            var b = new Button { Text = text, AutoSize = true, Padding = new Padding(14, 4, 14, 4), Margin = new Padding(8, 4, 0, 0), FlatStyle = FlatStyle.System };
            if (primary)
            {
                b.FlatStyle = FlatStyle.Flat; b.FlatAppearance.BorderSize = 0; b.ForeColor = Color.White;
                b.EnabledChanged += (s, e) => b.BackColor = b.Enabled ? Accent : Color.FromArgb(200, 200, 206);
                b.BackColor = Accent;
            }
            return b;
        }

        void AddRow(Check c)
        {
            var r = new Row { Check = c };
            r.Icon = new Label { Text = "…", AutoSize = false, Size = new Size(30, 30), Font = new Font("Segoe UI Symbol", 13f), TextAlign = ContentAlignment.TopCenter, Margin = new Padding(0, 6, 0, 0) };
            var text = new FlowLayoutPanel { FlowDirection = FlowDirection.TopDown, WrapContents = false, AutoSize = true, Margin = new Padding(4, 6, 8, 10) };
            r.Title = new Label { Text = c.Title, AutoSize = true, ForeColor = Ink, Font = new Font(Font, FontStyle.Bold) };
            r.Detail = new Label { Text = "Checking…", AutoSize = true, ForeColor = Muted, MaximumSize = new Size(470, 0) };
            text.Controls.Add(r.Title); text.Controls.Add(r.Detail);
            r.Action = MakeButton("", false);
            r.Action.Visible = false; r.Action.Margin = new Padding(8, 8, 0, 0);
            r.Action.Click += (s, e) => { if (r.Result != null && r.Result.Action != null) r.Result.Action(); };
            int i = table.RowCount++;
            table.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            table.Controls.Add(r.Icon, 0, i); table.Controls.Add(text, 1, i); table.Controls.Add(r.Action, 2, i);
            rows.Add(r);
        }

        void FitDetails()
        {
            int w = Math.Max(240, table.ClientSize.Width - 34 - 200);
            foreach (var r in rows) r.Detail.MaximumSize = new Size(w, 0);
        }

        void RunChecks()
        {
            if (running > 0) return;
            running = rows.Count;
            next.Enabled = recheck.Enabled = false;
            summary.Text = "Checking…";
            foreach (var r in rows)
            {
                r.Result = null; Show(r, new CheckResult(State.Checking, "Checking…"));
                var row = r;
                new Thread(() =>
                {
                    CheckResult res;
                    try { res = Demo.On ? Demo.For(row.Check) : row.Check.Run(); }
                    catch (Exception e) { res = new CheckResult(State.Warning, "Couldn't check this: " + e.Message); }
                    BeginInvoke(new Action(() => { row.Result = res; Show(row, res); if (--running == 0) Done(); }));
                }) { IsBackground = true }.Start();
            }
        }

        void Show(Row r, CheckResult res)
        {
            switch (res.State)
            {
                case State.Ok: r.Icon.Text = "✔"; r.Icon.ForeColor = Good; break;
                case State.Missing: r.Icon.Text = "✖"; r.Icon.ForeColor = Bad; break;
                case State.Warning: r.Icon.Text = "!"; r.Icon.ForeColor = Warn; break;
                default: r.Icon.Text = "…"; r.Icon.ForeColor = Muted; break;
            }
            r.Detail.Text = res.Detail;
            r.Detail.ForeColor = res.State == State.Missing ? Ink : Muted;
            r.Action.Visible = res.ActionText != null;
            if (res.ActionText != null) r.Action.Text = res.ActionText;
        }

        void Done()
        {
            int missing = rows.FindAll(r => r.Result.State == State.Missing).Count;
            recheck.Enabled = true;
            next.Enabled = missing == 0;
            summary.ForeColor = missing == 0 ? Good : Ink;
            summary.Text = missing == 0 ? "All set. Click Next."
                         : missing == 1 ? "One thing to sort out. Use its button, then click Check again."
                         : missing + " things to sort out. Work from the top; each has a button.";
        }

        string Report()
        {
            var sb = new StringBuilder("diskOS Disco! for Windows " + Program.Version + "\r\n");
            foreach (var r in rows)
                sb.Append(r.Result == null ? "?" : r.Result.State.ToString()).Append(" | ").Append(r.Check.Title)
                  .Append(" | ").Append(r.Result == null ? "" : r.Result.Detail).Append("\r\n");
            return sb.ToString();
        }
    }
}
