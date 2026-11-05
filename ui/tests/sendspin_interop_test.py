#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""MA Sendspin interop: runs the host build of the Disc's Sendspin player against aiosendspin's server (the library
Music Assistant uses) and walks the Sendspin 1.0 flows end to end:

  1. unpaired (Sentinel) connection, approved by the operator: roles, clock sync, PCM audio, metadata, artwork
  2. dynamic pairing code (digits), with one wrong code first: the code the Disc shows is typed into the server
  3. the client restarts and comes back paired (long-term PSK)
  4. server/unpair, then pairing with the QR-code format (the token the Disc shows)
  5. unpair again, then the Pairing PSK (the token from Settings)
  6. Allow Unpaired off: an unpaired, unapproved Disc is refused playback with pairing_required

Needs aiosendspin 10.x (pip install "aiosendspin[server]"). Usage:
  python3 ui/tests/sendspin_interop_test.py path/to/sendspin_host_binary
(the binary: make -C ui sendspin-host)"""
import asyncio, base64, os, re, subprocess, sys, tempfile, time

try:
    from aiosendspin.noise.keys import Identity
    from aiosendspin.noise.pairing import PairingAttempt
    from aiosendspin.noise.trust_store import InMemoryServerPairingStore
    from aiosendspin.models.types import PairMethod, PairingCodeFormat
    from aiosendspin.server import SendspinServer
    from aiosendspin.server.audio import AudioFormat
    from aiosendspin.server.roles.metadata import Metadata
    from PIL import Image
except ImportError as e:  # pragma: no cover
    print(f"SKIP: aiosendspin not installed ({e})"); sys.exit(0)

BIN = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else None
PORT = 18927
fails = 0


def check(what, ok, detail=""):
    global fails
    print(("ok   " if ok else "FAIL ") + what + (f"  ({detail})" if detail and not ok else ""))
    if not ok:
        fails += 1


def status(d):
    try:
        return dict(l.split("=", 1) for l in open(os.path.join(d, "status")).read().splitlines() if "=" in l)
    except OSError:
        return {}


async def wait_for(pred, timeout=15.0, step=0.1):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        v = pred()
        if v:
            return v
        await asyncio.sleep(step)
    return None


def b64u_dec(s):
    return base64.urlsafe_b64decode(s + "=" * (-len(s) % 4))


class Disc:
    def __init__(self, d, unpaired=1):
        self.d, self.unpaired = d, unpaired
        self.p = None

    def start(self):
        env = dict(os.environ, SS_DIR=self.d, SS_KEYS=os.path.join(self.d, "keys"), SS_DEBUG="1")
        self.log = open(os.path.join(self.d, "log"), "a")
        self.p = subprocess.Popen([BIN, f"127.0.0.1:{PORT}", "Test Disc \"quoted\"", "x", "0", "2000", "5999", str(self.unpaired)],
                                  env=env, stdout=subprocess.DEVNULL, stderr=self.log)

    def stop(self):
        if self.p:
            self.p.terminate()
            try:
                self.p.wait(5)
            except subprocess.TimeoutExpired:
                self.p.kill()
            self.p = None
            self.log.close()

    def logtext(self):
        return open(os.path.join(self.d, "log")).read()

    def keys(self):
        k = {}
        for l in open(os.path.join(self.d, "keys")).read().splitlines():
            parts = l.split()
            if parts and parts[0] in ("identity", "pairing"):
                k[parts[0]] = b64u_dec(parts[1])
            elif parts and parts[0] == "record":
                k.setdefault("records", []).append(parts[1])
        return k


def shown_code(d):
    s = status(d).get("pair", "")
    if s.startswith("digits:"):
        return s[7:]
    if s.startswith("qr:"):
        return s[3:]
    return None


async def main():
    if not BIN or not os.path.exists(BIN):
        print("usage: sendspin_interop_test.py <sendspin host binary>"); return 2
    loop = asyncio.get_running_loop()
    store = InMemoryServerPairingStore()
    server = SendspinServer(loop=loop, identity=Identity.generate(), server_name="Interop MA", pairing_store=store,
                            allow_unencrypted=False, allow_noncompliant_clients=False)
    await server.start_server(port=PORT, host="127.0.0.1", advertise_addresses=[], discover_clients=False)
    d = tempfile.mkdtemp(prefix="ss-interop-")
    disc = Disc(d)
    disc.start()
    try:
        # ---- 1. unpaired
        cl = await wait_for(lambda: server.connected_clients[0] if server.connected_clients else None)
        check("encrypted handshake (Sentinel)", cl is not None and "noise: encrypted (unpaired)" in disc.logtext(), disc.logtext()[-800:])
        if cl is None:
            return 1
        cid = cl.client_id
        k = disc.keys()
        from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey
        pub = X25519PrivateKey.from_private_bytes(k["identity"]).public_key().public_bytes_raw()
        check("client_id is the public key", base64.urlsafe_b64encode(pub).rstrip(b"=").decode() == cid)
        check("name sent escaped", cl.name == 'Test Disc "quoted"', cl.name)
        check("waiting for approval", await wait_for(lambda: status(d).get("state") == "waiting") is not None, str(status(d)))
        await server.trust_unpaired(cid)
        check("approved: roles active", await wait_for(lambda: "roles player metadata artwork controller" in disc.logtext()) is not None, disc.logtext()[-600:])
        check("clock synced", await wait_for(lambda: "clock synced" in disc.logtext()) is not None)
        await asyncio.sleep(0.5)
        group = cl.group
        stream = group.start_stream()
        group.group_role("metadata").set_metadata(Metadata(title="Ünïcødé \"Song\"", artist="Artist", album="Album",
                                                           track_progress=1000, track_duration=200000, playback_speed=1000))
        img = Image.new("RGB", (500, 300), (200, 30, 30))
        await group.group_role("artwork").set_album_artwork(img)
        import math, struct
        tone = b"".join(struct.pack("<hh", v, v) for v in (int(12000 * math.sin(2 * math.pi * 440 * i / 44100)) for i in range(4410)))
        for _ in range(20):
            stream.prepare_audio(tone, AudioFormat(sample_rate=44100, bit_depth=16, channels=2))
            await stream.commit_audio()
        check("player stream start (FLAC 44.1/16/2, listed first)", await wait_for(lambda: "stream/start flac 44100 Hz 2 ch 16 bit" in disc.logtext()) is not None, disc.logtext()[-800:])
        m = await wait_for(lambda: re.search(r"flac: decoding \((\d+) frames so far, peak (\d+)\)", disc.logtext()))
        check("FLAC chunks decode to the tone", m is not None and int(m.group(1)) >= 44100 and 11000 < int(m.group(2)) <= 12001 and "did not decode" not in disc.logtext(), m.group(0) if m else disc.logtext()[-600:])
        st = await wait_for(lambda: status(d) if status(d).get("title") else None)
        check("metadata shown", st is not None and st.get("title") == 'Ünïcødé "Song"' and st.get("artist") == "Artist", str(st))
        cov = await wait_for(lambda: (lambda t: t and os.path.exists(os.path.join(os.path.dirname(t), "cover.jpg")) and t)(status(d).get("track")))
        if cov:
            im = Image.open(os.path.join(os.path.dirname(cov), "cover.jpg"))
            check("artwork 360x360 jpeg", im.format == "JPEG" and im.size == (360, 360), f"{im.format} {im.size}")
        else:
            check("artwork received", False, disc.logtext()[-800:])
        group.stop_stream() if hasattr(group, "stop_stream") else None

        # ---- 2. dynamic pairing code (digits), one wrong code first
        calls = {"n": 0}

        async def provide():
            calls["n"] += 1
            code = await wait_for(lambda: shown_code(d), 20)
            return "000000" if calls["n"] == 1 and code != "000000" else code
        try:
            await asyncio.wait_for(cl.connection.initiate_pairing(PairingAttempt(method=PairMethod.DYNAMIC_PAIRING_CODE,
                                   pairing_code_provider=provide, pairing_format=PairingCodeFormat.DIGITS)), 60)
            ok = True
        except Exception as e:  # noqa: BLE001
            ok = False; print("   pairing error:", repr(e))
        check("dynamic code pairing (after a wrong code)", ok and calls["n"] == 2, f"calls={calls['n']}")
        check("record stored on the Disc", await wait_for(lambda: disc.keys().get("records")) is not None)
        check("re-handshake to long-term key", await wait_for(lambda: "re-handshake done (paired)" in disc.logtext()) is not None, disc.logtext()[-800:])
        check("status says paired", await wait_for(lambda: status(d).get("paired") == "1") is not None, str(status(d)))
        check("code hidden after pairing", not status(d).get("pair"))

        # ---- 3. restart: comes back paired
        disc.stop(); disc.start()
        check("reconnects paired (long-term PSK)", await wait_for(lambda: "noise: encrypted (paired)" in disc.logtext(), 20) is not None, disc.logtext()[-600:])
        cl = await wait_for(lambda: server.get_client(cid) if server.get_client(cid) and server.get_client(cid).connection else None)
        check("paired playback activates without approval", await wait_for(lambda: disc.logtext().count("roles player metadata artwork controller") >= 3) is not None)

        # ---- 4. unpair, then QR-code pairing
        await server.untrust_unpaired(cid)
        await server.unpair(cid)
        check("server/unpair drops the record", await wait_for(lambda: not disc.keys().get("records")) is not None)
        cl = await wait_for(lambda: (lambda c: c if c and c.connection and "unpaired" in disc.logtext() else None)(server.get_client(cid)), 20)
        await asyncio.sleep(1.0)

        async def provide_qr():
            return await wait_for(lambda: shown_code(d), 20)
        try:
            await asyncio.wait_for(server.get_client(cid).connection.initiate_pairing(PairingAttempt(method=PairMethod.DYNAMIC_PAIRING_CODE,
                                   pairing_code_provider=provide_qr, pairing_format=PairingCodeFormat.QR_CODE)), 60)
            ok = True
        except Exception as e:  # noqa: BLE001
            ok = False; print("   pairing error:", repr(e))
        check("QR-code pairing", ok, disc.logtext()[-800:])

        # ---- 5. unpair, then the Pairing PSK (token)
        await server.unpair(cid)
        await wait_for(lambda: not disc.keys().get("records"))
        await asyncio.sleep(4.5)
        k = disc.keys()
        try:
            await asyncio.wait_for(server.initiate_pairing(cid, PairingAttempt(method=PairMethod.PAIRING_PSK,
                                   pairing_psk=k["pairing"], client_id=cid)), 60)
            ok = True
        except Exception as e:  # noqa: BLE001
            ok = False; print("   pairing error:", repr(e))
        check("Pairing PSK (token) pairing", ok and await wait_for(lambda: disc.keys().get("records")) is not None, disc.logtext()[-800:])

        # ---- 6. Allow Unpaired off: a fresh, unpaired Disc can't be made to play
        disc.stop()
        d2 = tempfile.mkdtemp(prefix="ss-interop-")
        disc2 = Disc(d2, unpaired=0); disc2.start()
        cl2 = await wait_for(lambda: next((c for c in server.connected_clients if c.client_id != cid), None))
        if cl2:
            await server.trust_unpaired(cl2.client_id)
        check("Allow Unpaired off: no playback for an unpaired Disc", await wait_for(lambda: "goodbye: pairing_required" in disc2.logtext() or "activate: activities," in disc2.logtext() and "roles player" not in disc2.logtext(), 15) is not None, disc2.logtext()[-600:])
        disc2.stop()
    finally:
        disc.stop()
        await server.close()
        print("logs in", d)
    print(f"\n{fails} FAILED" if fails else "\nall passed")
    return 1 if fails else 0

sys.exit(asyncio.run(main()))
