"""Offline tests for repository checks and deterministic, source-only releases."""

import hashlib
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_repository import check_repository, project_version, public_files  # noqa: E402
from package_source import package_source  # noqa: E402


class RepositoryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="airctrl-repository-test-")
        self.addCleanup(self.temp.cleanup)
        self.area = Path(self.temp.name)
        self.root = self.area / "source"
        for source in public_files(ROOT):
            target = self.root / source.relative_to(ROOT)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)

    def test_repository_and_version(self):
        self.assertEqual([], check_repository(self.root))
        self.assertRegex(project_version(self.root), r"^\d+\.\d+(?:\.\d+)?$")

    def test_broken_readme_image_is_rejected(self):
        with (self.root / "README.md").open("a") as stream:
            stream.write("\n![missing](docs/images/not-present.png)\n")
        self.assertTrue(any("not-present.png" in error for error in check_repository(self.root)))

    def test_capture_in_source_is_rejected(self):
        (self.root / "docs" / "private.pcapng").write_bytes(b"private capture")
        with self.assertRaises(ValueError):
            package_source(self.root, self.area / "invalid.zip")
        self.assertFalse((self.area / "invalid.zip").exists())

    def test_device_identifier_is_rejected(self):
        # Deliberately generated synthetic 32-hex identifier, never a real device ID.
        with (self.root / "docs" / "private.md").open("w") as stream:
            stream.write('DeviceId = "' + "ab" * 16 + '"\n')
        self.assertTrue(any("identifier" in error for error in check_repository(self.root)))

    def test_symlink_is_rejected(self):
        (self.root / "docs" / "outside.md").symlink_to(self.area / "outside")
        with self.assertRaises(ValueError):
            public_files(self.root)

    def test_unpinned_action_is_rejected(self):
        with (self.root / ".github/workflows/ci.yml").open("a") as stream:
            stream.write("\n      - uses: actions/checkout@main\n")
        self.assertTrue(any("SHA-pinned" in error for error in check_repository(self.root)))

    def test_reproducible_source_only_archive(self):
        (self.root / "build").mkdir()
        (self.root / "build" / "secret.log").write_text("not public")
        (self.root / ".env").write_text("not public")
        one, checksum = package_source(self.root, self.area / "one.zip")
        two, _ = package_source(self.root, self.area / "two.zip")
        self.assertEqual(one.read_bytes(), two.read_bytes())
        self.assertEqual(hashlib.sha256(one.read_bytes()).hexdigest(),
                         checksum.read_text().split()[0])
        with zipfile.ZipFile(one) as archive:
            self.assertIsNone(archive.testzip())
            names = archive.namelist()
            self.assertIn("AirCtrl-Desklet/.github/workflows/ci.yml", names)
            self.assertIn("AirCtrl-Desklet/docs/images/desklet-dark.png", names)
            self.assertFalse(any("secret.log" in name or name.endswith("/.env") for name in names))
            self.assertTrue(all(name.startswith("AirCtrl-Desklet/") and ".." not in Path(name).parts
                                for name in names))
            archive.extractall(self.area / "unpacked")
        self.assertEqual([], check_repository(self.area / "unpacked" / "AirCtrl-Desklet"))

    def test_existing_output_is_preserved(self):
        output = self.area / "existing.zip"
        output.write_bytes(b"user data")
        with self.assertRaises(FileExistsError):
            package_source(self.root, output)
        self.assertEqual(b"user data", output.read_bytes())

    def test_output_symlink_is_rejected(self):
        output = self.area / "link.zip"
        target = self.area / "outside.zip"
        output.symlink_to(target)
        with self.assertRaises(ValueError):
            package_source(self.root, output)
        self.assertFalse(target.exists())


if __name__ == "__main__":
    unittest.main()
