#!/usr/bin/env python3
"""Check a portable ZIP with Windows-only PATH and a private local test ROM."""
import argparse
import os
import re
import subprocess
import tempfile
import zipfile
from pathlib import Path
from package_windows import ROOT, audit_archive


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--rom", type=Path, required=True)
    args = parser.parse_args()
    audit_archive(args.archive)
    rom = args.rom.resolve()
    data = rom.read_bytes()
    if len(data) != 12 * 1024 * 1024 or data[:4] != bytes.fromhex("80371240"):
        raise SystemExit("Use the supported normalized local test ROM")
    environment = dict(os.environ)
    windows = Path(environment.get("SystemRoot", "C:/Windows"))
    environment["PATH"] = str(windows / "System32") + os.pathsep + str(windows)
    with tempfile.TemporaryDirectory(prefix="package-test-", dir=ROOT / "build") as temporary:
        temporary = Path(temporary)
        with zipfile.ZipFile(args.archive) as package:
            package.extractall(temporary)
        app = next(p for p in temporary.iterdir() if p.is_dir())
        executable = app / "tgod_native.exe"
        cwd = temporary / "different-working-directory"
        cwd.mkdir()

        def run(arguments, expected=0, timeout=10):
            result = subprocess.run([str(executable), *map(str, arguments)], cwd=cwd, env=environment,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
            text = result.stdout.decode(errors="replace")
            if result.returncode != expected:
                raise RuntimeError(f"Unexpected exit {result.returncode}: {text[-1000:]}")
            return text

        run(["--help"])
        run(["--headless", "--seconds", "1"], 2)
        run(["--unknown"], 2)
        run(["--graphics-check"], 2)
        run(["--intro-check"], 2)
        run(["--headless", "--intro-check", "--graphics-check"], 2)
        run(["--render-height", "999"], 2)
        run(["--render-height"], 2)
        run(["--resolution-check"], 2)
        run(["--presentation-check"], 2)
        run(["--headless", "--presentation-check", "--resolution-check"], 2)
        run(["--headless", "--resolution-check", "--intro-check"], 2)
        run(["--headless", "--resolution-check", "--render-height", "1080"], 2)
        run(["--verify-rom", "--rom", rom])
        cases = temporary / "ROM con espacios y acentos á"
        cases.mkdir()
        for suffix, stride in (("v64", 2), ("n64", 4)):
            swapped = bytearray(data)
            if stride == 2:
                swapped[0::2], swapped[1::2] = data[1::2], data[0::2]
            else:
                for i in range(4): swapped[i::4] = data[3 - i::4]
            path = cases / ("ROM de prueba." + suffix)
            path.write_bytes(swapped)
            run(["--verify-rom", "--rom", path])
        wrong_size = cases / "truncated.z64"
        wrong_size.write_bytes(data[:4])
        run(["--verify-rom", wrong_size], 2)
        wrong_revision = cases / "modified.z64"
        modified = bytearray(data)
        modified[0x1000] ^= 1
        wrong_revision.write_bytes(modified)
        run(["--verify-rom", wrong_revision], 2)
        log = run(["--headless", "--seconds", "16", "--rom", rom], timeout=40)
        required = ["Mode: 2", "Result: 4 / 4", "Audio queue overruns / silent inputs discarded / pending frames replaced: 0/0/0",
                    "NVRAM EEPROM save verified"]
        if not all(marker in log for marker in required):
            raise RuntimeError("Packaged race/audio/save check failed:\n" + log[-2500:])
        (ROOT / "build/package-test.log").write_text(log, encoding="utf-8")
        hd_log=run(["--headless","--render-height","1080","--seconds","20","--rom",rom],timeout=45)
        if not all(marker in hd_log for marker in required) or not re.search(r"\[HD worker\] Completed: [1-9]\d*",hd_log):
            raise RuntimeError("Packaged asynchronous HD/native timing check failed:\n"+hd_log[-2500:])
        if not re.search(r"\[Native render\].*scale: 1(?:\.0+)?\.",hd_log):
            raise RuntimeError("HD changed the authoritative native rendering path")
        if "[HD GPU] Direct3D 11 hardware:" in hd_log:
            if not re.search(r"\[HD GPU\] Hardware frames: [1-9]\d*",hd_log) or "Replay failed:" in hd_log:
                raise RuntimeError("Hardware GPU was detected but the packaged HD replay failed")
            dxgi_log=run(["--headless","--presentation-check","--graphics-check",
                          "--render-height","4k","--seconds","24","--rom",rom],timeout=60)
            if (not all(marker in dxgi_log for marker in required)
                or not re.search(r"\[DXGI\] Presents: [1-9]\d*",dxgi_log)
                or not re.search(r"resizes: [1-9]\d*, resident HD frames: [1-9]\d*, final CPU readbacks: 0\.",dxgi_log)
                or not re.search(r"\[Native render\].*scale: 1(?:\.0+)?\.",dxgi_log)
                or "Presentation failed:" in dxgi_log or "Replay failed:" in dxgi_log):
                raise RuntimeError("Packaged DXGI/native timing check failed:\n"+dxgi_log[-3000:])
            (ROOT/"build/package-test-dxgi.log").write_text(dxgi_log,encoding="utf-8")
            print("PASS: DXGI presentation, native/720p/1080p/4K switches, resize/fullscreen/filter, no final framebuffer readback")
        (ROOT / "build/package-test-hd.log").write_text(hd_log,encoding="utf-8")
        print("PASS: Windows-only PATH, included DLLs, Unicode paths, 3 ROM byte orders, invalid ROM rejection, race/audio/save")
        print("PASS: asynchronous 1080p output with native game timing, race, audio and save")
    # The tested extraction is discarded, never repackaged with its private data.


if __name__ == "__main__":
    main()
