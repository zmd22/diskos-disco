"""Mask-ROM USB flashing - Python wrapper around the PROVEN native binaries
(usbboot + the my_write6 DRAM NAND writer + the X2000 SPL). We do NOT reimplement
the flashing protocol; we orchestrate it, watch it, and interpret the result.

Faithful port of flash_diskos.sh, with the python helper snippets (poison blob,
debug-struct parse) folded in natively, plus honest progress and fail-closed
result interpretation.
"""

import os
import shutil
import signal
import struct
import subprocess
import tempfile

from . import basegate, bundle, platform_probe
from .reporter import CLIReporter
from .errors import FlashError, DEVICE_RESULT_CODES, RECOVERABLE, WAIT_THEN_RECOVER


class BaseRefused(FlashError):
    """The writer's base gate refused BEFORE unlocking/erasing anything: the Disc is untouched. `.verdict` holds the
    parsed gate verdict (basegate.read_verdict), incl. the base the Disc is really on when it is a known one."""
    code = "E320"

    def __init__(self, message, verdict, **kw):
        super().__init__(message, **kw)
        self.verdict = verdict


# The ONLY writer this installer will run: flash/my_write6.c built by flash/build_nand.sh (reproducible, byte-exact).
# Identified by its exact SHA-256 - not by a code offset - so an older writer that would ignore the base plan can
# never be started. Its descriptor (magic "DGWD") must also agree on protocol, variant and capacity.
WRITER_NAME = "my_write6_dram.bin"
WRITER_SHA256 = "a3c3aa63e0c48f501181d542d9fa0526d36ded81e2e767f0c044af876674ea84"
PLAN_ADDR = "0xa0b00000"

IMG_SIZE = 100663296
SQUASH_MAGIC = b"hsqs"

# usbboot waits FLASH_WAIT seconds for the writer, then reads the DONE marker back. The writer
# finishes well inside this on real hardware (validated repeatedly). A too-short wait shows as a
# not-DONE readback (retry with a longer wait) - it never corrupts the flash. A device with an
# unusually high bad-block count can raise it via DISKOS_FLASH_WAIT.
# my_write6 first hashes the ~96 MiB image and ~25 MiB of base partitions from uncached DRAM (time not yet measured on
# a device), so the wait has 3 min more than my_write5's 15.
FLASH_WAIT = os.environ.get("DISKOS_FLASH_WAIT", "1080")   # 18 min
_FLASH_WAIT_SECS = int(FLASH_WAIT) if FLASH_WAIT.isdigit() else 1080

RESULT_NAMES = {code: name for code, (_fcode, name) in DEVICE_RESULT_CODES.items()}

FLASH_EXPECT_SECS = 20 * 60   # ~20 min expected (the base check + image hash add time; not yet measured)
# Hard ceiling for the whole usbboot invocation: the writer --wait plus margin for the image
# download + result readback; then treat a still-running usbboot as a hung/reset device and
# terminate it (E303) rather than blocking forever.
FLASH_HARD_TIMEOUT_SECS = _FLASH_WAIT_SECS + 15 * 60   # writer wait + 15 min margin


def _probe_helpers():
    """Confirm every bundled native tool can actually execute (and load its libs)
    BEFORE the destructive gate. Catches a noexec temp mount / missing dylibs while
    the device is still untouched. Non-zero exit is fine; an OSError is not."""
    probes = [(bundle.native("usbboot"), ["--help"]),
              (bundle.native("mksquashfs"), ["-version"]),
              (bundle.native("unsquashfs"), ["-version"])]
    for path, args in probes:
        try:
            subprocess.run([path] + args, env=bundle.native_env(path),
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                           timeout=15)
        except OSError as e:
            raise FlashError(
                f"native tool '{os.path.basename(path)}' at '{path}' cannot execute ({e})",
                code="E103",
                action="the tool's directory may be on a noexec mount, or a required library is "
                       "missing; move the installer to an exec-capable filesystem, install "
                       "squashfs-tools, or rebuild the bundled tools, and retry")
        except subprocess.TimeoutExpired:
            pass   # it started (that's all we needed to prove)


def preflight(image_path, rep=None):
    """Validate the image and that exactly one device is in mask-ROM. Fail closed."""
    rep = rep or CLIReporter()
    if not os.path.exists(image_path):
        raise FlashError(f"image not found: {image_path}", code="E120")
    sz = os.path.getsize(image_path)
    if sz != IMG_SIZE:
        raise FlashError(f"wrong image size {sz} (must be {IMG_SIZE})", code="E121",
                         action="use a diskOS image built by this installer")
    with open(image_path, "rb") as f:
        if f.read(4) != SQUASH_MAGIC:
            raise FlashError("image is not a squashfs (bad magic) - not a diskOS/stock image", code="E122")

    _probe_helpers()   # every bundled tool must actually execute (catches noexec /tmp, missing libs)

    n = platform_probe.maskrom_count()
    if n == 0:
        raise FlashError(
            "no device in mask-ROM mode", code="E110",
            action="power the device OFF, hold Volume-Down, plug in USB (screen stays "
                   "black), then retry")
    if n > 1:
        raise FlashError(f"{n} devices in mask-ROM mode - need exactly 1", code="E111",
                         action="unplug the other Ingenic devices")
    # Seeing the device is not enough: usbboot must OPEN it (GitHub #10 - counted fine, then E301). Refuse with the
    # exact fix instead. An unknown result (not Linux, unexpected error) is left to usbboot, which reports itself.
    access, where = platform_probe.maskrom_access()
    if access == "denied":
        raise FlashError(f"the device is in mask-ROM mode but this user cannot open it ({where}) - USB permissions",
                         code="E113", action=platform_probe.USB_ACCESS_FIX)
    if n < 0:
        # FAIL CLOSED: we could not prove exactly one device (no libusb backend or a
        # permission error). Never cross the destructive gate on an unproven count.
        raise FlashError(
            "cannot confirm exactly one device (USB enumeration failed - missing libusb "
            "backend or insufficient USB permissions)", code="E112",
            action="refusing to flash on an unproven device count; fix USB access and retry")


def _parse_debug(dbg_path):
    """Parse the 1KB little-endian debug readback (256 x uint32). Returns a dict.
    Mirrors the field offsets in flash_diskos.sh."""
    with open(dbg_path, "rb") as f:
        raw = f.read(1024)
    if len(raw) < 1024:
        raise FlashError(f"short debug readback ({len(raw)} bytes) - flash result UNKNOWN", code="E302",
                         action=RECOVERABLE)
    w = struct.unpack("<256I", raw)
    nbad = w[20]
    return {
        "magic": w[0],
        "start_block": w[5],
        "nlogblocks": w[6],   # writer's compiled logical-block capacity - MUST cover the whole image
        "done": w[9],
        "skipped": w[10],
        "result": w[16],
        "retried": w[17],
        "worst_retries": w[18],
        "bad_found": nbad,
        "bad_list": [w[40 + i] for i in range(min(nbad, 64))],
    }


def _writer_descriptor(writer_path):
    """(nlogblocks, blob_schema) of the pinned production writer, or None if the file is not exactly it."""
    import hashlib
    try:
        data = open(writer_path, "rb").read()
    except OSError:
        return None
    if hashlib.sha256(data).hexdigest() != WRITER_SHA256:
        return None
    k = data.find(struct.pack("<I", 0x44574744))              # "DGWD" descriptor
    if k < 0 or k + 32 > len(data):
        return None
    magic, tool, variant, schema, start, nlog, blob, dbg = struct.unpack("<8I", data[k:k + 32])
    if tool != 6 or variant != 1 or schema != basegate.BLOB_SCHEMA or start != 80 or blob != 0xa0b00000:
        return None
    return nlog, schema


def flash(image_path, target_ver, log_path=None, rep=None):
    """Flash `image_path` (built from firmware `target_ver`'s rootfs) via mask-ROM. The writer first proves ON THE
    DEVICE that the Disc's kernel + recovery are exactly `target_ver`'s (basegate) and that the image in its memory is
    this one; only then does it unlock and write. Returns the parsed debug dict on SUCCESS; raises BaseRefused if the
    gate refused (nothing written), FlashError otherwise (fail-closed)."""
    rep = rep or CLIReporter()
    launched = [False]
    try:
        return _flash(image_path, target_ver, log_path, rep, launched)
    except FlashError as e:
        if not launched[0]:
            e.pre_launch = True                          # usbboot never started: the Disc is untouched
        raise
    except Exception as e:
        if launched[0]:
            raise
        # an OS/tool error before usbboot started (temp dir, plan/log file, Popen itself): the Disc is untouched
        err = FlashError(f"could not start the flash: {e}", code="E304",
                         action="nothing was sent to the Disc; fix the problem above and retry")
        err.pre_launch = True
        raise err from e


def _flash(image_path, target_ver, log_path, rep, launched):
    preflight(image_path, rep)
    image_path = os.path.abspath(image_path)

    usbboot = bundle.native("usbboot")
    writer = bundle.native(WRITER_NAME)
    spl = bundle.native("disc_spl_lpddr3.bin")

    # PRE-WRITE capacity gate: refuse a truncating writer before it touches the NAND (the post-write
    # E311 check is too late - the image is already partially programmed). This is the 580-vs-768 bug.
    need_blocks = IMG_SIZE // (128 * 1024)
    desc = _writer_descriptor(writer)
    if desc is None:
        raise FlashError(
            "the NAND writer is not the exact build this installer was made with - refusing to run it "
            "(an unknown writer might not check the Disc's firmware base)", code="E123",
            action="use a diskOS installer bundle built by this project")
    wcap = desc[0]
    if target_ver not in basegate.CATALOGUE:
        raise FlashError(f"no known firmware base for V{target_ver} - refusing to flash", code="E125",
                         action="this firmware version is not supported by this installer")
    if wcap != need_blocks:
        raise FlashError(
            f"NAND writer covers {wcap} x 128 KB blocks but the image is exactly {need_blocks} - the writer "
            f"and the image must match exactly (nothing has been written).", code="E124",
            action=f"rebuild my_write6 with NLOGBLOCKS={need_blocks} (flash/build_nand.sh) and the bundle")
    rep.log(f"writer capacity (pre-write): {wcap} blocks (image needs {need_blocks})")

    tmp = tempfile.mkdtemp(prefix="diskos-flash-")
    poison = os.path.join(tmp, "poison.bin")
    dbg = os.path.join(tmp, "dbg.bin")
    with open(poison, "wb") as f:
        f.write(b"\xee" * 128)
    plan = os.path.join(tmp, "plan.bin")
    with open(plan, "wb") as f:
        f.write(basegate.build_plan(target_ver, image_path))     # binds the base AND this exact image
    rep.log(f"base required on the Disc: {basegate.CATALOGUE[target_ver]['name']}")

    cmd = [
        usbboot, "-v", "--cpu", "x2000", "--stage1", spl, "--wait", "2",
        "--addr", "0xa0c00000", "--download", writer,
        "--addr", "0xa1000000", "--download", image_path,
        "--addr", PLAN_ADDR, "--download", plan,
        "--addr", "0xa0a00000", "--download", poison,
        "--start1", "0xa0c00030", "--wait", FLASH_WAIT,
        "--addr", "0xa0a00000", "--length", "0x400", "--upload", dbg,
    ]

    rep.phase("Flashing (mask-ROM) - about 20 minutes", destructive=True)
    rep.warning("Do NOT disconnect the device or let the host sleep during the flash.")

    logf = open(log_path, "w") if log_path else open(os.path.join(tmp, "flash.log"), "w")
    rep.indeterminate(True, note="flashing (scan -> erase -> program -> verify -> retry)",
                      expect_secs=FLASH_EXPECT_SECS)
    try:
        try:
            with _sleep_inhibited(rep):
                # own session so a GUI/parent crash can't SIGPIPE the flasher; output goes
                # to a file (never a pipe) so a closed reader can't deadlock or kill it.
                proc = subprocess.Popen(cmd, stdout=logf, stderr=subprocess.STDOUT,
                                        env=bundle.native_env(cmd[0]), start_new_session=True)
                launched[0] = True                       # usbboot is running: from here on the Disc may be written

                def _kill_flasher():
                    # The flasher runs in its OWN session (start_new_session) so a parent SIGPIPE
                    # can't kill it - but that also means it SURVIVES us. Kill the host process group
                    # and reap without blocking. NB this stops only the HOST side: once usbboot has
                    # started the writer, the writer keeps running ON THE DISC regardless.
                    try:
                        os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
                    except OSError:
                        proc.kill()
                    try:
                        proc.wait(timeout=30)   # reap; don't block if stuck in D-state
                    except subprocess.TimeoutExpired:
                        pass

                try:
                    # A3: bound the wait. If the device resets/re-enumerates mid-flash,
                    # usbboot can block on a dead handle forever - never hang the app.
                    rc = proc.wait(timeout=FLASH_HARD_TIMEOUT_SECS)
                except subprocess.TimeoutExpired:
                    _kill_flasher()
                    raise FlashError(
                        f"flash timed out - the device stopped responding after "
                        f"{FLASH_HARD_TIMEOUT_SECS // 60} min (it most likely reset "
                        "mid-flash). Result UNKNOWN.", code="E303", action=WAIT_THEN_RECOVER)
                except BaseException:
                    # Ctrl-C, SIGTERM, a GUI/parent crash, or any error during the wait. Stop the host
                    # side, and say plainly that the Disc may STILL be writing: the writer runs on the
                    # device by itself once started, so unplugging now could leave it half-written.
                    rep.warning("The flash was interrupted on this computer. " + WAIT_THEN_RECOVER)   # first:
                    _kill_flasher()                      # a second Ctrl-C during the reap must not lose the warning
                    raise
        finally:
            rep.indeterminate(False)
            logf.close()

        rep.log(f"usbboot exit={rc}")
        if not os.path.exists(dbg):
            raise FlashError(
                "no debug readback produced - flash result UNKNOWN; assume FAILED",
                code="E301", action=RECOVERABLE)

        d = _parse_debug(dbg)
        with open(dbg, "rb") as f:
            verdict = basegate.read_verdict(f.read(1024))
        d["gate"] = verdict
        if d["magic"] == 0x4006E006 and d["done"] == 0x55555555 and verdict["result"] != 1:
            # the gate refused: the writer returned before unlocking/erasing anything
            rep.log(f"base gate: 0x{verdict['result']:08X} ({verdict['meaning']}); Disc base seen: "
                    f"{verdict['matched_base'] or 'unknown'}")
            if verdict["erase_started"]:
                raise FlashError("the writer reported a refusal AFTER starting to erase - flash result UNKNOWN",
                                 code="E321", action=RECOVERABLE)
            raise BaseRefused(basegate.refusal_message(verdict, target_ver), verdict)
        ok = (d["magic"] == 0x4006E006 and d["done"] == 0x55555555
              and d["result"] == 0x600DF10C and verdict["result"] == 1)
        # SAFETY: the writer only programs its compiled NLOGBLOCKS (dbg[6]) logical blocks. If that is
        # fewer than the image occupies, the TAIL is silently dropped - and squashfs keeps its
        # inode/directory/fragment tables at the tail, so a truncated image mounts-fails and won't
        # boot, WHILE the device still reports SUCCESS for the part it did write. Require the writer's
        # capacity to cover the whole image. (This is exactly the 580-vs-768 bug; this check catches it.)
        need_blocks = IMG_SIZE // (128 * 1024)
        cap_ok = d["nlogblocks"] == need_blocks
        rep.log(f"scan: bad-blocks-found={d['bad_found']} list={d['bad_list']}")
        rep.log(f"writer capacity: {d['nlogblocks']} blocks (image needs {need_blocks})")
        # B4: dbg[17]/[18] mean retried/worst only on SUCCESS; on the out-of-space /
        # block-write-fail aborts they hold the last phys/logical block; on the other
        # aborts they are 0 and must NOT be shown as "last block" (would be misleading).
        if ok:
            rep.log(f"write: skipped={d['skipped']} retried={d['retried']} "
                    f"worst={d['worst_retries']}")
        elif d["result"] in (0xDEAD0002, 0xDEAD0003):
            rep.log(f"write: skipped={d['skipped']} last-phys-block={d['retried']} "
                    f"last-logical-block={d['worst_retries']}")
        else:
            rep.log(f"write: skipped={d['skipped']}")
        fcode, name = DEVICE_RESULT_CODES.get(d["result"], ("F000", "UNKNOWN"))
        rep.log(f"result: 0x{d['result']:08X} [{fcode}] {name}")

        if not ok:
            e = FlashError(
                f"flash FAILED (device result [{fcode}] {name}, "
                f"magic=0x{d['magic']:08X} done=0x{d['done']:08X})",
                code="E310", action=RECOVERABLE)
            e.verdict = verdict                                  # the gate had passed: the base IS target_ver's
            raise e
        if not cap_ok:
            raise FlashError(
                f"writer/image size MISMATCH: the flashing tool programs {d['nlogblocks']} x 128 KB "
                f"blocks but this image needs {need_blocks}. The image was TRUNCATED - the device "
                f"reports success but will not boot. This is a build/tooling defect; do not ship or "
                f"trust this build.", code="E311", action=RECOVERABLE)
        rep.ok("flash verified OK")
        return d
    finally:
        # B3: never leak the per-flash tempdir (poison/dbg/flash.log), but keep the log
        # debuggable: if no external log_path was given, preserve the internal log to a
        # single stable file (overwritten each flash - bounded, not a growing leak).
        internal_log = os.path.join(tmp, "flash.log")
        if not log_path and os.path.exists(internal_log):
            try:
                # Unique, 0600, no symlink-follow: write THROUGH the mkstemp fd (never reopen the path,
                # which could follow a swapped-in symlink and truncate an arbitrary target). Path reported.
                fd, kept = tempfile.mkstemp(prefix="diskos-flash-", suffix=".log")
                with os.fdopen(fd, "wb") as out, open(internal_log, "rb") as src:
                    shutil.copyfileobj(src, out)
                rep.log(f"flash log saved to {kept}")
            except OSError:
                pass
        shutil.rmtree(tmp, ignore_errors=True)


class _sleep_inhibited:
    """Best-effort host sleep inhibitor for the duration of the flash. No-op if the
    platform tool isn't available; never blocks or fails the flash. If it CAN'T
    inhibit sleep, it warns (B5) - a suspend mid-flash would abort it."""

    def __init__(self, rep=None):
        self.rep = rep

    def __enter__(self):
        self.proc = None
        o, _ = platform_probe.host()
        try:
            if o == "macos":
                self.proc = subprocess.Popen(["caffeinate", "-dimsu"])
            elif o == "linux":
                import shutil
                if shutil.which("systemd-inhibit"):
                    # keep a long-lived inhibitor process alive; we kill it on exit
                    self.proc = subprocess.Popen(
                        ["systemd-inhibit", "--what=sleep:idle",
                         "--why=diskOS flash in progress", "sleep", "infinity"])
        except Exception:
            self.proc = None
        if self.proc is None and self.rep is not None:
            self.rep.warning(
                "could not auto-inhibit system sleep on this host - make sure your "
                "computer will NOT sleep/suspend for the next ~25 minutes (a suspend "
                "mid-flash aborts it; the device stays recoverable).")
        return self

    def __exit__(self, *exc):
        if self.proc:
            try:
                self.proc.terminate()
                self.proc.wait(timeout=5)   # reap so no zombie / stray 'sleep infinity' lingers
            except Exception:
                try:
                    self.proc.kill()
                except Exception:
                    pass
        return False
