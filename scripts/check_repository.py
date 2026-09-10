#!/usr/bin/env python3
"""Offline checks for public source files. No device or network access."""

import argparse
import json
import os
from pathlib import Path
import re
import sys
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]
ROOT_FILES = (
    ".editorconfig", ".gitattributes", ".gitignore", "AUTHORS.md",
    "CHANGELOG.md", "CMakeLists.txt", "CMakePresets.json", "CODE_OF_CONDUCT.md",
    "CONTRIBUTING.md", "LICENSE", "README.md", "README.en.md", "SECURITY.md",
    "THIRD_PARTY_NOTICES.md", "VALIDATION.md", "install.sh", "uninstall.sh",
    "einspielen-v1.05.sh", "einspielen-v1.06.sh",
    "vorschau.png", "alarme-vorschau.png", "diagnose-vorschau.png",
    "embleme-vorschau.png", "filter-vorschau.png", "power-vorschau.png",
)
SOURCE_DIRS = (".github", "assets", "config", "docs", "examples", "scripts", "src", "tests", "third_party")
REQUIRED = ROOT_FILES + (
    ".github/workflows/ci.yml", ".github/workflows/release.yml",
    ".github/dependabot.yml", ".github/ISSUE_TEMPLATE/bug_report.yml",
    ".github/ISSUE_TEMPLATE/feature_request.yml", ".github/pull_request_template.md",
    "docs/DEVELOPMENT.md", "docs/GITHUB_SETUP.md", "docs/ARCHITECTURE.md",
    "docs/USER_GUIDE.de.md", "docs/LUA_AUTOMATION.md",
    "docs/IPC_PROTOCOL.md",
    "config/airctrld.cfg",
    "scripts/v1.05-resume.json",
    "scripts/check_v105_resume.py",
    "docs/PROTOCOL_VALIDATION_2026-09-08.md",
    "docs/images/README.md", "docs/images/desklet-dark.png",
    "docs/images/desklet-light.png", "docs/images/diagnostics-demo.png",
    "docs/images/alarms-demo.png", "src/version.hpp.in", "src/automation_example.hpp.in",
    "third_party/aioairctrl/LICENSE", "third_party/aioairctrl/ORIGIN.md",
    "third_party/lua/LICENSE", "third_party/lua/ORIGIN.md", "third_party/lua/src/lua.h",
    "examples/automation.lua", "tests/test_automation.cpp",
    "scripts/package_source.py", "tests/test_repository.py",
)
ALLOWED_SUFFIXES = {".md", ".c", ".h", ".cpp", ".hpp", ".in", ".py", ".sh", ".lua", ".png", ".html", ".cfg",
                    ".svg", ".qrc", ".desktop", ".yml", ".yaml", ".json"}
ALLOWED_NAMES = {"LICENSE", "README", "Makefile"}
IGNORED_DIRS = {"__pycache__", ".pytest_cache"}
LINK = re.compile(r"!?\[[^\]]*\]\(([^\s)]+)(?:\s+\"[^\"]*\")?\)")
DEVICE_ID = re.compile(r"(?:DeviceId|ProductId)[^\n]{0,40}?[\"'][0-9a-f]{32}[\"']", re.I)
PERSONAL_PATH = re.compile(r"/home/(?!user/|USER/|example/)[A-Za-z0-9_.-]+/")
ACTION = re.compile(r"^\s*(?:-\s*)?uses:\s*([^\s#]+)", re.M)


def project_version(root=ROOT):
    text = (root / "CMakeLists.txt").read_text(encoding="utf-8")
    match = re.search(r"project\(airctrl_desklet\s+VERSION\s+(\d+\.\d+(?:\.\d+)?)\s", text)
    if not match:
        raise ValueError("Cannot read application version from CMakeLists.txt")
    return match.group(1)


def public_files(root=ROOT):
    """Explicit roots only. Reject links and unexpected payloads inside them."""
    root = root.resolve()
    paths = [root / name for name in ROOT_FILES]
    for name in SOURCE_DIRS:
        base = root / name
        if base.is_symlink():
            raise ValueError(f"Symlink is not public source: {name}")
        if not base.is_dir():
            raise ValueError(f"Missing source directory: {name}")
        for current, dirs, files in os.walk(base, followlinks=False):
            for dirname in dirs:
                child = Path(current) / dirname
                if child.is_symlink():
                    raise ValueError(f"Symlink is not public source: {child.relative_to(root)}")
            dirs[:] = sorted(d for d in dirs if d not in IGNORED_DIRS)
            paths.extend(Path(current) / filename for filename in sorted(files))
    result = []
    for path in sorted(paths):
        relative = path.relative_to(root).as_posix()
        if path.is_symlink() or not path.is_file():
            raise ValueError(f"Missing file, non-file or symlink: {relative}")
        if path.name.startswith(".env") or (relative not in ROOT_FILES
                and path.suffix not in ALLOWED_SUFFIXES and path.name not in ALLOWED_NAMES):
            raise ValueError(f"Unexpected public file (check privacy): {relative}")
        result.append(path)
    return result


def check_repository(root=ROOT):
    root = root.resolve()
    errors = []
    for name in REQUIRED:
        if not (root / name).is_file():
            errors.append(f"Missing required file: {name}")
    try:
        paths = public_files(root)
        version = project_version(root)
    except (ValueError, OSError) as error:
        return errors + [str(error)]
    public = set(paths)
    for path in paths:
        name = path.relative_to(root).as_posix()
        if path.suffix == ".png":
            if path.read_bytes()[:8] != b"\x89PNG\r\n\x1a\n":
                errors.append(f"Invalid PNG header: {name}")
            continue
        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeError:
            errors.append(f"Non-UTF-8 source file: {name}")
            continue
        if DEVICE_ID.search(text):
            errors.append(f"Possible real device identifier: {name}")
        if PERSONAL_PATH.search(text):
            errors.append(f"Personal absolute home path: {name}")
        if re.search(r"-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----", text):
            errors.append(f"Private key material: {name}")
        if path.suffix == ".md":
            # Ignore examples in fenced blocks; verify local file targets, not anchors/URLs.
            prose = re.sub(r"```.*?```", "", text, flags=re.S)
            for match in LINK.finditer(prose):
                url = urlsplit(match.group(1).strip("<>"))
                if url.scheme or url.netloc or not url.path:
                    continue
                target = (path.parent / unquote(url.path)).resolve()
                if target not in public:
                    errors.append(f"Missing/non-public local link in {name}: {url.path}")
        if name.startswith(".github/workflows/"):
            for action in ACTION.findall(text):
                if not re.fullmatch(r"[\w.-]+/[\w./-]+@[0-9a-f]{40}", action):
                    errors.append(f"Action is not SHA-pinned: {name}: {action}")
            if "pull_request_target" in text:
                errors.append(f"Privileged PR trigger is not allowed: {name}")
        if path.suffix == ".json":
            try:
                json.loads(text)
            except json.JSONDecodeError as error:
                errors.append(f"Invalid JSON: {name}: {error}")
    if root / f"docs/releases/v{version}.md" not in public:
        errors.append(f"Missing release notes for version {version}")
    for readme in ("README.md", "README.en.md"):
        if f"v{version}" not in (root / readme).read_text(encoding="utf-8"):
            errors.append(f"Version {version} is missing in {readme}")
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--version", action="store_true", help="Print application version only")
    args = parser.parse_args()
    if args.version:
        print(project_version(args.root))
        return 0
    errors = check_repository(args.root)
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print(f"Repository OK: v{project_version(args.root)}, {len(public_files(args.root))} public files")
    print("Offline checks only; screenshots and public data still require human review.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
