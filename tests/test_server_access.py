"""Check that the TCP host allowlist accepts and rejects real socket peers."""

import pathlib
import socket
import subprocess
import sys
import tempfile
import time


SERVER = pathlib.Path(sys.argv[1])


def check(allowed: str, source: str, expected: bool) -> None:
    with tempfile.TemporaryDirectory(prefix="airctrl-access-") as directory:
        root = pathlib.Path(directory)
        with socket.socket() as probe:
            probe.bind(("127.0.0.1", 0))
            port = probe.getsockname()[1]
        config = root / "airctrld.cfg"
        config.write_text(
            f"[server]\nlisten_address=127.0.0.1\nport={port}\nallowed_hosts={allowed}\n"
            f"[logging]\nstatus_file={root / 'status.csv'}\n"
            f"[automation]\nscript_file={root / 'automation.lua'}\n"
            f"state_file={root / 'automation-state.json'}\nenabled=false\n"
            "[device]\nhost=127.0.0.1\nport=5683\n"
        )
        subprocess.run([SERVER, "--config", config, "--check-config"],
                       check=True, capture_output=True)
        process = subprocess.Popen([SERVER, "--config", config],
                                   stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        try:
            deadline = time.monotonic() + 3
            while True:
                try:
                    with socket.socket() as client:
                        client.settimeout(1)
                        client.bind((source, 0))
                        client.connect(("127.0.0.1", port))
                        payload = client.recv(4096)
                    break
                except ConnectionRefusedError:
                    if time.monotonic() >= deadline:
                        raise
                    time.sleep(0.05)
            accepted = b'"_airctrl":"state"' in payload
            assert accepted == expected, (allowed, source, payload)
        finally:
            process.terminate()
            process.communicate(timeout=3)


check("192.0.2.10", "127.0.0.1", True)  # local administration
check("192.0.2.10", "127.0.0.2", False)
check("127.0.0.2", "127.0.0.2", True)
