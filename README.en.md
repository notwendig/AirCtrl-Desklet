# AirCtrl-Desklet

**Your Philips air purifier, right on your Linux desktop.**

C++17 · Qt 6 · Lua 5.4 · local server/clients · MIT · **v1.05**

[Deutsch](README.md) · [Development](docs/DEVELOPMENT.md) · [Changelog](CHANGELOG.md)

![AirCtrl-Desklet running on Cinnamon with a customized dark theme](docs/images/desklet-dark.png)

*Actual screenshot supplied by Jürgen, running Cinnamon on X11. Not a mockup.*

A compact, German-language Qt desktop widget for the **Philips AC2729/10**.
Control the device, read humidity/temperature/PM2.5/IAI, and see maintenance
warnings without opening a cloud app. AirCtrl-Desklet requires no cloud account.
It is a standalone Qt application, **not a Cinnamon JavaScript desklet**.

## Features

- Eight controls: power, child lock, automatic mode, fan speed, humidity target,
  lighting, purification/2-in-1 and shutdown timer.
- One persistent `airctrl-server` is the only process that contacts the AC2729.
  The desklet, Lua and `airctrl-client` use only its protected Unix socket.
- One UDP I/O session is shared by all clients. Only the server renews it after
  a 90-second status timeout.
- Confirmed-state emblems, seconds-since-reception indicator and separate alarm circle.
- Per-filter warnings, acknowledgement, desktop notifications and optional sound.
- Configurable colours, background transparency, fonts, window decoration and autostart.
- Diagnostics with raw JSON, hexadecimal codes and a full copyable report.
- Sandboxed Lua automation for status, connection, alarm and time events,
  including day/night schedules.

![Light theme rendered by the actual Qt application in demo mode](docs/images/desklet-light.png)

## Install on Fedora

```bash
sudo dnf install -y gcc-c++ cmake make qt6-qtbase-devel qt6-qtsvg \
  openssl-devel json-devel python3 dejavu-sans-fonts
bash install.sh
~/.local/bin/airctrl-desklet --demo
```

Close the demo and start with your device's IP address or hostname:

```bash
~/.local/bin/airctrl-desklet --host 192.0.2.10
# or, for example, through local DNS/mDNS:
~/.local/bin/airctrl-desklet --host aircleaner.local
```

Replace the documentation-only example address. Save the real address in
**right-click → Verbindung und Autostart** for future starts; `--host` initially applies
to this invocation. `AC2729-10` is the default but is not automatic discovery;
it must resolve on the local network. Installation is per-user under `~/.local`.
Python is used by the installer; GUI, server and command-line client are C++ programs.

The installer enables the per-user `airctrl-server.service`. Useful client calls:

```bash
airctrl-client status
airctrl-client watch
airctrl-client set pwr=1
airctrl-client set mode=S om=s uil=0
airctrl-client refresh
```

Clients use `$XDG_RUNTIME_DIR/airctrl-desklet/server.sock`; its directory and
socket are accessible only to the logged-in user.

Dependencies: C and C++17 compilers, CMake ≥ 3.16, Qt ≥ 6.2
(Core/Gui/Widgets/DBus/Network), OpenSSL Crypto, nlohmann/json ≥ 3.9. Presets
additionally require CMake ≥ 3.21 and Ninja.
[Build instructions and other distributions](docs/DEVELOPMENT.md)

## Controls and warnings

Right-click opens settings; drag a value or status area to move the widget.
F1 opens diagnostics, F5 explicitly reconnects. Left-click either circle for alarms.
Power stays clickable: orange when disconnected, white when off, green when on.

Data age defaults to green below 45 s, yellow from 45 s, red from 90 s.
The alarm circle is independent of data freshness. Both are 26 px at the default font.

## Lua automation

Open **right-click → Lua-Automatik** to edit and enable the local script. It is
disabled by default. The supplied example schedules night mode at 22:00 and
automatic day mode at 07:00. Its comments also form a complete event, status-field
and control-value reference. `on_event(event)` receives `startup`, `time`,
`connected`, `disconnected`, `status`, `alarm` and `command`; status events expose
both `event.status` and `event.changed`. `airctrl.set { ... }` uses the same field
allow-list, local IPC connection and confirmed-state command path as the UI.

The verified official Lua 5.4.9 sources are embedded. The sandbox exposes no API
for arbitrary file, network, process, shell, package or debug access and applies
memory/instruction limits. Only `airctrl.set` can send allow-listed values to the
configured device. Scheduled commands are attempted at most once per occurrence;
an already-confirmed target state sends no command.
[API and examples](docs/LUA_AUTOMATION.md)

A3 means HEPA replacement, C7 carbon-filter replacement, F1 wick replacement.
F0 is cleaning. For AC2729, the widget warns at 1–120 remaining operating hours
and escalates at zero. **120 h is a local widget policy, not a verified Philips
firmware threshold.** Unknown error bits are not decoded speculatively.
Acknowledgement never resets the device's maintenance counters.

## Diagnostics

![Qt diagnostics using synthetic data, hexadecimal codes and the copy button](docs/images/diagnostics-demo.png)

The public diagnostic image uses test data, not real device identifiers.
Copy the report using its button or Ctrl+Shift+C. Paste using Ctrl+V,
or Ctrl+Shift+V in a terminal. PRIMARY/middle-click is also supported when available.
Redact identifiers, device names, addresses and local paths before sharing reports.

## Validation

Two device captures from **8 September 2026** contain **148 valid status
messages** and **17/17 accepted controls**, each confirmed by the next status
within **45–97 ms**. Controls reuse the same UDP port without another sync;
Observe is briefly cancelled and registered again on that socket.

The captured recovery starts after **90 s** without status, opens a new session
after another 9.7 s, and receives its first status 36.4 s after registration:
**136.1 s** without fresh data in total. A separate 65.8-s pause while powered
off does not restart the session. The cause of the longer silence is unknown;
the captures also leave a gap of almost nine minutes.
[Packet evidence and interpretation limits](docs/PROTOCOL_VALIDATION_2026-09-08.md)

On this AC2729/10, `dt=6` is followed by `dtrs=360`, then `359` about a minute
later. This supports interpreting `dtrs` as remaining timer minutes, but is
an observation, not a Philips specification. The raw field remains read-only.

Real-device reception/control and Fedora 44 + Cinnamon + X11 are confirmed by
Jürgen. Wayland-aware handling exists, but full native Wayland verification is
outstanding. The v1.05 server/client build and installation are locally verified;
native multi-client IPC and the real-device run still need confirmation on Fedora,
because the isolated validation environment cannot open an AF_UNIX listening socket.
Other Philips models are not claimed compatible.
[Local results and limitations](VALIDATION.md). The prepared GitHub workflow is
not a claim of an already successful CI run.

```bash
cmake --preset dev
cmake --build --preset dev --parallel 2
ctest --preset dev
python3 scripts/check_repository.py
```

[Contributing](CONTRIBUTING.md) · [Security](SECURITY.md) · [GitHub setup](docs/GITHUB_SETUP.md)

## Roles and acknowledgements

| Contributor | Role |
|---|---|
| **Jürgen Sievers** | Project initiator, product owner and maintainer; requirements, UX direction, priorities, physical-device testing and release decisions |
| **OpenAI Codex** | AI development partner; collaborative C++/Qt/Lua implementation, protocol analysis, debugging, tests and documentation |
| **betaboon** | Author of upstream Python `aioairctrl`, underlying the server's internal C++ device transport |

Codex is credited as AI assistance, not a human maintainer or independent support
contact. Jürgen owns project decisions and publication. [Credits](AUTHORS.md)

## License

[MIT](LICENSE), with upstream notices retained. [Third-party notices](THIRD_PARTY_NOTICES.md)
System libraries are not bundled. This is an independent community project,
not an official Philips, Versuni or OpenAI product. Do not expose the device's UDP
service to the internet; protocol encryption does not replace network isolation.
