#!/usr/bin/env python3
"""Publish an audited Windows ZIP to this project's GitHub Releases."""
import argparse
import hashlib
import json
import os
import subprocess
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path
from package_windows import audit_archive

ROOT = Path(__file__).resolve().parents[1]
REPOSITORY = "germanalexis999/top-gear-overdrive-n64-native-pc-port"
API = "https://api.github.com/repos/" + REPOSITORY


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--notes", type=Path, default=ROOT / "docs/RELEASE-NOTES.md")
    parser.add_argument("--publish", action="store_true", help="Explicitly authorize creating/publishing the release")
    args = parser.parse_args()
    archive = args.archive.resolve()
    manifest = audit_archive(archive)
    if manifest.get("draft") or manifest.get("dirty_source"):
        raise SystemExit("A local draft package cannot be published")
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    remote = subprocess.check_output(["git", "remote", "get-url", "origin"], cwd=ROOT, text=True).strip()
    if remote.removesuffix(".git") != "https://github.com/" + REPOSITORY:
        raise SystemExit("Unexpected publication repository")
    if commit != manifest["source_commit"] or subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT):
        raise SystemExit("Package source must match the clean current commit")
    checksum = archive.with_suffix(".zip.sha256")
    expected_hash = hashlib.sha256(archive.read_bytes()).hexdigest()
    if checksum.read_text().strip() != f"{expected_hash}  {archive.name}":
        raise SystemExit("Archive checksum file is missing or mismatched")
    if not args.publish:
        print(f"Ready: {manifest['version']} at {commit}; audited ZIP and checksum. Use --publish to upload.")
        return

    # Use Git's configured credential helper. Capture credentials only in memory;
    # never write or print them, and send them only to GitHub's API/upload hosts.
    environment = dict(os.environ, GIT_TERMINAL_PROMPT="0")
    credential = subprocess.run(["git", "credential", "fill"], input="protocol=https\nhost=github.com\n\n",
                                text=True, capture_output=True, env=environment, check=True)
    fields = dict(line.split("=", 1) for line in credential.stdout.splitlines() if "=" in line)
    token = fields.get("password")
    if not token: raise SystemExit("Git credential helper did not provide GitHub authentication")

    def request(url, method="GET", data=None, content_type="application/json"):
        parsed = urllib.parse.urlparse(url)
        if parsed.scheme != "https" or parsed.hostname not in {"api.github.com", "uploads.github.com"}:
            raise RuntimeError("Unexpected authenticated endpoint")
        headers = {"Authorization": "Bearer " + token, "Accept": "application/vnd.github+json",
                   "User-Agent": "tgod-port-release", "X-GitHub-Api-Version": "2026-03-10"}
        if data is not None: headers["Content-Type"] = content_type
        body = json.dumps(data).encode() if isinstance(data, dict) else data
        with urllib.request.urlopen(urllib.request.Request(url, data=body, headers=headers, method=method), timeout=60) as response:
            return json.load(response)

    tag = manifest["version"]
    # Check the source commit is available remotely before publishing artifacts.
    remote_commit = request(API + "/commits/" + commit)
    if remote_commit["sha"] != commit: raise SystemExit("Source commit is not available on GitHub")
    try:
        release = request(API + "/releases/tags/" + urllib.parse.quote(tag, safe=""))
    except urllib.error.HTTPError as error:
        if error.code != 404: raise
        release = request(API + "/releases", "POST", {
            "tag_name": tag, "target_commitish": commit,
            "name": "Top Gear Overdrive " + tag + " - Windows x64",
            "body": args.notes.read_text(encoding="utf-8"), "draft": True, "prerelease": True})
    if release["target_commitish"] not in (commit, "main"):
        raise SystemExit("Release tag targets another source revision")
    upload_url = release["upload_url"].split("{")[0]
    existing = {asset["name"]: asset for asset in release["assets"]}
    for file in (archive, checksum):
        digest = "sha256:" + hashlib.sha256(file.read_bytes()).hexdigest()
        if file.name in existing:
            asset = existing[file.name]
        else:
            if not release["draft"]: raise SystemExit("Will not change assets of an already published release")
            asset = request(upload_url + "?name=" + urllib.parse.quote(file.name), "POST", file.read_bytes(),
                            "application/zip" if file == archive else "text/plain")
        if asset["state"] != "uploaded" or asset["size"] != file.stat().st_size or asset.get("digest") != digest:
            raise SystemExit("Uploaded asset size/hash verification failed; release remains a draft")
        print("Verified asset:", asset["name"], asset["digest"])
    if release["draft"]:
        release = request(API + "/releases/" + str(release["id"]), "PATCH", {"draft": False, "prerelease": True})
    print("Published:", release["html_url"])


if __name__ == "__main__":
    try:
        main()
    except urllib.error.HTTPError as error:
        raise SystemExit(f"GitHub API returned HTTP {error.code}; no authentication data was printed") from None
