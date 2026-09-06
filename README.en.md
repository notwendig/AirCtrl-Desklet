# AirCtrl-Desklet

**Your Philips air purifier, right on your Linux desktop.**

C++17 · Qt 6 · Lua 5.4 · local CoAP communication · MIT · **v1.02**

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
- Persistent CoAP observation; commands do not interrupt the observer.
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

Close the demo and start with your device's actual IP:

```bash
~/.local/bin/airctrl-desklet --host 192.0.2.10
```

Replace the documentation-only example address. Save the real address in
**right-click → Verbindung und Autostart** for future starts; `--host` initially applies
to this invocation. The historical v1.01 default remains for compatibility,
not as automatic discovery. Installation is per-user under `~/.local`.
Python is used by the installer; the GUI and backend are C++ programs.

Dependencies: C and C++17 compilers, CMake ≥ 3.16, Qt ≥ 6.2
(Core/Gui/Widgets/DBus), OpenSSL Crypto, nlohmann/json ≥ 3.9. Presets
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
automatic day mode at 07:00. `on_event(event)` receives `startup`, `time`,
`connected`, `disconnected`, `status`, `alarm` and `command`; status events expose
both `event.status` and `event.changed`. `airctrl.set { ... }` uses the same field
allow-list and confirmed-state command path as the UI.

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

Real-device reception/control and Fedora 44 + Cinnamon + X11 are confirmed by
Jürgen. Wayland-aware handling exists, but full native Wayland verification is
outstanding. Other Philips models are not claimed compatible.
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
| **betaboon** | Author of upstream Python `aioairctrl`, underlying the C++ backend |

Codex is credited as AI assistance, not a human maintainer or independent support
contact. Jürgen owns project decisions and publication. [Credits](AUTHORS.md)

## License

[MIT](LICENSE), with upstream notices retained. [Third-party notices](THIRD_PARTY_NOTICES.md)
System libraries are not bundled. This is an independent community project,
not an official Philips, Versuni or OpenAI product. Do not expose the device's UDP
service to the internet; protocol encryption does not replace network isolation.
