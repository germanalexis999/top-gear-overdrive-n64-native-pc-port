#!/usr/bin/env python3
"""Check a portable ZIP with Windows-only PATH and a private local test ROM."""
import argparse
import os
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
        print("PASS: Windows-only PATH, included DLLs, Unicode paths, 3 ROM byte orders, invalid ROM rejection, race/audio/save")
    # The tested extraction is discarded, never repackaged with its private data.


if __name__ == "__main__":
    main()
