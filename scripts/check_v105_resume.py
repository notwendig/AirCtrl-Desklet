#!/usr/bin/env python3
"""Verify that a dirty target is an interrupted, known v1.05 update."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess


CURRENT_ONLY = {
    "scripts/v1.05-resume.json",
    "scripts/check_v105_resume.py",
}

# Byte-exact states from the two already delivered corrective v1.05 archives.
# They may be present after a test stopped between copying and committing.
KNOWN_INTERMEDIATE = {
    "CHANGELOG.md": {
        "e263b7070f852fd0f1c96ca0085443038a6e79c01d3ec281c6c3a7986e304248",
        "da080769e8aa707844892ebc0cf43ebc9e334dcffbbe916d04583a4b31c766c8",
        "a160522b4f7144926af25e455afcd0816bf8bddc1c0a5ee910008ef292f49253",
        "a88cc1f41aff3a507c53ff64abb6b0b5d3388ec1b77b0d1d58279a354dd6c3b6",
    },
    "VALIDATION.md": {
        "0e690da594daf77bec533a4f46e7d5e17b1b8928147b44b5c43e75012d141a14",
        "965f75b614e41d35242924c49ccbd6660e608d75d822db257bfa1a40fe5bfdff",
        "5efa0d4a481fda02d9656bd5118b6d46a5ed89128737b26727aba61227f796e5",
        "cedda561744c08c0916c93b7c209ac6352d690cfe19126935d7ca92674f58f4a",
    },
    "docs/releases/v1.05.md": {
        "048ca997a9e3686d8d8500485e84b5fd73eb4e74443753275efaf38f1f6be8d5",
        "9c7aabde97aaeecedf86bf711fdde8c444182aac6c7c26eeecbbe2bbd69e9e72",
        "e2b785ed86f9cdc66e30dbac4b691964f68a0bddb84dd7e3e7776255b3919d6b",
        "fad44000d5e8c880340dbc0911a611ceccf350fb887c917f202ed4272982d94d",
    },
    "docs/DEVELOPMENT.md": {
        "16218a8757d45e2a54582cc2d68db8defb94d29ac3847c26686a2079c48611b5",
    },
    "einspielen-v1.05.sh": {
        "f4c10bdab925eb5280bae338cebe3e57d563b05ceea198b8f6c38c092c7d2c81",
        "2c2672cafcbe65b66a053fc142ee2a30ce7e6306cac26ccde622777edca3387e",
        "00dde9034a03bbe06dfcdb8efe52475939445f270edae432e81b3666c8f81316",
    },
    "tests/test_repository.py": {
        "dc322a8d4a2193be4fe3686559e85e4f3932fa1dfda3a44e568ea0c4f113d950",
        "b74955b3bbde3b77d5f11acf3c12676df0336d88f80835303cec33e345bc6c03",
        "7cdb0ee403cd18f8be30a81bf397b4f705471305e76598432dc04ac4521ded6a",
        "e3c8f072fc2aa88c0d1f1d67d83d5438321bdc10f628d7a141c0b1a3ed8bcf27",
    },
    "scripts/check_v105_resume.py": {
        "075c235d34cd237b4644104f137fe4031942794c58f520e2f856051686509798",
        "b9c5ad6909c1983b782d497264db233f5986725b207e49c4171dff72805b978f",
        "59f8a25c8a31096ad4845652b8ff3e9adf182cb12f7161a31a533e1874c0e532",
    },
    "src/controller.cpp": {
        "aba007f4c0361528f9b0d81db20579a63ccfb89a25d95a9c335f664a75bffdfc",
    },
    "tests/fake_backend.cpp": {
        "32071eca9dd38353f4d123570471764b9a8bcc384dd95a88747677c2334b7da9",
    },
    "tests/test_desklet.cpp": {
        "205d8d4b512900c383e3c60f03c2ad9a69a312df45a5df99f542a1c51ebb686a",
    },
}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify_resume(source, target, manifest_path):
    source, target = Path(source).resolve(), Path(target).resolve()
    manifest = json.loads(Path(manifest_path).read_text(encoding="utf-8"))
    old = manifest["files"]
    deleted = set(manifest["deleted"])
    known = set(old) | deleted | CURRENT_ONLY
    current = {}
    for relative in set(old) | CURRENT_ONLY:
        path = source / relative
        if path.is_file():
            current[relative] = digest(path)

    result = subprocess.run(
        ["git", "-C", str(target), "status", "--porcelain=v1", "-z",
         "--untracked-files=all"], check=True, stdout=subprocess.PIPE)
    records = [record for record in result.stdout.split(b"\0") if record]
    if not records:
        return 0

    checked = 0
    for raw in records:
        if len(raw) < 4:
            raise ValueError("Unlesbarer Git-Status im Original.")
        state = raw[:2].decode("ascii", "strict")
        relative = raw[3:].decode("utf-8", "surrogateescape")
        if state != "??" and state[0] != " ":
            raise ValueError(f"Bereits vorgemerkte Änderung wird nicht übernommen: {relative}")
        if "R" in state or "C" in state or relative not in known:
            raise ValueError(f"Fremde Änderung im Original: {relative}")
        path = target / relative
        if relative in deleted:
            if path.exists() or path.is_symlink():
                raise ValueError(f"Erwartete v1.05-Löschung fehlt: {relative}")
        else:
            if not path.is_file() or path.is_symlink():
                raise ValueError(f"Keine bekannte v1.05-Datei: {relative}")
            allowed = (({old.get(relative), current.get(relative)} - {None}) |
                       KNOWN_INTERMEDIATE.get(relative, set()))
            if digest(path) not in allowed:
                raise ValueError(f"Datei enthält eigene oder unbekannte Änderungen: {relative}")
        checked += 1
    return checked


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("target", type=Path)
    parser.add_argument("manifest", type=Path)
    args = parser.parse_args()
    try:
        count = verify_resume(args.source, args.target, args.manifest)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"{error}\n")
    print(f"Sichere Wiederaufnahme erkannt: {count} bekannte v1.05-Änderungen.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
