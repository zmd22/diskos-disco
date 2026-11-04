// SPDX-License-Identifier: MIT
// diskOS Disco! for Windows - the look: the website's night colours, the CD-rainbow disc and pill buttons.
using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;

namespace DiskosDisco
{
    static class Theme
    {
        public static readonly Color Bg = Color.FromArgb(11, 11, 16), Bg2 = Color.FromArgb(18, 18, 26), Card = Color.FromArgb(24, 24, 34);
        public static readonly Color Text = Color.FromArgb(242, 241, 246), Muted = Color.FromArgb(166, 164, 180), Faint = Color.FromArgb(111, 109, 126);
        public static readonly Color Line = Color.FromArgb(44, 44, 58);
        public static readonly Color Pink = Color.FromArgb(255, 90, 160), Orange = Color.FromArgb(255, 180, 90), Yellow = Color.FromArgb(255, 240, 106);
        public static readonly Color Green = Color.FromArgb(98, 242, 140), Cyan = Color.FromArgb(74, 216, 255), Blue = Color.FromArgb(106, 140, 255);
        public static readonly Color Violet = Color.FromArgb(179, 107, 255), Red = Color.FromArgb(255, 96, 112);

        static readonly Color[] Iri = { Pink, Orange, Yellow, Green, Cyan, Blue, Violet, Pink };

        public static Color Mix(Color a, Color b, float t)
        {
            return Color.FromArgb((int)(a.R + (b.R - a.R) * t), (int)(a.G + (b.G - a.G) * t), (int)(a.B + (b.B - a.B) * t));
        }

        // The rainbow at 0..1 along the iridescent sweep.
        public static Color IriAt(float t)
        {
            t = (t % 1f + 1f) % 1f * (Iri.Length - 1);
            int i = Math.Min((int)t, Iri.Length - 2);
            return Mix(Iri[i], Iri[i + 1], t - i);
        }

        // The disc: an iridescent CD with a dark hub and a soft glow. Drawn once per size.
        public static Bitmap Disc(int size)
        {
            int pad = size / 5, full = size + pad * 2;
            var bmp = new Bitmap(full, full);
            using (var g = Graphics.FromImage(bmp))
            {
                g.SmoothingMode = SmoothingMode.AntiAlias;
                for (int r = pad; r > 0; r -= 2)                                       // glow
                    using (var b = new SolidBrush(Color.FromArgb((int)(36f * (1f - (float)r / pad)), Violet)))
                        g.FillEllipse(b, pad - r, pad - r, size + r * 2, size + r * 2);
                var rect = new RectangleF(pad, pad, size, size);
                for (int a = 0; a < 360; a += 2)                                       // the conic rainbow
                    using (var b = new SolidBrush(IriAt((a + 210) / 360f)))
                        g.FillPie(b, rect.X, rect.Y, rect.Width, rect.Height, a - 90, 2.6f);
                using (var b = new PathGradientBrush(Ellipse(rect)))                    // a lighter centre, like a real CD
                {
                    b.CenterColor = Color.FromArgb(70, 255, 255, 255);
                    b.SurroundColors = new[] { Color.FromArgb(0, 255, 255, 255) };
                    g.FillEllipse(b, rect);
                }
                float hub = size * 0.36f, hole = size * 0.14f;
                using (var b = new SolidBrush(Color.FromArgb(150, Bg))) g.FillEllipse(b, pad + (size - hub) / 2, pad + (size - hub) / 2, hub, hub);
                using (var b = new SolidBrush(Bg)) g.FillEllipse(b, pad + (size - hole) / 2, pad + (size - hole) / 2, hole, hole);
                using (var p = new Pen(Color.FromArgb(60, 255, 255, 255), Math.Max(1f, size / 60f))) g.DrawEllipse(p, rect);
            }
            return bmp;
        }

        static GraphicsPath Ellipse(RectangleF r) { var p = new GraphicsPath(); p.AddEllipse(r); return p; }

        public static GraphicsPath Pill(RectangleF r)
        {
            float d = r.Height;
            var p = new GraphicsPath();
            p.AddArc(r.X, r.Y, d, d, 90, 180);
            p.AddArc(r.Right - d, r.Y, d, d, 270, 180);
            p.CloseFigure();
            return p;
        }

        public static LinearGradientBrush Rainbow(RectangleF r)
        {
            var b = new LinearGradientBrush(new RectangleF(r.X - 1, r.Y, r.Width + 2, r.Height), Pink, Violet, 0f);
            b.InterpolationColors = new ColorBlend
            {
                Colors = new[] { Pink, Orange, Cyan, Violet },
                Positions = new[] { 0f, 0.35f, 0.7f, 1f },
            };
            return b;
        }

        // "diskOS Disco" with the rainbow "!" right after it.
        public static void Wordmark(Graphics g, Font f, float x, float y)
        {
            var tf = StringFormat.GenericTypographic;
            using (var t = new SolidBrush(Text)) g.DrawString("diskOS Disco", f, t, x, y, tf);
            float w = g.MeasureString("diskOS Disco", f, 1000, tf).Width + f.Size * 0.08f;
            var bang = g.MeasureString("!", f, 1000, tf);
            using (var b = Rainbow(new RectangleF(x + w, y, Math.Max(4, bang.Width), bang.Height))) g.DrawString("!", f, b, x + w, y, tf);
        }

        // A form in the Disco style, with the header band on top and the body below.
        public static void Style(Form f)
        {
            f.Text = "diskOS Disco! for Windows";
            f.Font = new Font("Segoe UI", 10f);
            f.BackColor = Bg;
            f.ForeColor = Text;
            f.AutoScaleMode = AutoScaleMode.Dpi;
            try { f.Icon = Icon.FromHandle(Disc(28).GetHicon()); } catch { }
        }

        public static TextBox Log()
        {
            return new TextBox
            {
                Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Vertical, Dock = DockStyle.Fill, WordWrap = false,
                Font = new Font("Consolas", 9f), BackColor = Bg2, ForeColor = Muted, BorderStyle = BorderStyle.None,
                Margin = new Padding(0, 12, 0, 0)
            };
        }
    }

    // A rounded button: the rainbow for the main action, quiet glass for the rest.
    class PillButton : Button
    {
        public bool Primary;
        bool hover, down;

        public PillButton(string text, bool primary)
        {
            Text = text; Primary = primary;
            SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer |
                     ControlStyles.SupportsTransparentBackColor, true);
            BackColor = Color.Transparent;
            Cursor = Cursors.Hand;
            AutoSize = true;
            Margin = new Padding(8, 4, 0, 0);
            Font = new Font("Segoe UI Semibold", 10f);
        }

        public override Size GetPreferredSize(Size proposed)
        {
            var t = TextRenderer.MeasureText(Text, Font);
            return new Size(t.Width + 40, Math.Max(36, t.Height + 16));
        }

        protected override void OnMouseEnter(EventArgs e) { hover = true; Invalidate(); base.OnMouseEnter(e); }
        protected override void OnMouseLeave(EventArgs e) { hover = down = false; Invalidate(); base.OnMouseLeave(e); }
        protected override void OnMouseDown(MouseEventArgs e) { down = true; Invalidate(); base.OnMouseDown(e); }
        protected override void OnMouseUp(MouseEventArgs e) { down = false; Invalidate(); base.OnMouseUp(e); }
        protected override void OnEnabledChanged(EventArgs e) { Cursor = Enabled ? Cursors.Hand : Cursors.Default; Invalidate(); base.OnEnabledChanged(e); }

        protected override void OnPaint(PaintEventArgs e)
        {
            var g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.Clear(Parent != null ? Parent.BackColor : Theme.Bg);
            var r = new RectangleF(1, 1, Width - 3, Height - 3);
            using (var path = Theme.Pill(r))
            {
                Color fore;
                if (!Enabled)
                {
                    using (var b = new SolidBrush(Theme.Card)) g.FillPath(b, path);
                    fore = Theme.Faint;
                }
                else if (Primary)
                {
                    using (var b = Theme.Rainbow(r)) g.FillPath(b, path);
                    if (hover || down) using (var b = new SolidBrush(Color.FromArgb(down ? 50 : 30, 255, 255, 255))) g.FillPath(b, path);
                    fore = Theme.Bg;
                }
                else
                {
                    using (var b = new SolidBrush(down ? Color.FromArgb(44, 44, 60) : hover ? Color.FromArgb(36, 36, 50) : Theme.Card)) g.FillPath(b, path);
                    using (var p = new Pen(hover ? Theme.Faint : Theme.Line)) g.DrawPath(p, path);
                    fore = Theme.Text;
                }
                TextRenderer.DrawText(g, Text, Font, Rectangle.Round(r), fore,
                                      TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.SingleLine);
            }
            if (Focused && ShowFocusCues)
                using (var p = new Pen(Color.FromArgb(120, Theme.Cyan)) { DashStyle = DashStyle.Dot })
                using (var path = Theme.Pill(new RectangleF(3, 3, Width - 7, Height - 7))) g.DrawPath(p, path);
        }
    }

    // The band across the top: disc, name, the tabs, and About. Every tab is always clickable.
    class Header : Control
    {
        readonly Bitmap disc = Theme.Disc(54);
        readonly LinkLabel about;
        readonly RectangleF[] tabs = new RectangleF[Shell.Keys.Length];
        string current;
        int hover = -1;
        public event Action<string> Go;

        public string Current { get { return current; } set { current = value; Invalidate(); } }

        public Header(string current)
        {
            this.current = current;
            SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
            Dock = DockStyle.Top;
            Height = 150;
            about = new LinkLabel
            {
                Text = "About", AutoSize = true, LinkColor = Theme.Muted, ActiveLinkColor = Theme.Cyan, VisitedLinkColor = Theme.Muted,
                LinkBehavior = LinkBehavior.HoverUnderline, BackColor = Color.Transparent, Font = new Font("Segoe UI", 9.5f),
            };
            about.Click += (s, e) => { using (var f = new AboutForm()) f.ShowDialog(FindForm()); };
            Controls.Add(about);
        }

        protected override void OnLayout(LayoutEventArgs e)
        {
            base.OnLayout(e);
            if (about != null) about.Location = new Point(Width - about.Width - 26, 22);   // (layout runs once before it exists)
        }

        int HitTab(Point p)
        {
            for (int i = 0; i < tabs.Length; i++) if (tabs[i].Contains(p)) return i;
            return -1;
        }

        protected override void OnMouseMove(MouseEventArgs e)
        {
            base.OnMouseMove(e);
            int h = HitTab(e.Location);
            Cursor = h >= 0 ? Cursors.Hand : Cursors.Default;
            if (h != hover) { hover = h; Invalidate(); }
        }

        protected override void OnMouseLeave(EventArgs e)
        {
            base.OnMouseLeave(e);
            if (hover != -1) { hover = -1; Invalidate(); }
        }

        protected override void OnMouseClick(MouseEventArgs e)
        {
            base.OnMouseClick(e);
            int h = HitTab(e.Location);
            if (h >= 0 && Go != null) Go(Shell.Keys[h]);
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            var g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.TextRenderingHint = System.Drawing.Text.TextRenderingHint.ClearTypeGridFit;
            using (var b = new LinearGradientBrush(ClientRectangle, Color.FromArgb(26, 16, 44), Theme.Bg, 90f)) g.FillRectangle(b, ClientRectangle);
            using (var b = new LinearGradientBrush(new Rectangle(0, 0, Width + 1, 3), Theme.Pink, Theme.Violet, 0f))   // the rainbow rim
            {
                b.InterpolationColors = new ColorBlend { Colors = new[] { Theme.Pink, Theme.Orange, Theme.Yellow, Theme.Green, Theme.Cyan, Theme.Blue, Theme.Violet },
                                                         Positions = new[] { 0f, .17f, .33f, .5f, .67f, .83f, 1f } };
                g.FillRectangle(b, 0, Height - 3, Width, 3);
            }
            int pad = (disc.Width - 54) / 2;
            g.DrawImage(disc, 24 - pad, 14 - pad);
            using (var f = new Font("Segoe UI", 18f, FontStyle.Bold)) Theme.Wordmark(g, f, 92, 17);
            using (var f = new Font("Segoe UI", 9.5f)) g.DrawString("for Windows", f, new SolidBrush(Theme.Muted), 94, 50);

            // the tabs: pills, the current one lit with its own colour dot
            float x = 24, y = 96, h = 36;
            for (int i = 0; i < Shell.Keys.Length; i++)
            {
                bool on = Shell.Keys[i] == current, hot = i == hover && !on;
                using (var f = new Font("Segoe UI", 10f, on ? FontStyle.Bold : FontStyle.Regular))
                {
                    float w = g.MeasureString(Shell.Names[i], f).Width + 44;
                    var r = new RectangleF(x, y, w, h);
                    tabs[i] = r;
                    using (var path = Theme.Pill(r))
                    using (var b = new SolidBrush(on ? Color.FromArgb(34, 34, 47) : hot ? Color.FromArgb(28, 28, 39) : Theme.Bg2))
                        g.FillPath(b, path);
                    using (var b = new SolidBrush(on ? Theme.IriAt(i / (float)Shell.Keys.Length) : Theme.Faint))
                        g.FillEllipse(b, x + 15, y + h / 2 - 3.5f, 7, 7);
                    var sz = g.MeasureString(Shell.Names[i], f);
                    g.DrawString(Shell.Names[i], f, new SolidBrush(on ? Theme.Text : hot ? Theme.Text : Theme.Muted), x + 28, y + (h - sz.Height) / 2);
                    x += w + 8;
                }
            }
        }
    }
}
