# SPDX-License-Identifier: MIT
# Copyright (c) 2026 diskOS contributors
"""diskOS Disco! for Linux - the graphical installer (the Windows app's pages, without a console).

Tabs, each usable on its own at any time:
  Check    - what this PC needs (USB access for flashing, tools, installer files), with one-click fixes
  Install  - firmware .zip + interface + update keys (new or existing) -> flash
  Update   - an mq_ui + your keys -> signed diskos-update folder on the Disc's SD card (or a folder to copy by hand)
  Restore  - back to FiiO's stock firmware

Threading as before: all Tk work on the main thread; one worker thread runs the engine and reports through a
queue; a blocking confirm gate before anything is written to the Disc; the window can't close while flashing.
Anti-aliased artwork (pill buttons, the disc) is rendered in pure Python into PNGs, cached - no extra packages."""

import base64
import colorsys
import math
import os
import queue
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import time
import zlib

from diskos_installer import __version__, bundle, owner, platform_probe, service, state
from diskos_installer.reporter import QueueReporter

# ---- theme (the Windows app's palette) -------------------------------------------------------------------------------
BG, BG2, CARD = "#0b0b10", "#12121a", "#181822"
TEXT, MUTED, FAINT, LINE = "#f2f1f6", "#a6a4b4", "#6f6d7e", "#2c2c3a"
PINK, ORANGE, YELLOW, GREEN = "#ff5aa0", "#ffb45a", "#fff06a", "#62f28c"
CYAN, BLUE, VIOLET, RED = "#4ad8ff", "#6a8cff", "#b36bff", "#ff6070"
IRI = [PINK, ORANGE, YELLOW, GREEN, CYAN, BLUE, VIOLET, PINK]
APP = "diskOS Disco!"
WELCOME_SEEN = os.path.join(os.environ.get("XDG_CONFIG_HOME") or os.path.expanduser("~/.config"), "diskos-disco",
                            "welcome-seen")
UDEV_RULE = "70-diskos-maskrom.rules"


def _rgb(h):
    h = h.lstrip("#")
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def _hex(c):
    return "#%02x%02x%02x" % tuple(max(0, min(255, int(round(v)))) for v in c)


def _mix(a, b, t):
    return tuple(x + (y - x) * t for x, y in zip(a, b))


def iri_at(t):
    """The iridescent ramp, t in [0, 1)."""
    t = (t % 1.0) * (len(IRI) - 1)
    i = int(t)
    return _mix(_rgb(IRI[i]), _rgb(IRI[min(i + 1, len(IRI) - 1)]), t - i)


# ---- tiny anti-aliased PNG renderer ----------------------------------------------------------------------------------
def png_bytes(w, h, px):
    """px: list of rows, each a list of (r, g, b, a) 0..255."""
    raw = b"".join(b"\x00" + bytes(v for p in row for v in p) for row in px)
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def render(w, h, shade, ss=3):
    """shade(x, y) -> (r, g, b, a) or None at sub-pixel centres; averaged ss x ss per pixel (premultiplied)."""
    rows, n = [], ss * ss
    for y in range(h):
        row = []
        for x in range(w):
            r = g = b = a = 0.0
            for j in range(ss):
                for i in range(ss):
                    c = shade(x + (i + 0.5) / ss, y + (j + 0.5) / ss)
                    if c:
                        al = c[3] / 255.0
                        r += c[0] * al; g += c[1] * al; b += c[2] * al; a += al
            if a:
                row.append((int(r / a), int(g / a), int(b / a), int(255 * a / n)))
            else:
                row.append((0, 0, 0, 0))
        rows.append(row)
    return png_bytes(w, h, rows)


def pill_png(w, h, kind):
    """kind: 'primary' (iridescent), 'primary-off', 'ghost', 'ghost-hover', 'ghost-off', 'tab', 'tab-on'."""
    r = h / 2.0
    fills = {"ghost": _rgb("#1f1f2b"), "ghost-hover": _rgb("#2a2a3a"), "ghost-off": _rgb("#15151d"),
             "primary-off": _rgb("#23232f"), "tab": _rgb(BG2), "tab-on": _rgb("#22222f")}
    border = {"ghost": _rgb("#34344a"), "ghost-hover": _rgb("#4a4a66"), "ghost-off": _rgb("#22222e"),
              "primary-off": _rgb("#2c2c3a"), "tab": None, "tab-on": None}

    def shade(x, y):
        cx = min(max(x, r), w - r)
        d = math.hypot(x - cx, y - r)
        if d > r:
            return None
        if kind in ("primary", "primary-hover"):
            c = iri_at(0.02 + 0.55 * x / w)
            if kind == "primary-hover":
                c = _mix(c, (255, 255, 255), 0.12)
            c = _mix(c, (255, 255, 255), 0.10 * (1 - y / h))          # soft top light
            return (*c, 255)
        b = border.get(kind)
        if b and d > r - 1.2:
            return (*b, 255)
        return (*fills[kind], 255)
    return render(w, h, shade)


def disc_png(size):
    """The app's disc: an iridescent record with a dark hub and a soft glow (as the Windows app draws it)."""
    c = size / 2.0
    R = size * 0.46

    def shade(x, y):
        dx, dy = x - c, y - c
        d = math.hypot(dx, dy)
        if d > R:
            if d < size / 2.0:                                          # glow
                a = 70 * (1 - (d - R) / (size / 2.0 - R)) ** 2
                return (*_rgb(VIOLET), a)
            return None
        ang = (math.atan2(dy, dx) / (2 * math.pi)) % 1.0
        col = iri_at(ang)
        groove = 0.06 * math.sin(d * 1.9)                                # grooves
        col = _mix(col, (0, 0, 0), 0.18 + groove)
        sheen = max(0.0, math.cos((ang - 0.12) * 4 * math.pi)) * 0.22   # light catching the vinyl
        col = _mix(col, (255, 255, 255), sheen)
        if d < R * 0.30:
            col = _mix(_rgb("#14141c"), (0, 0, 0), 0.2)                 # label/hub
            if d < R * 0.08:
                col = _rgb(BG)
        elif d > R - 1.0:
            col = _mix(col, (255, 255, 255), 0.25)
        return (*col, 255)
    return render(size, size, shade, ss=4)


def radio_png(size, on):
    """A radio mark: a soft ring; selected = iridescent dot inside."""
    c, R = size / 2.0, size / 2.0 - 1

    def shade(x, y):
        d = math.hypot(x - c, y - c)
        if d > R:
            return None
        if d > R - 1.6:
            return (*(_rgb(PINK) if on else _rgb("#4a4a62")), 255)
        if on and d < R * 0.5:
            return (*iri_at(0.02 + 0.5 * (x / size)), 255)
        return (*_rgb(CARD), 255)
    return render(size, size, shade)


def rule_png(w, h=2):
    return png_bytes(w, h, [[(*map(int, iri_at(x / w)), 255) for x in range(w)] for _ in range(h)])


# ---- widgets ---------------------------------------------------------------------------------------------------------
class Images:
    """PhotoImages cached by key (Tk drops images without a Python reference)."""
    def __init__(self, tk):
        self.tk, self.cache = tk, {}

    def get(self, key, make):
        if key not in self.cache:
            self.cache[key] = self.tk.PhotoImage(data=base64.b64encode(make()).decode("ascii"), format="png")
        return self.cache[key]


class Pill:
    """A rounded, anti-aliased button: primary (iridescent) or ghost. Canvas-based; keyboard focus + Return."""
    def __init__(self, app, parent, text, command, primary=False, width=None, bg=BG):
        tk = app.tk
        self.app, self.text, self.command, self.primary, self.enabled = app, text, command, primary, True
        f = app.f_btn
        self.w = width or max(120, f.measure(text) + 56)
        self.h = 48
        self.cv = tk.Canvas(parent, width=self.w, height=self.h, bg=bg, highlightthickness=0, bd=0,
                            cursor="hand2", takefocus=1)
        self.img = self.cv.create_image(0, 0, anchor="nw")
        self.lbl = self.cv.create_text(self.w / 2, self.h / 2, text=text, font=f)
        self.hover = False
        for ev, fn in (("<Enter>", lambda e: self._hov(True)), ("<Leave>", lambda e: self._hov(False)),
                       ("<ButtonRelease-1>", self._click), ("<Return>", self._click), ("<space>", self._click)):
            self.cv.bind(ev, fn)
        self._paint()

    def _kind(self):
        if self.primary:
            return "primary" + ("-hover" if self.hover else "") if self.enabled else "primary-off"
        return "ghost" + ("-hover" if self.hover else "") if self.enabled else "ghost-off"

    def _paint(self):
        k = self._kind()
        self.cv.itemconfigure(self.img, image=self.app.img.get(("pill", self.w, self.h, k),
                                                                  lambda: pill_png(self.w, self.h, k)))
        fg = ("#16121c" if self.primary else TEXT) if self.enabled else FAINT
        self.cv.itemconfigure(self.lbl, text=self.text, fill=fg)
        self.cv.configure(cursor="hand2" if self.enabled else "arrow")

    def _hov(self, on):
        self.hover = on
        self._paint()

    def _click(self, e=None):
        if self.enabled and self.command:
            self.command()

    def set(self, enabled=None, text=None):
        if enabled is not None:
            self.enabled = bool(enabled)
        if text is not None:
            self.text = text
        self._paint()

    def pack(self, **kw):
        self.cv.pack(**kw)
        return self

    def grid(self, **kw):
        self.cv.grid(**kw)
        return self


# ---- the app ---------------------------------------------------------------------------------------------------------
TABS = (("check", "Check"), ("install", "Install"), ("update", "Update"), ("restore", "Restore"))
TAB_TITLES = {"check": "Check this PC", "install": "Install on your Disc", "update": "Update from the SD card",
              "restore": "Back to stock"}


class App:
    def __init__(self, root):
        import tkinter as tk
        import tkinter.font as tkfont
        self.tk, self.root = tk, root
        self.img = Images(tk)
        self.q = queue.Queue()
        self.reporter = QueueReporter(self.q)
        self.busy = False
        self.flashing = False
        self.job = None
        self.t0 = None
        self.mode = ""
        self.keys_made = None
        self._confirm_event = threading.Event()
        self._confirm_result = False

        fam = next((f for f in ("Inter", "Inter Display", "Noto Sans", "Cantarell", "DejaVu Sans")
                    if f in tkfont.families(root)), "TkDefaultFont")
        mono = next((f for f in ("JetBrains Mono", "Noto Sans Mono", "DejaVu Sans Mono")
                     if f in tkfont.families(root)), "TkFixedFont")
        F = lambda px, w="normal": tkfont.Font(root=root, family=fam, size=-px, weight=w)   # pixels, not points
        self.f_title, self.f_h, self.f_b, self.f_s = F(31, "bold"), F(19, "bold"), F(17), F(15)
        self.f_btn, self.f_tab, self.f_tab_on, self.f_page = F(17, "bold"), F(17), F(17, "bold"), F(22)
        self.f_mono = tkfont.Font(root=root, family=mono, size=-15)

        root.title(APP + " for Linux")
        root.configure(bg=BG)
        root.geometry("1080x1060")
        root.minsize(1000, 960)
        root.protocol("WM_DELETE_WINDOW", self._on_close)
        try:
            root.iconphoto(True, self.img.get(("disc", 64), lambda: disc_png(64)))
        except tk.TclError:
            pass
        self._style()

        # paths chosen by the user
        self.fw = None
        self.ui_file = None
        self.keys_new = tk.BooleanVar(value=not owner.keys_status(owner.default_keys_dir())[0])
        self.keys_dir = owner.default_keys_dir() if os.path.isdir(owner.default_keys_dir()) else None
        self.ui_builtin = tk.BooleanVar(value=True)
        self.up_ui_builtin = tk.BooleanVar(value=True)
        self.up_ui_file = None
        self.restore_fw = None
        self.cards = []

        self._header()
        self.body = tk.Frame(root, bg=BG)
        self.body.pack(fill="x", padx=36)
        self.page_title = tk.Label(self.body, text="", bg=BG, fg=TEXT, font=self.f_page, anchor="w")
        self.page_title.pack(fill="x", pady=(14, 0))
        self.pages = {}
        for key, _ in TABS:
            self.pages[key] = tk.Frame(self.body, bg=BG)
        self._page_check()
        self._page_install()
        self._page_update()
        self._page_restore()
        self._footer()
        self.show("check" if not self._checks_ok_quick() else "install")
        root.after(80, self._drain)
        if not os.path.exists(WELCOME_SEEN):
            root.after(300, self._welcome)
        root.after(3000, self._tick)

    # ---- style ---------------------------------------------------------------------------------------------------
    def _style(self):
        from tkinter import ttk
        s = ttk.Style(self.root)
        s.theme_use("clam")
        s.configure("Iri.Horizontal.TProgressbar", troughcolor=BG2, background=PINK, bordercolor=BG2,
                    lightcolor=PINK, darkcolor=PINK, thickness=8)
        s.configure("Dark.Vertical.TScrollbar", troughcolor=BG2, background="#2a2a38", bordercolor=BG2,
                    arrowcolor=MUTED, lightcolor="#2a2a38", darkcolor="#2a2a38", gripcount=0)
        s.map("Dark.Vertical.TScrollbar", background=[("active", "#3a3a4e")])
        s.configure("Dark.TCombobox", fieldbackground=CARD, background=CARD, foreground=TEXT, arrowcolor=MUTED,
                    bordercolor=LINE, lightcolor=CARD, darkcolor=CARD, selectbackground=CARD, selectforeground=TEXT)
        s.map("Dark.TCombobox", fieldbackground=[("readonly", CARD)], foreground=[("readonly", TEXT)])
        self.root.option_add("*TCombobox*Listbox.background", CARD)
        self.root.option_add("*TCombobox*Listbox.foreground", TEXT)
        self.root.option_add("*TCombobox*Listbox.selectBackground", "#2e2e40")

    # ---- header: logo, title, tabs, iridescent rule ----------------------------------------------------------------
    def _header(self):
        tk = self.tk
        head = tk.Canvas(self.root, height=186, bg=BG, highlightthickness=0, bd=0)
        head.pack(fill="x")
        self.head = head

        def draw(e=None):
            w = max(head.winfo_width(), 2)
            head.delete("all")
            top, bot = _rgb("#1a1028"), _rgb(BG)                       # violet dusk fading into the page
            for y in range(0, 186, 2):
                head.create_rectangle(0, y, w, y + 2, outline="", fill=_hex(_mix(top, bot, y / 186)))
            head.create_image(32, 22, anchor="nw", image=self.img.get(("disc", 74), lambda: disc_png(74)))
            t = head.create_text(124, 28, anchor="nw", text="diskOS Disco", font=self.f_title, fill=TEXT)
            x2, y2 = head.bbox(t)[2], head.bbox(t)[3]
            head.create_text(x2, 28, anchor="nw", text="!", font=self.f_title, fill=YELLOW)
            head.create_text(125, y2 + 1, anchor="nw", text="for Linux", font=self.f_s, fill=MUTED)
            about = head.create_text(w - 34, 30, anchor="ne", text="About", font=self.f_b, fill=MUTED)
            head.tag_bind(about, "<Button-1>", lambda e: self._about())
            head.tag_bind(about, "<Enter>", lambda e: head.itemconfigure(about, fill=TEXT))
            head.tag_bind(about, "<Leave>", lambda e: head.itemconfigure(about, fill=MUTED))
            # tabs: pills, the current one lit with its own iridescent dot
            x = 32
            for i, (key, name) in enumerate(TABS):
                on = key == self.current
                f = self.f_tab_on if on else self.f_tab
                tw = f.measure(name) + 54
                img = self.img.get(("pill", tw, 44, "tab-on" if on else "tab"),
                                   lambda tw=tw, on=on: pill_png(tw, 44, "tab-on" if on else "tab"))
                bgi = head.create_image(x, 120, anchor="nw", image=img)
                dot = head.create_oval(x + 18, 138, x + 26, 146, outline="",
                                       fill=_hex(iri_at(i / len(TABS))) if on else FAINT)
                lab = head.create_text(x + 35, 142, anchor="w", text=name, font=f, fill=TEXT if on else MUTED)
                for item in (bgi, dot, lab):
                    head.tag_bind(item, "<Button-1>", lambda e, k=key: self.show(k))
                    head.tag_bind(item, "<Enter>", lambda e: head.configure(cursor="hand2"))
                    head.tag_bind(item, "<Leave>", lambda e: head.configure(cursor=""))
                x += tw + 10
            head.create_image(0, 183, anchor="nw", image=self.img.get(("rule", w, 3), lambda: rule_png(w, 3)))
        self.current = "check"
        self._draw_head = draw
        head.bind("<Configure>", draw)

    # ---- shared building blocks ------------------------------------------------------------------------------------
    def _intro(self, page, text):
        self.tk.Label(page, text=text, bg=BG, fg=MUTED, font=self.f_b, justify="left", anchor="w",
                      wraplength=990).pack(fill="x", pady=(8, 0))

    def _step(self, page, n, title, button=None, command=None):
        """A numbered section: title (+ an optional pill on the right). Returns (frame, status label, pill)."""
        tk = self.tk
        f = tk.Frame(page, bg=BG)
        f.pack(fill="x", pady=(18, 0))
        f.columnconfigure(0, weight=1)
        tk.Label(f, text=f"{n}.  {title}", bg=BG, fg=TEXT, font=self.f_h, anchor="w").grid(row=0, column=0, sticky="w")
        pill = Pill(self, f, button, command).grid(row=0, column=1, rowspan=3, sticky="e") if button else None
        st = tk.Label(f, text="", bg=BG, fg=MUTED, font=self.f_b, anchor="w", justify="left", wraplength=780)
        st.grid(row=2, column=0, sticky="w", pady=(3, 0))
        f.grid_columnconfigure(1, minsize=190)
        return f, st, pill

    def _radio(self, parent, text, var, value, command):
        """A drawn radio button (Tk's own indicator ignores the dark theme): image + label, click anywhere."""
        on_img = self.img.get(("radio", 1), lambda: radio_png(20, True))
        off_img = self.img.get(("radio", 0), lambda: radio_png(20, False))
        lb = self.tk.Label(parent, text="  " + text, image=off_img, compound="left", bg=BG, fg=TEXT, font=self.f_b,
                           cursor="hand2", anchor="w", pady=2)

        def paint(*_):
            on = var.get() == value
            lb.configure(image=on_img if on else off_img, fg=TEXT if on else MUTED)

        def click(e):
            if not self.busy:
                var.set(value)
                command()
        lb.bind("<Button-1>", click)
        var.trace_add("write", paint)
        paint()
        return lb

    def _say(self, label, text, kind="muted"):
        col = {"ok": GREEN, "bad": RED, "warn": ORANGE, "muted": MUTED, "text": TEXT}[kind]
        mark = {"ok": "✔  ", "bad": "✖  ", "warn": "!  "}.get(kind, "")
        label.configure(text=mark + text, fg=col)

    # ---- Check ---------------------------------------------------------------------------------------------------------
    CHECKS = (("usb", "USB access to the Disc"), ("tools", "Image tools (squashfs, usbboot)"),
              ("openssl", "OpenSSL (makes and uses your update keys)"), ("files", "Installer files"))

    def _page_check(self):
        tk, p = self.tk, self.pages["check"]
        self._intro(p, "diskOS needs a few things on this PC. Everything below runs as your normal user; only the "
                       "one-time USB permission asks for your password.")
        self.check_rows = {}
        for key, title in self.CHECKS:
            row = tk.Frame(p, bg=CARD, highlightthickness=1, highlightbackground=LINE)
            row.pack(fill="x", pady=(10, 0), ipady=8)
            row.columnconfigure(1, weight=1)
            icon = tk.Label(row, text="…", bg=CARD, fg=MUTED, font=self.f_title, width=2)
            icon.grid(row=0, column=0, rowspan=2, padx=(12, 6))
            tk.Label(row, text=title, bg=CARD, fg=TEXT, font=self.f_h, anchor="w").grid(row=0, column=1, sticky="w")
            det = tk.Label(row, text="Checking…", bg=CARD, fg=MUTED, font=self.f_b, anchor="w", justify="left",
                           wraplength=820)
            det.grid(row=1, column=1, sticky="w")
            act = Pill(self, row, "Fix", None, bg=CARD)
            act.grid(row=0, column=2, rowspan=2, padx=12)
            act.cv.grid_remove()
            self.check_rows[key] = (icon, det, act)
        bar = tk.Frame(p, bg=BG)
        bar.pack(fill="x", pady=(16, 0))
        self.check_summary = tk.Label(bar, text="", bg=BG, fg=TEXT, font=self.f_h, anchor="w")
        self.check_summary.pack(side="left")
        Pill(self, bar, "Check again", self.run_checks).pack(side="right")

    def _check_results(self):
        res = {}
        acc, detail = platform_probe.maskrom_access()
        rule = any(os.path.exists(os.path.join(d, UDEV_RULE)) for d in ("/etc/udev/rules.d", "/usr/lib/udev/rules.d",
                                                                         "/lib/udev/rules.d"))
        if acc == "denied":
            res["usb"] = ("bad", "The Disc is connected but this user can't open it. Allow USB access once.", "Allow")
        elif rule:
            res["usb"] = ("ok", "Allowed for your user (udev rule installed).", None)
        else:
            res["usb"] = ("warn", "Not set up yet: flashing needs a one-time USB permission (asks for your password).",
                          "Allow")
        fix = []
        if not (bundle.native("mksquashfs", required=False) and bundle.native("unsquashfs", required=False)):
            fix.append("squashfs-tools are missing: install them with your package manager "
                       "(e.g. sudo dnf install squashfs-tools).")
        if not bundle.native("usbboot", required=False):
            fix.append("usbboot isn't built yet: run ./install.sh once (or bash build/build-usbboot-static.sh).")
        res["tools"] = ("ok", "Found.", None) if not fix else ("bad", " ".join(fix), None)
        res["openssl"] = (("ok", "Found.", None) if shutil.which("openssl") else
                          ("bad", "Not found. Install openssl with your package manager.", None))
        miss = [n for n in ("mq_ui", "S97diskos_install") if not bundle.data(n, required=False)]
        try:
            owner.tool("diskos-keys.sh"); owner.tool("diskos-sign.sh")
        except FileNotFoundError:
            miss.append("owner key tools")
        res["files"] = (("ok", "Complete (interface " + self._builtin_version() + ").", None) if not miss else
                        ("bad", "Missing: " + ", ".join(miss) + ". Download the installer again.", None))
        return res

    def run_checks(self):
        for key, _ in self.CHECKS:
            icon, det, act = self.check_rows[key]
            icon.configure(text="…", fg=MUTED); det.configure(text="Checking…", fg=MUTED)
        def work():
            try:
                r = self._check_results()
            except Exception as e:  # never leave the page spinning
                r = {k: ("warn", f"Couldn't check: {e}", None) for k, _ in self.CHECKS}
            self.q.put(("checks", r))
        threading.Thread(target=work, daemon=True).start()

    def _show_checks(self, r):
        bad = 0
        for key, _ in self.CHECKS:
            icon, det, act = self.check_rows[key]
            kind, text, action = r[key]
            icon.configure(text={"ok": "✔", "bad": "✖", "warn": "!"}[kind],
                           fg={"ok": GREEN, "bad": RED, "warn": ORANGE}[kind])
            det.configure(text=text, fg=MUTED)
            if action:
                act.set(enabled=True, text=action); act.command = self.allow_usb; act.cv.grid()
            else:
                act.cv.grid_remove()
            bad += kind == "bad"
        self.check_summary.configure(text="All set." if bad == 0 else f"{bad} thing(s) to fix before installing.",
                                     fg=GREEN if bad == 0 else ORANGE)

    def _checks_ok_quick(self):
        return bool(shutil.which("openssl")) and bool(bundle.data("mq_ui", required=False))

    def allow_usb(self):
        """Install the udev rule with pkexec (a graphical password prompt), then reload udev."""
        src = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "udev", UDEV_RULE)
        if not os.path.isfile(src) or not shutil.which("pkexec"):
            self._msg("USB access", "Run this once in a terminal, then unplug and replug the Disc:\n\n"
                      f"sudo cp '{src}' /etc/udev/rules.d/ && sudo udevadm control --reload-rules && "
                      "sudo udevadm trigger")
            return
        cmd = (f"install -m 0644 '{src}' /etc/udev/rules.d/{UDEV_RULE} && udevadm control --reload-rules && "
               "udevadm trigger --subsystem-match=usb")
        def work():
            rc = subprocess.call(["pkexec", "sh", "-c", cmd])
            self.q.put(("usb-allowed", rc))
        threading.Thread(target=work, daemon=True).start()

    # ---- Install -------------------------------------------------------------------------------------------------------
    def _page_install(self):
        tk, p = self.tk, self.pages["install"]
        _, self.fw_st, _ = self._step(p, 1, "Firmware", "Choose…", self._pick_fw)
        f, self.ui_st, self.ui_pick = self._step(p, 2, "Interface", "Choose…", self._pick_ui)
        rr = tk.Frame(f, bg=BG); rr.grid(row=1, column=0, sticky="w", pady=(4, 0))
        self._radio(rr, "Built into this installer", self.ui_builtin, True, self._refresh).pack(anchor="w")
        self._radio(rr, "An mq_ui file on this PC", self.ui_builtin, False, self._refresh).pack(anchor="w")
        f, self.keys_st, self.keys_pick = self._step(p, 3, "Update keys", "Choose…", self._pick_keys)
        rr = tk.Frame(f, bg=BG); rr.grid(row=1, column=0, sticky="w", pady=(4, 0))
        self._radio(rr, "Create new update keys (first install)", self.keys_new, True, self._refresh).pack(anchor="w")
        self._radio(rr, "Use my existing keys folder", self.keys_new, False, self._refresh).pack(anchor="w")
        _, self.dev_st, _ = self._step(p, 4, "Connect the Disc", "Look again", self._refresh_device)
        self.install_btn = self._action_row(p, "Install", self.do_install)

    def _builtin_version(self):
        meta = bundle.data("mq_ui.build.json", required=False)
        try:
            import json
            with open(meta) as fh:
                return "v" + json.load(fh)["version"]
        except Exception:
            return "bundled"

    def _pick_fw(self):
        from tkinter import filedialog
        p = filedialog.askopenfilename(parent=self.root, title="FiiO's firmware (.zip)",
                                       filetypes=[("Firmware", "*.zip"), ("All files", "*")])
        if p:
            self.fw = p
            self._refresh()

    def _pick_ui(self):
        from tkinter import filedialog
        p = filedialog.askopenfilename(parent=self.root, title="Choose mq_ui")
        if p:
            self.ui_file = p
            self.ui_builtin.set(False)
            self._refresh()

    def _pick_keys(self):
        from tkinter import filedialog
        p = filedialog.askdirectory(parent=self.root, title="Your diskOS keys folder",
                                    initialdir=self.keys_dir or os.path.expanduser("~"))
        if p:
            self.keys_dir = p
            self.keys_new.set(False)
            self._refresh()

    def _device_state(self):
        n = platform_probe.maskrom_count()
        if n == 1:
            acc, _ = platform_probe.maskrom_access()
            if acc == "denied":
                return "bad", "Disc found, but this user can't open it. Allow USB access on the Check tab."
            return "ok", "Disc connected and ready."
        if n > 1:
            return "bad", "More than one device in flash mode. Connect only the Disc."
        if n < 0:
            return "warn", "Can't see USB devices (no pyusb or lsusb). Run ./install.sh once."
        return "muted", "Power the Disc off, hold Volume Down and plug in USB. It's found automatically."

    def _refresh_device(self):
        if not self.busy:
            self._refresh()

    # ---- Update --------------------------------------------------------------------------------------------------------
    def _page_update(self):
        tk, p = self.tk, self.pages["update"]
        self._intro(p, "Once diskOS Disco! is on your Disc, new versions install from the SD card: no flashing. This "
                       "signs an interface with your keys and puts it on the card. It works for any Disc you "
                       "installed with these keys, from this PC or another.")
        f, self.up_ui_st, self.up_ui_pick = self._step(p, 1, "Version", "Choose…", self._pick_up_ui)
        rr = tk.Frame(f, bg=BG); rr.grid(row=1, column=0, sticky="w", pady=(4, 0))
        self._radio(rr, "Built into this installer", self.up_ui_builtin, True, self._refresh).pack(anchor="w")
        self._radio(rr, "An mq_ui file on this PC", self.up_ui_builtin, False, self._refresh).pack(anchor="w")
        _, self.up_keys_st, _ = self._step(p, 2, "Your update keys", "Choose…", self._pick_up_keys)
        f, self.card_st, _ = self._step(p, 3, "SD card", "Look again", self._find_cards)
        self.card_var = tk.StringVar()
        from tkinter import ttk
        self.card_box = ttk.Combobox(f, textvariable=self.card_var, state="readonly", style="Dark.TCombobox",
                                     width=52, font=self.f_b)
        self.card_box.grid(row=1, column=0, sticky="w", pady=(6, 0))
        self.card_box.bind("<<ComboboxSelected>>", lambda e: self._refresh())
        row = self._action_row(p, "Make the update", self.do_update, extra=("Save to a folder instead…",
                                                                            self.do_update_folder))
        self.update_btn = row

    def _pick_up_ui(self):
        from tkinter import filedialog
        p = filedialog.askopenfilename(parent=self.root, title="Choose mq_ui")
        if p:
            self.up_ui_file = p
            self.up_ui_builtin.set(False)
            self._refresh()

    def _pick_up_keys(self):
        self._pick_keys()

    def _find_cards(self):
        def work():
            self.q.put(("cards", owner.find_cards()))
        threading.Thread(target=work, daemon=True).start()

    def _show_cards(self, cards):
        prev = self.card_var.get()
        self.cards = cards
        names = [f"{c['name']}   ({c['path']})" for c in cards]
        self.card_box.configure(values=names)
        if names:
            self.card_box.set(prev if prev in names else names[0])
            self.card_box.grid()
        else:
            self.card_box.set("")
            self.card_box.grid_remove()
        self._refresh()

    def _card(self):
        v = self.card_var.get()
        for c in self.cards:
            if v.endswith(f"({c['path']})"):
                return c
        return None

    # ---- Restore -------------------------------------------------------------------------------------------------------
    def _page_restore(self):
        p = self.pages["restore"]
        self._intro(p, "Put FiiO's own firmware back. Your music and the Disc's data folder stay. diskOS uses the "
                       "stock image it saved when you installed; without one, pick FiiO's firmware .zip.")
        _, self.rs_st, self.rs_pick = self._step(p, 1, "Stock image", "Choose…", self._pick_restore_fw)
        _, self.rs_dev_st, _ = self._step(p, 2, "Connect the Disc", "Look again", self._refresh_device)
        self.restore_btn = self._action_row(p, "Restore stock", self.do_restore,
                                            extra=("Delete this installer's saved files", self.do_forget))

    def _pick_restore_fw(self):
        from tkinter import filedialog
        p = filedialog.askopenfilename(parent=self.root, title="FiiO's firmware (.zip)",
                                       filetypes=[("Firmware", "*.zip"), ("All files", "*")])
        if p:
            self.restore_fw = p
            self._refresh()

    # ---- action row, progress, log, footer -------------------------------------------------------------------------
    def _action_row(self, page, text, command, extra=None):
        tk = self.tk
        row = tk.Frame(page, bg=BG)
        row.pack(fill="x", pady=(22, 0))
        btn = Pill(self, row, text, command, primary=True, width=max(210, self.f_btn.measure(text) + 76)).pack(side="left")
        if extra:
            b2 = Pill(self, row, extra[0], extra[1]).pack(side="left", padx=(10, 0))
            btn.extra = b2
        return btn

    def _footer(self):
        tk = self.tk
        from tkinter import ttk
        wrap = tk.Frame(self.root, bg=BG)
        wrap.pack(fill="both", expand=True, padx=36, pady=(16, 0))
        st = tk.Frame(wrap, bg=BG)
        st.pack(fill="x")
        self.status = tk.Label(st, text="", bg=BG, fg=MUTED, font=self.f_b, anchor="w")
        self.status.pack(side="left")
        self.elapsed = tk.Label(st, text="", bg=BG, fg=FAINT, font=self.f_s)
        self.elapsed.pack(side="right")
        self.bar = ttk.Progressbar(wrap, style="Iri.Horizontal.TProgressbar", mode="determinate", maximum=100)
        self.banner = tk.Label(wrap, text="DO NOT DISCONNECT THE DISC OR LET THIS PC SLEEP", bg=YELLOW, fg="#111",
                               font=self.f_h)
        box = tk.Frame(wrap, bg=BG2, highlightthickness=1, highlightbackground=LINE)
        box.pack(fill="both", expand=True, pady=(8, 0))
        self.log = tk.Text(box, height=6, bg=BG2, fg="#cfcfd8", font=self.f_mono, wrap="word", relief="flat",
                           bd=0, padx=10, pady=8, insertbackground=TEXT, highlightthickness=0)
        sb = ttk.Scrollbar(box, command=self.log.yview, style="Dark.Vertical.TScrollbar")
        self.log.configure(yscrollcommand=sb.set, state="disabled")
        self.log.pack(side="left", fill="both", expand=True)
        sb.pack(side="right", fill="y")
        for tag, col in (("ok", GREEN), ("bad", RED), ("warn", ORANGE), ("step", CYAN)):
            self.log.tag_configure(tag, foreground=col)
        bot = tk.Frame(self.root, bg=BG)
        bot.pack(fill="x", padx=36, pady=14)
        tk.Label(bot, text=f"Version {__version__}", bg=BG, fg=FAINT, font=self.f_s).pack(side="left")
        Pill(self, bot, "Copy report", self._copy_report).pack(side="right")

    # ---- page switching and state --------------------------------------------------------------------------------------
    def show(self, key):
        if self.busy and key != self.current:
            self._status("Wait for the current step to finish.", "warn")
            return
        self.current = key
        for k, f in self.pages.items():
            f.pack_forget()
        self.pages[key].pack(fill="x")
        self.page_title.configure(text=TAB_TITLES[key])
        self._draw_head()
        if key == "check":
            self.run_checks()
        elif key == "update":
            self._find_cards()
        self._refresh_device() if key in ("install", "restore") else self._refresh()

    def _refresh(self):
        """Re-evaluate every page's labels and buttons from the current choices."""
        busy = self.busy
        # install
        if self.fw:
            self._say(self.fw_st, self.fw, "ok")
        else:
            self._say(self.fw_st, "FiiO's firmware .zip for the Disc (recommended: stock V2.57).")
        if self.ui_builtin.get():
            self._say(self.ui_st, "diskOS Disco! " + self._builtin_version() + ", included with this installer.", "ok")
        elif self.ui_file:
            self._say(self.ui_st, self.ui_file, "ok")
        else:
            self._say(self.ui_st, "Click Choose… and pick an mq_ui file.")
        kdir = owner.default_keys_dir() if self.keys_new.get() else self.keys_dir
        can_inst, can_sign, missing = owner.keys_status(kdir)
        if self.keys_new.get():
            if can_inst:
                self._say(self.keys_st, f"There are already keys in {kdir}. Choose \"Use my existing keys folder\".",
                          "bad")
            else:
                self._say(self.keys_st, "New keys go to " + kdir + ". Back this folder up afterwards.", "text")
        elif not kdir:
            self._say(self.keys_st, "Choose the folder you installed with (it holds diskos-root.pub.pem).")
        elif can_inst:
            self._say(self.keys_st, kdir, "ok")
        else:
            self._say(self.keys_st, kdir + " has no diskos-root.pub.pem.", "bad")
        dev_kind, dev_text = self._device_state() if self.current in ("install", "restore") and not busy else \
            (None, None)
        if dev_kind:
            self._say(self.dev_st, dev_text, dev_kind)
            self._say(self.rs_dev_st, dev_text, dev_kind)
        keys_ok = (not can_inst) if self.keys_new.get() else can_inst
        ui_ok = self.ui_builtin.get() or bool(self.ui_file)
        self.install_btn.set(enabled=not busy and bool(self.fw) and ui_ok and keys_ok)
        # update
        if self.up_ui_builtin.get():
            self._say(self.up_ui_st, "diskOS Disco! " + self._builtin_version() + ", included with this installer.",
                      "ok")
        elif self.up_ui_file:
            self._say(self.up_ui_st, self.up_ui_file, "ok")
        else:
            self._say(self.up_ui_st, "Click Choose… and pick the mq_ui file.")
        _, sign_ok, miss = owner.keys_status(self.keys_dir)
        if not self.keys_dir:
            self._say(self.up_keys_st, "Choose the keys folder you installed with (it contains leaf.key).")
        elif sign_ok:
            self._say(self.up_keys_st, self.keys_dir, "ok")
        else:
            self._say(self.up_keys_st, self.keys_dir + " is missing " + ", ".join(miss) + ".", "bad")
        card = self._card()
        if card and card["disc"]:
            self._say(self.card_st, "Your Disc in USB storage mode: the update goes to the top of its SD card.", "ok")
        elif card:
            self._say(self.card_st, "The update goes to the top of this card, in a folder called diskos-update.",
                      "text")
        else:
            self._say(self.card_st, "No Disc or card found. Switch the Disc to USB storage mode (or use a card "
                                    "reader), then Look again - or save to a folder and copy it yourself.", "warn")
        up_ui = self.up_ui_builtin.get() or bool(self.up_ui_file)
        self.update_btn.set(enabled=not busy and sign_ok and up_ui and card is not None)
        self.update_btn.extra.set(enabled=not busy and sign_ok and up_ui)
        # restore
        if state.stock_image_exists():
            self._say(self.rs_st, "The stock image saved at install time will be used.", "ok")
            self.rs_pick.set(enabled=False)
        elif self.restore_fw:
            self._say(self.rs_st, self.restore_fw, "ok")
        else:
            self._say(self.rs_st, "No saved stock image on this PC: choose FiiO's firmware .zip.")
        self.restore_btn.set(enabled=not busy and (state.stock_image_exists() or bool(self.restore_fw)))
        self.restore_btn.extra.set(enabled=not busy)

    def _tick(self):
        if not self.busy and self.current in ("install", "restore"):
            self._refresh()
        if self.busy and self.t0:
            s = int(time.monotonic() - self.t0)
            self.elapsed.configure(text=f"{s // 60}:{s % 60:02d}")
        self.root.after(1000 if self.busy else 3000, self._tick)

    # ---- running jobs --------------------------------------------------------------------------------------------------
    def _start(self, title, fn):
        self.busy = True
        self.t0 = time.monotonic()
        self._clear_log()
        self._status(title, "text")
        self.bar.configure(mode="indeterminate", value=0)
        self.bar.pack(fill="x", pady=(6, 0), before=self.log.master)
        self.bar.start(14)
        self._refresh()

        def run():
            try:
                self.q.put(("done", fn()))
            except Exception as e:
                self.q.put(("failed", {"error": str(e)}))
        self.job = threading.Thread(target=run, daemon=True)
        self.job.start()

    def _ui_binary(self, builtin, path):
        return bundle.data("mq_ui", required=False) if builtin else path

    def do_install(self):
        make = self.keys_new.get()
        keys = owner.default_keys_dir() if make else self.keys_dir
        ui_bin = self._ui_binary(self.ui_builtin.get(), self.ui_file)
        fw = self.fw
        what = ("Install diskOS Disco! on the connected Disc?\n\n"
                f"Firmware: {os.path.basename(fw)}\nInterface: "
                f"{'built in (' + self._builtin_version() + ')' if self.ui_builtin.get() else ui_bin}\n"
                f"Update keys: {'new, in ' if make else ''}{keys}\n\n"
                "This takes about 20 minutes. Keep the Disc connected and this PC awake until it says Done. A backup "
                "of the Disc's original system is kept, so you can go back to stock later.\n\n"
                "Recommended: the Disc should be on FiiO's stock V2.57 firmware. Installing over upstream diskOS or "
                "another diskOS build is not supported.")
        if not self._ask("Install", what, "Install"):
            return
        self.mode = "install"
        self.keys_made = keys if make else None

        def job():
            if make:
                self.reporter.phase("Making your update keys")
                owner.make_keys(keys, self.reporter.log)
                self.reporter.ok("Keys made in " + keys + ". Back this folder up.")
            return service.do_install({"firmware": fw, "variant": "public", "ui_binary": ui_bin,
                                       "ota_rootpub": os.path.join(keys, owner.INSTALL_KEY)},
                                      self.reporter, self._worker_confirm)
        self._start("Installing… about 20 minutes. Keep the Disc connected.", job)

    def _update_job(self, to_card):
        ui_bin = self._ui_binary(self.up_ui_builtin.get(), self.up_ui_file)
        keys, card = self.keys_dir, self._card() if to_card else None
        self.mode = "update"

        def job():
            work = tempfile.mkdtemp(prefix="diskos-update.")
            try:
                self.reporter.phase("Signing with your keys")
                out = owner.sign_update(ui_bin, keys, os.path.join(work, "diskos-update"), self.reporter.log)
                if card and os.path.isdir(card["path"]):
                    self.reporter.phase("Copying to the card")
                    dest = owner.place_update(out, card["path"], self.reporter.log)
                    return {"update": "card", "dest": dest, "disc": card["disc"]}
                dest = owner.keep_update(out)
                return {"update": "folder", "dest": dest}
            finally:
                shutil.rmtree(work, ignore_errors=True)
        self._start("Making the update…", job)

    def do_update(self):
        self._update_job(True)

    def do_update_folder(self):
        self._update_job(False)

    def do_restore(self):
        fw = None if state.stock_image_exists() else self.restore_fw
        if not self._ask("Restore stock", "Put FiiO's stock firmware back on the connected Disc?\n\nYour music and the "
                         "Disc's data folder stay. This takes a few minutes; keep the Disc connected.", "Restore"):
            return
        self.mode = "restore"
        self._start("Restoring stock…", lambda: service.do_restore({"firmware": fw}, self.reporter,
                                                                    self._worker_confirm))

    def do_forget(self):
        if not self._ask("Delete saved files", "Delete this installer's saved files from this PC (the saved stock "
                         "image and build files)? Your keys and the Disc are not touched.", "Delete"):
            return
        self.mode = "forget"
        self._start("Deleting saved files…", lambda: service.do_remove({"force": False}, self.reporter,
                                                                       self._worker_confirm))

    # ---- confirm gate (worker -> main thread) -----------------------------------------------------------------------
    def _worker_confirm(self, summary):
        self._confirm_result = False
        self._confirm_event.clear()
        self.q.put(("confirm", summary))
        self._confirm_event.wait()
        return self._confirm_result

    def _confirm_dialog(self, summary):
        destructive = summary.get("action") in ("install", "restore-stock")
        lines = [f"{k}: {summary[k]}" for k in ("variant", "ota", "duration", "consequence", "requires")
                 if summary.get(k)]
        ok = self._ask(summary.get("action", "Confirm").replace("-", " ").capitalize(),
                       "Last check before writing to the Disc:\n\n" + "\n".join(lines),
                       "Begin (~20 min)" if destructive else "Proceed",
                       ack="I understand this rewrites my Disc and must not be interrupted." if destructive else None)
        if ok and destructive:
            self.flashing = True                       # set before the worker resumes: no closing in the gap
        self._confirm_result = ok
        self._confirm_event.set()

    def _ask(self, title, text, yes, ack=None):
        """A themed modal question. Returns True for `yes`."""
        tk = self.tk
        dlg = tk.Toplevel(self.root, bg=BG)
        dlg.title(title)
        dlg.transient(self.root)
        dlg.resizable(False, False)
        res = {"v": False}
        body = tk.Frame(dlg, bg=BG)
        body.pack(fill="both", expand=True, padx=24, pady=20)
        tk.Label(body, text=title, bg=BG, fg=TEXT, font=self.f_title, anchor="w").pack(fill="x")
        tk.Label(body, image=self.img.get(("rule", 560), lambda: rule_png(560)), bg=BG, bd=0).pack(anchor="w",
                                                                                                    pady=(6, 12))
        tk.Label(body, text=text, bg=BG, fg=MUTED, font=self.f_b, justify="left", wraplength=560,
                 anchor="w").pack(fill="x")
        row = tk.Frame(body, bg=BG)
        row.pack(fill="x", pady=(18, 0), side="bottom")

        def done(v):
            res["v"] = v
            dlg.grab_release()
            dlg.destroy()
        go = Pill(self, row, yes, lambda: done(True), primary=True).pack(side="right")
        Pill(self, row, "Cancel", lambda: done(False)).pack(side="right", padx=(0, 10))
        if ack:
            v = tk.BooleanVar(value=False)
            tk.Checkbutton(body, text=ack, variable=v, bg=BG, fg=TEXT, selectcolor=CARD, activebackground=BG,
                           activeforeground=TEXT, highlightthickness=0, font=self.f_b, wraplength=540, justify="left",
                           command=lambda: go.set(enabled=v.get())).pack(anchor="w", pady=(14, 0))
            go.set(enabled=False)
        dlg.protocol("WM_DELETE_WINDOW", lambda: done(False))
        dlg.bind("<Escape>", lambda e: done(False))
        dlg.update_idletasks()
        x = self.root.winfo_rootx() + (self.root.winfo_width() - dlg.winfo_reqwidth()) // 2
        y = self.root.winfo_rooty() + 140
        dlg.geometry(f"+{x}+{max(0, y)}")
        dlg.wait_visibility()
        dlg.grab_set()
        go.cv.focus_set()
        self.root.wait_window(dlg)
        return res["v"]

    def _msg(self, title, text, open_path=None):
        tk = self.tk
        dlg = tk.Toplevel(self.root, bg=BG)
        dlg.title(title)
        dlg.transient(self.root)
        body = tk.Frame(dlg, bg=BG)
        body.pack(fill="both", expand=True, padx=24, pady=20)
        tk.Label(body, text=title, bg=BG, fg=TEXT, font=self.f_title, anchor="w").pack(fill="x")
        tk.Label(body, image=self.img.get(("rule", 560), lambda: rule_png(560)), bg=BG, bd=0).pack(anchor="w",
                                                                                                    pady=(6, 12))
        tk.Label(body, text=text, bg=BG, fg=MUTED, font=self.f_b, justify="left", wraplength=560,
                 anchor="w").pack(fill="x")
        row = tk.Frame(body, bg=BG)
        row.pack(fill="x", pady=(18, 0))
        Pill(self, row, "OK", dlg.destroy, primary=True).pack(side="right")
        if open_path:
            Pill(self, row, "Open folder", lambda: subprocess.Popen(["xdg-open", open_path])).pack(side="right",
                                                                                                padx=(0, 10))
        dlg.update_idletasks()
        x = self.root.winfo_rootx() + (self.root.winfo_width() - dlg.winfo_reqwidth()) // 2
        dlg.geometry(f"+{x}+{self.root.winfo_rooty() + 140}")

    # ---- first start: a short, friendly heads-up (once) -------------------------------------------------------------
    WELCOME_POINTS = (
        ("A hobby project.", "diskOS Disco! is made by fans, not by FiiO or Snowsky. It's tested on real Discs, "
                             "but it comes as-is, without a warranty."),
        ("Start from stock V2.57.", "Install over FiiO's own V2.57 firmware. Other diskOS builds underneath can "
                                    "behave oddly."),
        ("You can always go back.", "The installer keeps a copy of your Disc's original system, and the Restore tab "
                                    "puts it back."),
        ("Keep your keys safe.", "Your update keys stay on this PC. Back up the keys folder: the Disc only accepts "
                                 "updates signed with them."),
    )

    def _welcome(self):
        tk = self.tk
        dlg = tk.Toplevel(self.root, bg=BG)
        dlg.title("Welcome")
        dlg.transient(self.root)
        dlg.resizable(False, False)
        body = tk.Frame(dlg, bg=BG)
        body.pack(fill="both", expand=True, padx=30, pady=24)
        top = tk.Frame(body, bg=BG)
        top.pack(fill="x")
        tk.Label(top, image=self.img.get(("disc", 56), lambda: disc_png(56)), bg=BG, bd=0).pack(side="left")
        tk.Label(top, text="  Welcome to diskOS Disco!", bg=BG, fg=TEXT, font=self.f_title).pack(side="left")
        tk.Label(body, image=self.img.get(("rule", 560), lambda: rule_png(560)), bg=BG, bd=0).pack(anchor="w",
                                                                                                    pady=(12, 14))
        tk.Label(body, text="A few things worth knowing before you start:", bg=BG, fg=MUTED, font=self.f_b,
                 anchor="w").pack(fill="x", pady=(0, 6))
        for i, (head, text) in enumerate(self.WELCOME_POINTS):
            row = tk.Frame(body, bg=BG)
            row.pack(fill="x", pady=5)
            tk.Label(row, text="●", bg=BG, fg=_hex(iri_at(i / len(self.WELCOME_POINTS))), font=self.f_b).pack(
                side="left", anchor="n", padx=(0, 10))
            col = tk.Frame(row, bg=BG)
            col.pack(side="left", fill="x")
            tk.Label(col, text=head, bg=BG, fg=TEXT, font=self.f_h, anchor="w").pack(fill="x")
            tk.Label(col, text=text, bg=BG, fg=MUTED, font=self.f_b, anchor="w", justify="left",
                     wraplength=520).pack(fill="x")
        row = tk.Frame(body, bg=BG)
        row.pack(fill="x", pady=(20, 0))

        def go():
            try:
                os.makedirs(os.path.dirname(WELCOME_SEEN), exist_ok=True)
                open(WELCOME_SEEN, "w").close()
            except OSError:
                pass                                   # shown again next time: harmless
            dlg.grab_release()
            dlg.destroy()
        Pill(self, row, "Let's go", go, primary=True, width=150).pack(side="right")
        Pill(self, row, "Quit", self.root.destroy).pack(side="right", padx=(0, 10))
        dlg.protocol("WM_DELETE_WINDOW", go)
        dlg.bind("<Return>", lambda e: go())
        dlg.update_idletasks()
        x = self.root.winfo_rootx() + (self.root.winfo_width() - dlg.winfo_reqwidth()) // 2
        dlg.geometry(f"+{max(0, x)}+{self.root.winfo_rooty() + 120}")
        dlg.wait_visibility()
        dlg.grab_set()

    def _about(self):
        self._msg("About", f"{APP} for Linux · installer {__version__} · interface {self._builtin_version()}\n\n"
                  "Installs diskOS Disco! on the FiiO Snowsky Disc and makes signed SD-card updates with your own "
                  "keys. A best-effort hobby project, as-is, without warranty; not affiliated with FiiO or Snowsky.\n\n"
                  "Installer: MIT. Interface: GPL-3.0-or-later. Third-party licenses: NOTICE.md.\n"
                  "github.com/zmd22/diskos-disco")

    # ---- events from the worker ------------------------------------------------------------------------------------
    def _drain(self):
        try:
            for _ in range(200):
                kind, p = self.q.get_nowait()
                try:
                    self._handle(kind, p)
                except Exception as e:
                    self._log(f"[ui error handling {kind}: {e}]", "bad")
        except queue.Empty:
            pass
        self.root.after(80, self._drain)

    def _handle(self, kind, p):
        if kind == "phase":
            self._status(p["name"] + "…", "text")
            self._log("▶ " + p["name"], "step")
            if p.get("destructive"):
                self.flashing = True
                self.banner.pack(fill="x", pady=(6, 0), before=self.log.master)
        elif kind == "status":
            self._status(p["message"], "text")
            self._log(p["message"])
        elif kind == "log":
            self._log("   " + p["line"])
        elif kind == "progress":
            self.bar.stop()
            self.bar.configure(mode="determinate", maximum=p["total"] or 1, value=p["completed"])
        elif kind == "indeterminate":
            if p["active"]:
                self.bar.configure(mode="indeterminate"); self.bar.start(14)
            else:
                self.bar.stop(); self.bar.configure(mode="determinate")
        elif kind == "warning":
            self._log("! " + p["message"], "warn")
        elif kind == "ok":
            self._log("✔ " + p["message"], "ok")
        elif kind == "error":
            self._log("✖ " + p["message"], "bad")
        elif kind == "confirm":
            self._confirm_dialog(p)
        elif kind == "done":
            self._finish(p or {})
        elif kind == "failed":
            self._end()
            self._status(p.get("error", "Something went wrong."), "bad")
            self._log("✖ " + p.get("error", ""), "bad")
            self._msg("That didn't work", p.get("error", "Something went wrong.") +
                      "\n\nNothing more will happen. Copy report and send it if you need help.")
        elif kind == "checks":
            self._show_checks(p)
        elif kind == "cards":
            self._show_cards(p)
        elif kind == "usb-allowed":
            if p == 0:
                self._msg("USB access", "Done. Unplug the Disc and plug it in again.")
            self.run_checks()

    def _end(self):
        self.busy = self.flashing = False
        self.bar.stop()
        self.bar.pack_forget()
        self.banner.pack_forget()
        self.elapsed.configure(text="")
        self._refresh()

    def _finish(self, r):
        self._end()
        mode = getattr(self, "mode", "")
        if r.get("aborted"):
            self._status("Stopped before anything was written to the Disc.", "warn")
            return
        if r.get("errors"):
            self._status("Finished with problems: see the log.", "bad")
            return
        if mode == "install":
            self._status("Done. Unplug the Disc and turn it on.", "ok")
            self._msg("Done!", "Unplug the Disc and turn it on. diskOS Disco! installs itself on the first start.\n\n"
                      + ("Important: copy your keys folder to a USB stick or another safe place. Your Disc only "
                         "accepts updates signed with these keys.\n\n" if self.keys_made else "")
                      + "Happy listening!", open_path=self.keys_made)
            if self.keys_made:
                self.keys_dir = self.keys_made
                self.keys_new.set(False)
            self._refresh()
        elif mode == "update" and r.get("update") == "card":
            self._status("Done: the update is on the card.", "ok")
            self._msg("The update is on the card",
                      ("1. Eject the Disc safely, then switch it back to Playback.\n" if r.get("disc") else
                       "1. Eject the card safely and put it back in the Disc.\n")
                      + "2. On the Disc: Settings › System › Maintenance › Update from SD Card › Update, then "
                        "restart.\n3. Play some music for a few minutes: that keeps the new version.")
        elif mode == "update":
            self._status("Saved: copy the diskos-update folder to the card.", "ok")
            self._msg("Copy it to the card yourself",
                      "No Disc or card was used, so the update was saved here:\n\n" + r["dest"] + "\n\n"
                      "Copy the whole diskos-update folder to the top of the Disc's SD card (so the path is "
                      "<card>/diskos-update/mq_ui), eject it safely, then on the Disc: Settings › System › "
                      "Maintenance › Update from SD Card › Update.", open_path=os.path.dirname(r["dest"]))
        elif mode == "restore":
            self._status("Done. Unplug the Disc and turn it on: it starts FiiO's firmware.", "ok")
        else:
            self._status("Done.", "ok")

    # ---- log and status ----------------------------------------------------------------------------------------------
    def _status(self, text, kind="muted"):
        self.status.configure(text=text, fg={"ok": GREEN, "bad": RED, "warn": ORANGE, "text": TEXT,
                                             "muted": MUTED}[kind])

    def _log(self, line, tag=None):
        self.log.configure(state="normal")
        self.log.insert("end", line + "\n", tag or ())
        if int(self.log.index("end-1c").split(".")[0]) > 2000:
            self.log.delete("1.0", "200.0")
        self.log.see("end")
        self.log.configure(state="disabled")

    def _clear_log(self):
        self.log.configure(state="normal")
        self.log.delete("1.0", "end")
        self.log.configure(state="disabled")

    def _copy_report(self):
        o, a = platform_probe.host()
        rep = (f"{APP} for Linux {__version__} | {o}-{a} {platform_probe.distro_name() or ''} | interface "
               f"{self._builtin_version()} | tab {self.current}\n" + self.log.get("1.0", "end"))
        self.root.clipboard_clear()
        self.root.clipboard_append(rep)
        self._status("Report copied. Paste it into a message or a GitHub issue.", "text")

    def _on_close(self):
        if self.flashing:
            self._msg("Please wait", "The Disc is being written. Closing now could leave it needing a recovery. "
                                     "Wait until it says Done.")
            return
        if self.busy and not self._ask("Still working", "Something is still running. Close anyway?", "Close"):
            return
        self.root.destroy()


def install_desktop_entry():
    """Add diskOS Disco! to the applications menu (~/.local/share/applications), launched without a terminal."""
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    data = os.environ.get("XDG_DATA_HOME") or os.path.expanduser("~/.local/share")
    icon_dir = os.path.join(data, "icons", "hicolor", "256x256", "apps")
    os.makedirs(icon_dir, exist_ok=True)
    icon = os.path.join(icon_dir, "diskos-disco.png")
    with open(icon, "wb") as f:
        f.write(disc_png(256))
    apps = os.path.join(data, "applications")
    os.makedirs(apps, exist_ok=True)
    entry = os.path.join(apps, "diskos-disco.desktop")
    launcher = os.path.join(here, "diskos-installer").replace('"', '\\"')
    with open(entry, "w") as f:
        f.write("[Desktop Entry]\nType=Application\nName=diskOS Disco!\nGenericName=Disc installer\n"
                "Comment=Install diskOS Disco! on the Snowsky Disc and make SD-card updates\n"
                f"Exec=\"{launcher}\" gui\nIcon=diskos-disco\nTerminal=false\nCategories=Utility;AudioVideo;\n"
                "StartupWMClass=Diskos-disco\n")
    os.chmod(entry, 0o755)
    return entry


def main():
    try:
        import tkinter as tk
    except Exception as e:
        sys.stderr.write(f"{APP}: Tkinter unavailable ({e}). Install your distribution's python3-tkinter / tk "
                         "package, or use the CLI: diskos-installer --help\n")
        return 2
    root = tk.Tk(className="Diskos-disco")
    App(root)
    root.mainloop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
