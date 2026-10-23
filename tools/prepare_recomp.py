#!/usr/bin/env python3
"""Generate local game C from a verified user ROM and apply port scheduling hooks."""
from __future__ import annotations

import argparse
import hashlib
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHA1 = "75ebbe451906ce68b0dd43eef9ee44416452a4f1"
# Polling loops must let host interrupts wake other cooperative N64 threads.
POLL_LABELS = ("800009FC", "80000A74", "80006214", "80006684", "800072BC", "80009250")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", type=Path,
        default=ROOT / "top_gear_rom/normalized/Top Gear Overdrive (USA).z64")
    parser.add_argument("--recompiler", type=Path,
        default=ROOT / "tools/N64Recomp/build/N64Recomp.exe")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "generated/RecompiledFuncs")
    args = parser.parse_args()
    rom = args.rom.resolve()
    if hashlib.sha1(rom.read_bytes()).hexdigest() != SHA1:
        raise SystemExit("Unsupported ROM revision or byte order: expected normalized USA v0.")
    output = args.output_dir.resolve()
    if not output.is_relative_to(ROOT) or output == ROOT:
        raise SystemExit("Generated output must be a subdirectory of this project.")
    build = ROOT / "build"
    build.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="recomp-", dir=build) as scratch_name:
        scratch = Path(scratch_name).resolve()
        if not scratch.is_relative_to(build.resolve()):
            raise SystemExit("Invalid temporary build path.")
        generated = scratch / "generated"
        config = (ROOT / "config/topgearoverdrive.us.v0.toml").read_text(encoding="utf-8")
        replacements = {
            "symbols_file_path": ROOT / "symbols/topgearoverdrive.us.v0.candidates.toml",
            "rom_file_path": rom,
            "output_func_path": generated,
        }
        for key, path in replacements.items():
            config, count = re.subn(rf'(?m)^{key}\s*=.*$', f'{key} = "{path.as_posix()}"', config)
            if count != 1:
                raise SystemExit(f"Expected one {key} configuration entry.")
        config_path = scratch / "config.toml"
        config_path.write_text(config, encoding="utf-8")
        subprocess.run([str(args.recompiler.resolve()), str(config_path)], cwd=ROOT, check=True)
        sources = {p: p.read_text(encoding="utf-8") for p in generated.glob("funcs_*.c")}
        for callback in ("tgod_apply_original_resolution(rdram);", "tgod_extend_camera(rdram, ctx);",
                         "tgod_select_high_detail(rdram, ctx);", "tgod_extend_track_visibility(rdram, ctx);"):
            if sum(text.count(callback) for text in sources.values()) != 1:
                raise SystemExit(f"Graphics hook missing or duplicated in generator output: {callback}")
        for label in POLL_LABELS:
            marker = f"L_{label}:\n"
            owners = [p for p, text in sources.items() if marker in text]
            if len(owners) != 1 or sources[owners[0]].count(marker) != 1:
                raise SystemExit(f"Polling hook {label} no longer matches the generator output.")
            path = owners[0]
            sources[path] = sources[path].replace(marker, marker + "    yield_self_1ms(rdram);\n")
        for path, text in sources.items():
            path.write_text(text, encoding="utf-8", newline="\n")
        output.mkdir(parents=True, exist_ok=True)
        files = [p for p in generated.iterdir() if p.is_file()]
        names = {p.name for p in files}
        for stale in output.glob("funcs_*.c"):
            if stale.name not in names:
                stale.unlink()
        for path in files:
            destination = output / path.name
            if not destination.exists() or destination.read_bytes() != path.read_bytes():
                shutil.copyfile(path, destination)
        print(f"Generated {len(sources)} C files; installed {len(POLL_LABELS)} cooperative polling hooks.")


if __name__ == "__main__":
    main()
