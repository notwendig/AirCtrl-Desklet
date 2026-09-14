"""Offline tests for repository checks and deterministic, source-only releases."""

import hashlib
import json
import os
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
        self.assertIn("main.cpp", server_cmake)
        self.assertIn("server.cpp", server_cmake)
        self.assertIn("server.h", server_cmake)
        self.assertIn("statuslog.cpp", server_cmake)
        self.assertIn("add_executable(airctrl-client cli_main.cpp)", client_cmake)
        self.assertNotIn("Qt6", server_cmake)
        self.assertIn("airctrl_client_common", client_cmake)
        self.assertIn("config/airctrld.cfg", cmake)
        self.assertIn("scripts/plot-airctrl.py", cmake)
        self.assertIn("RENAME airctrl-plot", cmake)
        plotter = (self.root / "scripts" / "plot-airctrl.py").read_text()
        self.assertIn('matplotlib.use("Agg")', plotter)
        self.assertIn("PdfPages", plotter)
        self.assertIn('BOOLEAN_COLUMNS = {"pwr", "cl", "uil"', plotter)
        self.assertNotIn("plot-airctrl.gnuplot", cmake)
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
        self.assertIn("local_port=5680", config)
        self.assertIn("keepalive_ms=20000", config)
        self.assertIn("observe_refreshes=1", config)
        self.assertIn("status_file=/var/log/airctrl.log", config)
        server = (self.root / "src" / "server" / "server.cpp").read_text()
        server_header = (self.root / "src" / "server" / "server.h").read_text()
        self.assertIn("<poll.h>", server)
        self.assertIn("aioairctrl::Json", server_header)
        self.assertIn("class AirCtrlServer final", server_header)
        self.assertNotIn("class AirCtrlServer final", server)
        self.assertIn("AirCtrlServer::run()", server)
        self.assertNotIn("#include <Q", server)
        self.assertNotIn("QTcpServer", server)
        protocol = (self.root / "third_party" / "aioairctrl" / "src" / "coap.cpp").read_text()
        client = (self.root / "third_party" / "aioairctrl" / "src" / "client.cpp").read_text()
        self.assertIn("Empty CON is the standard CoAP ping", protocol)
        self.assertIn("re-registering on the same UDP session", client)
        self.assertIn('file(READ "${PROJECT_SOURCE_DIR}/examples/automation.lua"', cmake)

    def test_role_specific_installer(self):
        installer = self.root / "install.sh"
        help_result = subprocess.run(
            ["bash", str(installer), "--help"], check=True,
            capture_output=True, text=True)
        self.assertIn("-s, --server", help_result.stdout)
        self.assertIn("-c, --client", help_result.stdout)
        self.assertIn("--prefix PFAD", help_result.stdout)
        self.assertIn("Server: /usr/local", help_result.stdout)

        unknown = subprocess.run(
            ["bash", str(installer), "--unbekannt"], check=False,
            capture_output=True, text=True)
        self.assertEqual(2, unknown.returncode)
        self.assertIn("Unbekannte Option", unknown.stderr)

        def run_install(name, *options, explicit_prefix=True):
            scenario = self.area / name
            fake_bin = scenario / "fake-bin"
            prefix = scenario / "prefix"
            home = scenario / "home"
            config_home = scenario / "config"
            log = scenario / "cmake.log"
            fake_bin.mkdir(parents=True)
            home.mkdir()
            cmake_stub = fake_bin / "cmake"
            cmake_stub.write_text("""#!/usr/bin/env bash
set -euo pipefail
printf '%s\\n' "$*" >> "$AIRCTRL_TEST_CMAKE_LOG"
if [[ "$*" == *"-DAIRCTRL_COMPONENT=server"* ]]; then
    mkdir -p "$AIRCTRL_TEST_PROJECT/build/RELEASE/server"
    printf '[{"directory":"%s","command":"c++ -c main.cpp","file":"%s/src/server/main.cpp"}]\\n' \
        "$AIRCTRL_TEST_PROJECT" "$AIRCTRL_TEST_PROJECT" > \
        "$AIRCTRL_TEST_PROJECT/build/RELEASE/server/compile_commands.json"
fi
if [[ "$*" == *"-DAIRCTRL_COMPONENT=client"* ]]; then
    mkdir -p "$AIRCTRL_TEST_PROJECT/build/RELEASE/client"
    printf '[{"directory":"%s","command":"c++ -c desklet.cpp","file":"%s/src/client/desklet.cpp"}]\\n' \
        "$AIRCTRL_TEST_PROJECT" "$AIRCTRL_TEST_PROJECT" > \
        "$AIRCTRL_TEST_PROJECT/build/RELEASE/client/compile_commands.json"
fi
if [[ " $* " == *" --install "* && "$*" == *"/server"* ]]; then
    mkdir -p "$AIRCTRL_TEST_PREFIX/bin"
    : > "$AIRCTRL_TEST_PREFIX/bin/airctrl-server"
    : > "$AIRCTRL_TEST_PREFIX/bin/airctrl-plot"
    chmod +x "$AIRCTRL_TEST_PREFIX/bin/airctrl-server"
    chmod +x "$AIRCTRL_TEST_PREFIX/bin/airctrl-plot"
fi
if [[ " $* " == *" --install "* && "$*" == *"/client"* ]]; then
    mkdir -p "$AIRCTRL_TEST_PREFIX/bin" "$AIRCTRL_TEST_PREFIX/share/applications"
    : > "$AIRCTRL_TEST_PREFIX/bin/airctrl-desklet"
    : > "$AIRCTRL_TEST_PREFIX/bin/airctrl-client"
    printf '%s\\n' '[Desktop Entry]' 'Exec=airctrl-desklet' > \
        "$AIRCTRL_TEST_PREFIX/share/applications/airctrl-desklet.desktop"
fi
""")
            cmake_stub.chmod(0o755)
            pkill_stub = fake_bin / "pkill"
            pkill_stub.write_text("#!/usr/bin/env bash\nexit 0\n")
            pkill_stub.chmod(0o755)
            sudo_stub = fake_bin / "sudo"
            sudo_stub.write_text("#!/usr/bin/env bash\nexec \"$@\"\n")
            sudo_stub.chmod(0o755)
            env = os.environ.copy()
            env.update({
                "PATH": f"{fake_bin}:{env['PATH']}",
                "HOME": str(home),
                "XDG_CONFIG_HOME": str(config_home),
                "AIRCTRL_SKIP_SYSTEM_CONFIG": "1",
                "AIRCTRL_SKIP_SYSTEM_LOG": "1",
                "AIRCTRL_SKIP_SYSTEMD": "1",
                "AIRCTRL_TEST_CMAKE_LOG": str(log),
                "AIRCTRL_TEST_PREFIX": str(prefix),
                "AIRCTRL_TEST_PROJECT": str(self.root),
            })
            command = ["bash", str(installer), *options]
            if explicit_prefix:
                command.extend(("--prefix", str(prefix)))
            subprocess.run(
                command,
                cwd=self.root, env=env, check=True, capture_output=True, text=True)
            return log.read_text(), prefix, config_home

        server_log, server_prefix, server_config = run_install("server", "--server")
        self.assertIn("-DAIRCTRL_COMPONENT=server", server_log)
        self.assertNotIn("-DAIRCTRL_COMPONENT=client", server_log)
        self.assertTrue((server_prefix / "bin/airctrl-server").is_file())
        self.assertTrue((server_prefix / "bin/airctrl-plot").is_file())
        self.assertTrue((server_config / "systemd/user/airctrl-server.service").is_file())
        self.assertFalse((server_prefix / "bin/airctrl-desklet").exists())
        commands = json.loads((self.root / "compile_commands.json").read_text())
        self.assertTrue(any(entry["file"].endswith("src/server/main.cpp")
                            for entry in commands))
        self.assertFalse(any(entry["file"].endswith("src/client/desklet.cpp")
                             for entry in commands))

        client_log, client_prefix, client_config = run_install("client", "--client")
        self.assertIn("-DAIRCTRL_COMPONENT=client", client_log)
        self.assertNotIn("-DAIRCTRL_COMPONENT=server", client_log)
        self.assertTrue((client_prefix / "bin/airctrl-desklet").is_file())
        desktop = client_prefix / "share/applications/airctrl-desklet.desktop"
        self.assertIn(str(client_prefix / "bin/airctrl-desklet"), desktop.read_text())
        self.assertFalse((client_config / "systemd/user/airctrl-server.service").exists())
        self.assertFalse((client_prefix / "bin/airctrl-server").exists())
        commands = json.loads((self.root / "compile_commands.json").read_text())
        self.assertTrue(any(entry["file"].endswith("src/client/desklet.cpp")
                            for entry in commands))
        self.assertFalse(any(entry["file"].endswith("src/server/main.cpp")
                             for entry in commands))

        full_log, full_prefix, full_config = run_install("full")
        self.assertIn("-DAIRCTRL_COMPONENT=server", full_log)
        self.assertIn("-DAIRCTRL_COMPONENT=client", full_log)
        self.assertTrue((full_prefix / "bin/airctrl-server").is_file())
        self.assertTrue((full_prefix / "bin/airctrl-desklet").is_file())
        self.assertTrue((full_config / "systemd/user/airctrl-server.service").is_file())
        commands = json.loads((self.root / "compile_commands.json").read_text())
        self.assertEqual({"main.cpp", "desklet.cpp"},
                         {Path(entry["file"]).name for entry in commands})

        default_log, _, default_config = run_install(
            "server-default", "--server", explicit_prefix=False)
        self.assertIn("-DCMAKE_INSTALL_PREFIX=/usr/local", default_log)
        service = default_config / "systemd/user/airctrl-server.service"
        self.assertIn('ExecStart="/usr/local/bin/airctrl-server"', service.read_text())

    def test_pdf_plotter(self):
        try:
            __import__("matplotlib")
        except ImportError:
            self.skipTest("Matplotlib ist in dieser Testumgebung nicht installiert")
        csv_file = self.area / "status.csv"
        pdf_file = self.area / "status.pdf"
        csv_file.write_text(
            "timestamp,DeviceId,pm25,pwr,rh,name,_extra_json\n"
            "2026-09-13T23:18:57.937Z,0123456789abcdef,4,1,52,Wohnzimmer,\n"
            "2026-09-13T23:19:57.937Z,0123456789abcdef,7,0,53,Wohnzimmer,\n",
            encoding="utf-8")
        result = subprocess.run(
            [sys.executable, str(self.root / "scripts/plot-airctrl.py"),
             str(csv_file), str(pdf_file)],
            check=False, capture_output=True, text=True)
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertIn("3 Werte, 1 Seiten, 2 Statuszeilen", result.stdout)
        self.assertGreater(pdf_file.stat().st_size, 5_000)
        self.assertEqual(b"%PDF", pdf_file.read_bytes()[:4])

    def test_server_and_client_sources_are_physically_separated(self):
        server_sources = "\n".join(
            path.read_text() for path in (self.root / "src" / "server").rglob("*.cpp"))
        server_header = (self.root / "src" / "server" / "server.h").read_text()
        client_sources = "\n".join(
            path.read_text() for path in (self.root / "src" / "client").rglob("*.cpp"))
        common_sources = "\n".join(
            path.read_text() for path in (self.root / "src" / "common").rglob("*.cpp"))
        self.assertIn("aioairctrl/client.hpp", server_header)
        self.assertNotIn("aioairctrl", client_sources)
        self.assertNotIn("#include <Q", server_sources)
        self.assertNotIn("#include <Q", server_header)
        self.assertNotIn("Qt6", server_sources)
        self.assertNotIn("aioairctrl", common_sources)
        self.assertNotIn("#include <Q", common_sources)
        self.assertFalse((self.root / "src" / "common" / "CMakeLists.txt").exists())
        self.assertTrue((self.root / "src" / "client" / "controlvalues.cpp").is_file())
        self.assertTrue((self.root / "src" / "client" / "ipc.cpp").is_file())

    def test_server_cmake_branch_does_not_discover_qt(self):
        cmake = (self.root / "CMakeLists.txt").read_text()
        server_branch = cmake[cmake.index('if(AIRCTRL_COMPONENT STREQUAL "server")'):
                              cmake.index('if(CMAKE_CXX_COMPILER_ID')]
        self.assertLess(server_branch.index("add_subdirectory(src/server)"),
                        server_branch.index("find_package(Qt6"))
        self.assertIn("else()", server_branch)

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
