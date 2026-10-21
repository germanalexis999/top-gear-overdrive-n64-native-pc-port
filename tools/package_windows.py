#!/usr/bin/env python3
"""Create an allowlisted portable release without ROMs, assets, saves or captures."""
from __future__ import annotations
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REPO_URL = "https://github.com/germanalexis999/top-gear-overdrive-n64-native-pc-port"
RUNTIME_DLLS = {"libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll"}
WINDOWS_DLLS = {"advapi32.dll", "bcrypt.dll", "comdlg32.dll", "gdi32.dll",
               "kernel32.dll", "msvcrt.dll", "ntdll.dll", "ole32.dll", "shell32.dll",
               "user32.dll", "ucrtbase.dll", "winmm.dll", "ws2_32.dll"}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def pe_imports(path: Path, objdump: Path) -> list[str]:
    result = subprocess.check_output([str(objdump), "-p", str(path)], text=True, errors="replace")
    if "pei-x86-64" not in result:
        raise RuntimeError(f"Expected a 64-bit Windows PE: {path.name}")
    return sorted(set(re.findall(r"DLL Name:\s*(\S+)", result)), key=str.lower)


def audit_archive(archive: Path) -> dict:
    with zipfile.ZipFile(archive) as package:
        names = package.namelist()
        if not names or len(names) != len(set(names)):
            raise RuntimeError("Empty package or duplicate archive members")
        roots = {name.split("/")[0] for name in names}
        if len(roots) != 1:
            raise RuntimeError("Package must contain one portable directory")
        prefix = next(iter(roots)) + "/"
        relative = {name[len(prefix):]: name for name in names}
        required = {"tgod_native.exe", "Jugar.cmd", "README.txt", "LICENSE", "SOURCE.md",
                    "THIRD_PARTY_NOTICES.md", "PACKAGE-MANIFEST.json", *RUNTIME_DLLS}
        if not required <= relative.keys():
            raise RuntimeError("Required program, runtime, instructions or licenses are missing")
        manifest = json.loads(package.read(relative["PACKAGE-MANIFEST.json"]))
        expected = set(manifest["files"]) | {"PACKAGE-MANIFEST.json"}
        if expected != relative.keys():
            raise RuntimeError("Package manifest does not match archive members")
        for name, info in manifest["files"].items():
            if ".." in Path(name).parts or "\\" in name or name.startswith("/"):
                raise RuntimeError("Unsafe archive member")
            if name not in required and not (name.startswith("LICENSES/") and name.endswith(".txt")):
                raise RuntimeError(f"Unapproved release file: {name}")
            data = package.read(relative[name])
            if hashlib.sha256(data).hexdigest() != info["sha256"] or len(data) != info["bytes"]:
                raise RuntimeError(f"Hash/size mismatch: {name}")
            if data[:4] in (b"\x80\x37\x12\x40", b"\x37\x80\x40\x12", b"\x40\x12\x37\x80"):
                raise RuntimeError("ROM header found in release file")
        return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", default="v0.1.0-preview.1")
    parser.add_argument("--exe", type=Path, default=ROOT / "build/tgod_native.exe")
    parser.add_argument("--toolchain-bin", type=Path)
    parser.add_argument("--output-dir", type=Path, default=ROOT / "dist")
    parser.add_argument("--draft", action="store_true", help="Mark as local/unpublishable, allowing dirty source")
    parser.add_argument("--audit", type=Path, help="Audit an existing ZIP only")
    args = parser.parse_args()
    if args.audit:
        manifest = audit_archive(args.audit)
        print(f"PASS: {len(manifest['files'])} hashed files; executable, runtime DLLs and notices only")
        return
    if not re.fullmatch(r"v\d+\.\d+\.\d+(?:-[a-zA-Z0-9.]+)?", args.version):
        raise SystemExit("Use a semantic version tag such as v0.1.0-preview.1")
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    dirty = bool(subprocess.check_output(["git", "status", "--porcelain", "--untracked-files=no"], cwd=ROOT))
    if dirty and not args.draft:
        raise SystemExit("Commit reviewed source changes before creating a publishable release")
    if not (ROOT / "LICENSE").is_file():
        raise SystemExit("Choose and record the port license before packaging")
    compiler = Path(shutil.which("g++") or "")
    toolchain = args.toolchain_bin or compiler.parent
    objdump = toolchain / "objdump.exe"
    if not objdump.is_file():
        raise SystemExit("Cannot locate the matching MinGW objdump.exe")
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    stem = f"top-gear-overdrive-{args.version}-windows-x64"
    archive = output / (stem + ".zip")
    if archive.exists():
        raise SystemExit("Release archive already exists; preserve it or choose another output directory/version")
    with tempfile.TemporaryDirectory(prefix="package-", dir=output) as temporary:
        stage = Path(temporary) / stem
        stage.mkdir()
        shutil.copyfile(args.exe, stage / "tgod_native.exe")
        imports = {}
        pending = ["tgod_native.exe"]
        while pending:
            name = pending.pop()
            dependencies = pe_imports(stage / name, objdump)
            imports[name] = dependencies
            for dependency in dependencies:
                lower = dependency.lower()
                if lower in WINDOWS_DLLS or lower.startswith(("api-ms-win-", "ext-ms-win-")):
                    continue
                if lower not in RUNTIME_DLLS:
                    raise SystemExit(f"Unexpected non-system DLL import: {dependency}")
                target = stage / lower
                if not target.exists():
                    shutil.copyfile(toolchain / lower, target)
                    pending.append(lower)
        if not RUNTIME_DLLS <= {p.name for p in stage.iterdir()}:
            raise SystemExit("Unexpected MinGW runtime dependency set")
        shutil.copyfile(ROOT / "packaging/Jugar.cmd", stage / "Jugar.cmd")
        shutil.copyfile(ROOT / "packaging/README-WINDOWS.txt", stage / "README.txt")
        shutil.copyfile(ROOT / "LICENSE", stage / "LICENSE")
        shutil.copyfile(ROOT / "THIRD_PARTY_NOTICES.md", stage / "THIRD_PARTY_NOTICES.md")
        (stage / "LICENSES").mkdir()
        for license_file in sorted((ROOT / "LICENSES").glob("*.txt")):
            shutil.copyfile(license_file, stage / "LICENSES" / license_file.name)
        source = ["# Source and build information", "", f"Version: {args.version}",
                  f"Port source: {REPO_URL}/tree/{commit}",
                  f"Source archive: {REPO_URL}/archive/{commit}.zip", "",
                  "Build instructions: docs/BUILDING.md in the matching source tree.",
                  "Local changes to dependencies are in patches/. Dependencies are fetched separately at these exact revisions:", ""]
        lock = json.loads((ROOT / "dependencies.lock.json").read_text())
        for dependency in lock["repositories"]:
            source.append(f"- {dependency['url']} at {dependency['commit']}; patch: {dependency['patch']}")
        source += ["", "Compiler runtime: GCC 16.2.0 (GPLv3 with GCC Runtime Library Exception 3.1).",
                   "GCC source: https://gcc.gnu.org/git/?p=gcc.git;a=tree;hb=releases/gcc-16.2.0",
                   "MinGW-w64/winpthreads 14.0.0 source: https://github.com/mingw-w64/mingw-w64/tree/v14.0.0",
                   "Toolchain distribution: https://winlibs.com/", "",
                   "The game code is generated locally from a user-supplied supported ROM.",
                   "No ROM, extracted assets, game data files or saves are provided in this package."]
        (stage / "SOURCE.md").write_text("\n".join(source) + "\n", encoding="utf-8")
        files = {p.relative_to(stage).as_posix(): {"bytes": p.stat().st_size, "sha256": sha256(p)}
                 for p in sorted(stage.rglob("*")) if p.is_file()}
        manifest = {"version": args.version, "source_commit": commit, "draft": args.draft,
                    "dirty_source": dirty, "files": files, "pe_imports": imports,
                    "windows_system_dlls_included": False, "rom_included": False}
        (stage / "PACKAGE-MANIFEST.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as package:
            for path in sorted(stage.rglob("*")):
                if path.is_file(): package.write(path, path.relative_to(stage.parent).as_posix())
    audit_archive(archive)
    checksum = sha256(archive)
    archive.with_suffix(".zip.sha256").write_text(f"{checksum}  {archive.name}\n", encoding="ascii")
    print(f"Created {archive.name}: {archive.stat().st_size} bytes, SHA256 {checksum}")
    print(f"PASS: recursive PE imports and {len(files)} allowlisted, hashed files; no ROM/assets/saves/captures")


if __name__ == "__main__":
    main()
