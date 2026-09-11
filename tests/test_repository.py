"""Offline tests for repository checks and deterministic, source-only releases."""

import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_repository import check_repository, project_version, public_files  # noqa: E402
from check_v105_resume import verify_resume  # noqa: E402
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

    def test_capture_formats_are_git_ignored(self):
        patterns = set((self.root / ".gitignore").read_text().splitlines())
        for pattern in ("*.pcap", "*.pcapng", "*.pcap.*", "*.pcapng.*", "*.lz4"):
            with self.subTest(pattern=pattern):
                self.assertIn(pattern, patterns)

    def test_server_client_install_contract(self):
        cmake = (self.root / "CMakeLists.txt").read_text()
        server_cmake = (self.root / "src" / "server" / "CMakeLists.txt").read_text()
        client_cmake = (self.root / "src" / "client" / "CMakeLists.txt").read_text()
        presets = json.loads((self.root / "CMakePresets.json").read_text())
        self.assertIn("install(TARGETS airctrl-server", cmake)
        self.assertIn("install(TARGETS airctrl-desklet airctrl-client", cmake)
        self.assertNotIn("airctrl-backend", cmake)
        self.assertIn("add_executable(airctrl-server main.cpp)", server_cmake)
        self.assertIn("add_executable(airctrl-client cli_main.cpp)", client_cmake)
        self.assertIn("config/airctrld.cfg", cmake)
        locations = {preset["name"]: preset.get("binaryDir")
                     for preset in presets["configurePresets"]}
        self.assertEqual("${sourceDir}/build/DEBUG/server", locations["debug-server"])
        self.assertEqual("${sourceDir}/build/DEBUG/client", locations["debug-client"])
        self.assertEqual("${sourceDir}/build/RELEASE/server", locations["release-server"])
        self.assertEqual("${sourceDir}/build/RELEASE/client", locations["release-client"])
        config = (self.root / "config" / "airctrld.cfg").read_text()
        self.assertIn("host=AC2729-10", config)
        self.assertIn("port=5683", config)
        self.assertIn("port=5680", config)
        server = (self.root / "src" / "server" / "main.cpp").read_text()
        self.assertIn("QTcpServer", server)
        self.assertNotIn("QLocalServer", server)

    def test_server_and_client_sources_are_physically_separated(self):
        server_sources = "\n".join(
            path.read_text() for path in (self.root / "src" / "server").rglob("*.cpp"))
        client_sources = "\n".join(
            path.read_text() for path in (self.root / "src" / "client").rglob("*.cpp"))
        common_sources = "\n".join(
            path.read_text() for path in (self.root / "src" / "common").rglob("*.cpp"))
        self.assertIn("aioairctrl/client.hpp", server_sources)
        self.assertNotIn("aioairctrl", client_sources)
        self.assertNotIn("QWidget", server_sources)
        self.assertNotIn("aioairctrl", common_sources)
        self.assertNotIn("QWidget", common_sources)

    def test_diagnostic_report_iterates_diagnostic_fields(self):
        diagnostics = (self.root / "src" / "client" / "diagnostics.cpp").read_text()
        self.assertIn("for(const DiagnosticField& field:fields)", diagnostics)
        self.assertNotIn("for(const QString& field:fields)", diagnostics)

    def test_desklet_suite_has_slow_machine_timeout(self):
        cmake = (self.root / "CMakeLists.txt").read_text()
        self.assertIn(
            "set_tests_properties(desklet PROPERTIES TIMEOUT 300)", cmake)
        self.assertNotIn(
            "set_tests_properties(desklet PROPERTIES TIMEOUT 90)", cmake)

    def test_tcp_watchdog_is_armed_before_connect(self):
        controller = (self.root / "src" / "client" / "controller.cpp").read_text()
        start = controller.index("void Controller::connectServer()")
        end = controller.index("void Controller::launchServer()", start)
        connect_body = controller[start:end]
        self.assertLess(connect_body.index("connectWatchdog_.start(3000);"),
                        connect_body.index("socket_.connectToHost("))
        fake = (self.root / "tests" / "fake_backend.cpp").read_text()
        self.assertIn("std::optional<IpcCommand> pending_", fake)
        self.assertIn("if(pending_) return", fake)

    def test_detached_servers_do_not_keep_ctest_pipes_open(self):
        controller = (self.root / "src" / "client" / "controller.cpp").read_text()
        self.assertIn("server.setStandardOutputFile(QProcess::nullDevice())", controller)
        self.assertIn("server.setStandardErrorFile(QProcess::nullDevice())", controller)

    def test_synthetic_alarms_do_not_reach_real_desktop(self):
        desklet = (self.root / "src" / "client" / "desklet.cpp").read_text()
        tests = (self.root / "tests" / "test_desklet.cpp").read_text()
        self.assertIn("AIRCTRL_TEST_SUPPRESS_DESKTOP_ALARMS", desklet)
        self.assertIn('qputenv("AIRCTRL_TEST_SUPPRESS_DESKTOP_ALARMS","1")', tests)

    def test_update_script_uses_original_ssh_and_atomic_push(self):
        script = (self.root / "einspielen-v1.06.sh").read_text()
        self.assertIn('${HOME}/Projects/Qt/AirCtrl-Desklet', script)
        self.assertIn('git@github.com:notwendig/AirCtrl-Desklet.git', script)
        self.assertIn('push --atomic', script)
        self.assertIn('public_files', script)
        self.assertIn('worktree add --detach "$publish_dir" origin/master', script)
        self.assertIn('backup-before-server-clients-', script)
        self.assertIn('v1.06: TCP server and system device configuration', script)
        self.assertIn('architecture_tag="server_clients"', script)
        self.assertIn('"refs/tags/$architecture_tag"', script)
        self.assertIn('Der veröffentlichte Tag $tag bleibt unverändert', script)
        self.assertIn('push_refs=("HEAD:master" "refs/tags/$architecture_tag")', script)
        self.assertIn('"tests/parent_probe.cpp"', script)
        self.assertIn('cmake -E remove_directory "$test_build"', script)
        self.assertLess(script.index('cmake -E remove_directory "$test_build"'),
                        script.index('cmake -S "$source_dir" -B "$server_test_build"'))
        self.assertNotIn('push --force', script)
        self.assertNotIn('reset --hard', script)

    def test_ci_targets_published_master_branch(self):
        workflow = (self.root / ".github" / "workflows" / "ci.yml").read_text()
        self.assertIn("branches: [master]", workflow)
        self.assertNotIn("branches: [main]", workflow)

    def test_cpp_api_is_doxygen_documented(self):
        self.assertIn("OUTPUT_LANGUAGE        = English",
                      (self.root / "Doxyfile").read_text())
        for header in (self.root / "src").rglob("*.hpp"):
            with self.subTest(header=header.name):
                self.assertIn("@file", header.read_text())

    def test_interrupted_update_resume_accepts_only_known_bytes(self):
        source = self.area / "resume-source"
        target = self.area / "resume-target"
        source.mkdir()
        target.mkdir()
        subprocess.run(["git", "init", "-q", str(target)], check=True)
        subprocess.run(["git", "-C", str(target), "config", "user.name", "Test"], check=True)
        subprocess.run(["git", "-C", str(target), "config", "user.email", "test@example.invalid"], check=True)
        tracked = target / "sample.txt"
        tracked.write_text("v1.04\n")
        subprocess.run(["git", "-C", str(target), "add", "sample.txt"], check=True)
        subprocess.run(["git", "-C", str(target), "commit", "-qm", "baseline"], check=True)

        known = b"first v1.05 attempt\n"
        current = b"v1.05 with timeout fix\n"
        tracked.write_bytes(known)
        (source / "sample.txt").write_bytes(current)
        manifest = source / "resume.json"
        manifest.write_text(json.dumps({"version": 1, "files": {
            "sample.txt": hashlib.sha256(known).hexdigest()}, "deleted": []}))
        self.assertEqual(1, verify_resume(source, target, manifest))
        tracked.write_text("user edit\n")
        with self.assertRaises(ValueError):
            verify_resume(source, target, manifest)

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
            self.assertIn("AirCtrl-Desklet/docs/IPC_PROTOCOL.md", names)
            self.assertIn("AirCtrl-Desklet/einspielen-v1.06.sh", names)
            self.assertFalse(any("secret.log" in name or name.endswith("/.env") for name in names))
            self.assertFalse(any(name.lower().endswith((".pcap", ".pcapng", ".lz4")) for name in names))
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
