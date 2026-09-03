#!/usr/bin/env python3
"""Create a deterministic public source ZIP and SHA-256 sidecar, without Git/network."""

import argparse
import hashlib
from pathlib import Path
import zipfile

from check_repository import ROOT, check_repository, project_version, public_files


def package_source(root, output):
    root, output = root.resolve(), output.absolute()
    checksum = output.with_name(output.name + ".sha256")
    if output.suffix != ".zip" or output.is_symlink() or checksum.is_symlink():
        raise ValueError("Output must be a regular .zip path; symlinks are not allowed")
    if output.exists() or checksum.exists():
        raise FileExistsError("Output already exists; choose another --output path")
    if "\n" in output.name or "\r" in output.name or "\\" in output.name:
        raise ValueError("Output filename cannot contain newlines or backslashes")
    errors = check_repository(root)
    if errors:
        raise ValueError("Repository check failed:\n" + "\n".join(errors))
    paths = public_files(root)
    output.parent.mkdir(parents=True, exist_ok=True)
    created = []
    try:
        with output.open("xb") as stream:
            created.append(output)
            with zipfile.ZipFile(stream, "w", compression=zipfile.ZIP_DEFLATED,
                                 compresslevel=9) as archive:
                for path in paths:
                    relative = path.relative_to(root).as_posix()
                    info = zipfile.ZipInfo("AirCtrl-Desklet/" + relative,
                                           date_time=(2026, 1, 1, 0, 0, 0))
                    info.create_system = 3
                    mode = 0o755 if path.suffix == ".sh" else 0o644
                    info.external_attr = (0o100000 | mode) << 16
                    info.compress_type = zipfile.ZIP_DEFLATED
                    archive.writestr(info, path.read_bytes(), compresslevel=9)
        digest = hashlib.sha256(output.read_bytes()).hexdigest()
        with checksum.open("x", encoding="utf-8", newline="\n") as stream:
            created.append(checksum)
            stream.write(f"{digest}  {output.name}\n")
    except Exception:
        # Only remove partial outputs created by this invocation, never existing files.
        for path in created:
            path.unlink(missing_ok=True)
        raise
    return output, checksum


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    output = args.output or ROOT / "dist" / f"AirCtrl-Desklet-{project_version()}.zip"
    try:
        archive, checksum = package_source(ROOT, output)
    except (OSError, ValueError) as error:
        parser.exit(1, f"Packaging failed: {error}\n")
    print(archive)
    print(checksum)


if __name__ == "__main__":
    main()
