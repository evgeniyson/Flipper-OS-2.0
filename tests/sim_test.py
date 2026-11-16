#!/usr/bin/env python3
"""End-to-end tests: drive the firmware UI in the simulator with key presses
and check what it transmitted and what it wrote to the (fake) SD card.

    tests/sim_test.py path/to/fos_sim
"""
import os
import shutil
import subprocess
import sys
import tempfile

SIM = os.path.abspath(sys.argv[1])
failures = 0

PRINCETON_KEY = """Filetype: Flipper SubGhz Key File
Version: 1
Frequency: {freq}
Preset: FuriHalSubGhzPresetOok650Async
Protocol: {proto}
Bit: {bits}
Key: 00 00 00 00 00 95 D5 D4
TE: 400
"""

# menu order: Receiver, Spectrum, Frequency finder, RAW recorder, Saved, Settings
OPEN_SAVED = ["down", "down", "down", "down", "ok"]
OPEN_RECORDER = ["down", "down", "down", "ok"]
OPEN_SETTINGS = ["up", "ok"]
SEND_FIRST_FILE = OPEN_SAVED + ["ok", "wait:300", "ok", "wait:1500"]


def check(name, cond, detail=""):
    global failures
    print(("ok   " if cond else "FAIL ") + name + ("" if cond else "  " + str(detail)))
    if not cond:
        failures += 1


class Run:
    """One simulator session with its own SD card directory."""

    def __init__(self, files=None, region=None):
        self.dir = tempfile.mkdtemp(prefix="fos_sim_")
        self.sd = os.path.join(self.dir, "sd")
        os.makedirs(os.path.join(self.sd, "subghz"))
        os.makedirs(os.path.join(self.sd, "fos"))
        if region is not None:
            self.write("fos/settings.txt", "region=%d\n" % region)
        for name, text in (files or {}).items():
            self.write("subghz/" + name, text)

    def write(self, rel, text):
        with open(os.path.join(self.sd, rel), "w") as f:
            f.write(text)

    def read(self, rel):
        path = os.path.join(self.sd, rel)
        return open(path).read() if os.path.exists(path) else None

    def go(self, *commands):
        env = dict(os.environ, FOS_SD=self.sd, FOS_OUT=self.dir)
        p = subprocess.run([SIM] + list(commands), env=env, capture_output=True, text=True, timeout=120)
        self.output = p.stdout + p.stderr
        check("simulator exits cleanly", p.returncode == 0, self.output[-400:])
        return self

    def tx(self):
        """List of transmissions: (header, [pulses])."""
        path = os.path.join(self.dir, "tx.log")
        if not os.path.exists(path):
            return []
        result = []
        for line in open(path).read().split("\n"):
            if line.startswith("TX "):
                result.append((line, []))
            elif line and line != "END":
                result[-1][1].append(int(line))
        return result

    def close(self):
        shutil.rmtree(self.dir)


def raw_pulses(text):
    out = []
    for line in text.split("\n"):
        if line.startswith("RAW_Data:"):
            out += [int(v) for v in line.split(":")[1].split()]
    return out


def princeton_frame(key, te):
    frame = []
    for i in range(23, -1, -1):
        frame += [3 * te, -te] if key >> i & 1 else [te, -3 * te]
    return frame + [te, -30 * te]


def test_send_key():
    key_file = PRINCETON_KEY.format(freq=433920000, proto="Princeton", bits=24)
    r = Run({"a.sub": key_file}, region=1).go(*SEND_FIRST_FILE)
    tx = r.tx()
    check("key file is transmitted once", len(tx) == 1, tx)
    if tx:
        check("on its frequency and modulation",
              tx[0][0] == "TX 433920000 FuriHalSubGhzPresetOok650Async INT", tx[0][0])
        check("as 8 correct Princeton frames", tx[0][1] == princeton_frame(0x95D5D4, 400) * 8, tx[0][1][:60])
    r.close()


def test_region_gate():
    key_file = PRINCETON_KEY.format(freq=433920000, proto="Princeton", bits=24)
    r = Run({"a.sub": key_file}).go(*SEND_FIRST_FILE)
    check("nothing is sent while no region is selected", r.tx() == [])
    r.close()

    us_file = PRINCETON_KEY.format(freq=315000000, proto="Princeton", bits=24)
    r = Run({"a.sub": us_file}, region=1).go(*SEND_FIRST_FILE)
    check("315 MHz is refused in the EU/RU region", r.tx() == [])
    r.close()
    r = Run({"a.sub": us_file}, region=2).go(*SEND_FIRST_FILE)
    check("315 MHz is sent in the US region", len(r.tx()) == 1)
    r.close()


def test_rolling_never_sent():
    key_file = PRINCETON_KEY.format(freq=433920000, proto="KeeLoq", bits=64)
    r = Run({"a.sub": key_file}, region=1).go(*SEND_FIRST_FILE)
    check("a rolling-code key file is never transmitted", r.tx() == [])
    r.close()


def test_send_raw(vector):
    text = open(vector).read()
    r = Run({"a.sub": text}, region=1).go(*(OPEN_SAVED + ["ok", "wait:300", "ok", "wait:4000"]))
    tx = r.tx()
    check("RAW file is transmitted", len(tx) == 1, tx)
    if tx:
        check("pulse for pulse", tx[0][1] == raw_pulses(text), (len(tx[0][1]), len(raw_pulses(text))))
    r.close()


def test_external_module():
    key_file = PRINCETON_KEY.format(freq=433920000, proto="Princeton", bits=24)
    r = Run({"a.sub": key_file}, region=1).go("external:1", *SEND_FIRST_FILE)
    tx = r.tx()
    check("with a module attached the external radio is used", len(tx) == 1 and tx[0][0].endswith(" EXT"), tx)
    r.close()


def test_record(vector):
    text = open(vector).read()
    r = Run().go(*(OPEN_RECORDER + ["wait:200", "ok", "inject:" + vector, "wait:4000", "ok", "wait:300"]))
    rec = r.read("subghz/RAW_0001.sub")
    check("recording creates RAW_0001.sub", rec is not None)
    if rec:
        check("with a valid header", rec.startswith("Filetype: Flipper SubGhz RAW File\nVersion: 1\n"
                                                    "Frequency: 433920000\n"), rec[:120])
        check("containing every received pulse", raw_pulses(rec) == raw_pulses(text),
              (len(raw_pulses(rec)), len(raw_pulses(text))))
    r.close()


def test_settings_persist():
    r = Run().go(*(OPEN_SETTINGS + ["right", "back", "wait:200"]))
    saved = r.read("fos/settings.txt") or ""
    check("the chosen region is stored on the SD card", "region=1\n" in saved, saved)
    r.close()


def main():
    vectors = os.path.join(os.path.dirname(os.path.abspath(__file__)), "vectors")
    vector = os.path.join(vectors, "princeton_raw.sub")
    test_send_key()
    test_region_gate()
    test_rolling_never_sent()
    test_external_module()
    test_settings_persist()
    if os.path.exists(vector):
        test_send_raw(vector)
        test_record(vector)
    else:
        print("skip RAW tests: run tools/fetch_test_vectors.sh")
    print("%d failed" % failures)
    sys.exit(1 if failures else 0)


main()
