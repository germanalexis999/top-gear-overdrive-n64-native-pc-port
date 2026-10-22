# Publication workflow

User preference recorded 2026-10-22: whenever asked to push this project, also
publish its matching GitHub prerelease. A separate release request is unnecessary.
Include the portable Windows ZIP, SHA-256 checksum, DLLs and license notices;
the package must identify the same clean source commit pushed to main.
Use the next available preview version and the existing packaging/publication
tools. Preserve existing published releases. Do not finish after pushing only
source unless publication is blocked; report the concrete blocker in that case.
Keep EXE/DLL/ZIP as release assets, outside source history. Never include ROMs,
extracted game data, captures, saves or credentials. Keep candidate fixes and
pending verification explicit in release notes. Do not claim tests were run
unless they were actually executed under the applicable session instructions.
This preference does not install a Git hook or a GitHub Actions workflow.
