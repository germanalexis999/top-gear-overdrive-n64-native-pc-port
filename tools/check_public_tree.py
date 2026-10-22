#!/usr/bin/env python3
"""Audit the Git index against the public port source allowlist."""
import re
import argparse
import subprocess
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parents[1]
EXACT = {"AGENTS.md", "README.md", "CMakeLists.txt", ".gitignore", ".gitattributes",
         "dependencies.lock.json", "THIRD_PARTY_NOTICES.md",
         "src/main.cpp", "src/f3dex2.cpp", "include/f3dex2.hpp",
         "config/topgearoverdrive.us.v0.toml", "symbols/topgearoverdrive.us.v0.candidates.toml",
         "docs/BUILDING.md", "docs/STATUS.md",
         "tools/normalize_n64_rom.ps1", "tools/prepare_recomp.py", "tools/bootstrap.ps1",
         "tools/check_public_tree.py", "tools/renderer_regression_checks.cpp",
         "patches/n64modernruntime.patch", "patches/n64recomp.patch",
         "patches/runtime-recomp-header.patch", "LICENSES/N64Recomp-MIT.txt",
         "LICENSES/RT64-MIT.txt", "LICENSES/N64ModernRuntime-GPL-3.0.txt"}
EXACT.update({"LICENSE", "include/rom_loader.hpp", "src/rom_loader.cpp",
              "include/vi_scanout.hpp",
              "include/render_surface.hpp", "include/controller_input.hpp",
              "include/sdl_input.hpp", "src/sdl_input.cpp",
              "include/controller_profiles.hpp", "include/controller_mapper.hpp",
              "src/controller_profiles.cpp", "src/controller_mapper.cpp",
              "include/gpu_raster.hpp", "src/gpu_raster.cpp", "src/gpu_raster_shader.inc",
              "src/gpu_present_shader.inc",
              "tools/gpu_regression_checks.cpp", "tools/compile_gpu_shader.cpp",
              "tools/package_windows.py", "tools/publish_release.py", "tools/test_windows_package.py",
              "tools/release_windows.ps1",
              "docs/RELEASES.md", "docs/RELEASE-NOTES.md",
              "docs/DIAGNOSTICS-2026-10-21.md",
              "docs/RENDERER-ARCHITECTURE.md", "docs/CONTROLLER-MAPPING-RESEARCH.md", "docs/PHYSICS-INVESTIGATION-2026-10-22.md",
              "packaging/README-WINDOWS.txt", "packaging/Jugar.cmd"})
EXACT.update("LICENSES/" + name + ".txt" for name in (
    "miniz", "o1heap", "xxHash", "fmt", "rabbitizer", "ELFIO", "tomlplusplus",
    "sljit", "concurrentqueue", "GCC-RUNTIME-EXCEPTION", "winpthreads",
    "mingw-w64-runtime", "nlohmann-json", "SDL3-zlib"))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--worktree",action="store_true",help="Audit current tracked and untracked source before staging")
    args=parser.parse_args()
    command=["git","ls-files","--cached","-z"]
    if args.worktree: command=["git","ls-files","--cached","--others","--exclude-standard","-z"]
    files = subprocess.check_output(command, cwd=ROOT).decode().split("\0")
    errors = []
    for name in filter(None, files):
        if name not in EXACT or ".." in PurePosixPath(name).parts:
            errors.append(f"Unapproved file: {name}")
            continue
        blob = (ROOT/name).read_bytes() if args.worktree else subprocess.check_output(["git", "show", ":" + name], cwd=ROOT)
        if b"\0" in blob or len(blob) > 512 * 1024:
            errors.append(f"Binary or oversized file: {name}")
        text = blob.decode("utf-8", errors="replace")
        if re.search(r"-----BEGIN (?:RSA |OPENSSH |EC )?PRIVATE KEY-----|gh[pousr]_[A-Za-z0-9]{30,}", text):
            errors.append(f"Possible private credential: {name}")
        if re.search(r"(?i)[A-Z]:[\\/]Users[\\/]", text):
            errors.append(f"Private local user path: {name}")
    if errors:
        raise SystemExit("\n".join(errors))
    if len(list(filter(None, files))) != len(EXACT):
        raise SystemExit("The public source manifest is incomplete.")
    scope="working-tree" if args.worktree else "indexed"
    print(f"PASS: {len(EXACT)} {scope} text files; allowlist, binary, credential and local-path checks.")


if __name__ == "__main__":
    main()
