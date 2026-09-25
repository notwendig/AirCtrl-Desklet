# AirCtrl-Desklet

**Your Philips air purifier, right on your Linux desktop.**

C++17 · Qt 6 · Lua 5.4 · TCP server/clients · MIT · **v2.00 Stable**

[Deutsch](README.md) · [Development](docs/DEVELOPMENT.md) · [C++ API](docs/CPP_API.md) · [Changelog](CHANGELOG.md)

![AirCtrl-Desklet running on Cinnamon with a customized dark theme](docs/images/desklet-dark.png)

*Actual client screenshot, running Cinnamon on X11. Not a mockup.*

A compact, German-language Qt desktop widget for the **Philips AC2729/10**.
Control the device, read humidity/temperature/PM2.5/IAI, and see maintenance
warnings without opening a cloud app. AirCtrl-Desklet requires no cloud account.
It is a standalone Qt application, **not a Cinnamon JavaScript desklet**.

## Features

- Eight controls: power, child lock, automatic mode, fan speed, humidity target,
  lighting, purification/2-in-1 and shutdown timer. Holding the timer button
  for at least 800 ms invokes the server-side Lua function `on_long_timer()`.
- One persistent `airctrl-server` is the only process that contacts the AC2729
  and the only process containing the Lua runtime. Editors and other clients use its TCP endpoint.
- Ping/pong monitoring detects a silently broken TCP connection without waiting
  for a control action and reconnects the desklet automatically.
- One UDP I/O session is shared by all clients. A fixed source port and CoAP
  keepalive preserve host-firewall state; the server first refreshes Observe on
  that session before performing a complete reconnect.
- Confirmed-state emblems, seconds-since-reception indicator and separate alarm circle.
- Per-filter warnings, acknowledgement, desktop notifications and optional sound.
- Configurable colours, background transparency, fonts, window decoration and autostart.
- Diagnostics with raw JSON, hexadecimal codes and a full copyable report.
- Server-side sandboxed Lua automation for status, connection, alarm and time
  events, including complete `between` day/night rules, with an exclusive editor on clients.
- Manual operating changes suspend Lua server-wide. A slowly blinking power
  button reports the override on every client and resumes/re-evaluates Lua with
  one click; lighting and child-lock changes do not suspend it.

![Light theme rendered by the actual Qt application in demo mode](docs/images/desklet-light.png)

## Install on Fedora

```bash
# On the machine next to the purifier (server only, no Qt required):
sudo dnf install -y gcc-c++ cmake ninja-build openssl-devel json-devel python3 python3-matplotlib
bash install.sh --server

# On the desktop machine (desklet and command-line client):
sudo dnf install -y gcc-c++ cmake ninja-build qt6-qtbase-devel qt6-qtsvg \
  python3 dejavu-sans-fonts
bash install.sh --client
$HOME/.local/bin/airctrl-desklet --demo
```

Running `bash install.sh` without a role installs both components on the same
machine. `-s` and `-c` are the short forms and may be combined. Use
`--prefix /absolute/path` to override the prefix for all selected roles. By
default the server is installed in `/usr/local/bin`, while the client and
desklet remain in `$HOME/.local/bin`. Run the script itself without `sudo`; it asks
for elevated privileges only for the server install and initial system config.
The installer also writes `compile_commands.json` in the project directory so
clangd/VSCodium can resolve generated headers such as `airctrl_version.hpp`.
In an already open session, run **clangd: Restart language server** or
**Developer: Reload Window** once.

The device endpoint is configured only in `/etc/airctrld.cfg`:

```ini
[server]
listen_address=0.0.0.0
port=5680

[logging]
status_file=/var/log/airctrl.log

[automation]
enabled=false

[device]
host=AC2729-10
port=5683
local_port=5680
initial_status_ms=120000
idle_ms=90000
keepalive_ms=20000
observe_refreshes=1
cancel_grace_ms=300
```

The server appends every valid confirmed status to `/var/log/airctrl.log` as a
CSV row with an ISO UTC timestamp. The header fixes the field order, booleans
are written as `0/1`, and fields introduced later are preserved in
`_extra_json`. The installer keeps existing data, sets mode `0640`, and installs
a 10 MiB size rotation. The log may contain `DeviceId` and `ProductId`; review
it before sharing.

The installed headless Matplotlib tool draws numeric and boolean columns in a
multi-page PDF with four labeled panels per page:

```bash
/usr/local/bin/airctrl-plot /var/log/airctrl.log "$HOME/airctrl-status.pdf"
```

Text columns and device identifiers remain in the CSV but are not coerced onto
numeric axes.

The desklet settings contain only the AirControl server endpoint, by default
`localhost:5680`. Clients never receive the device hostname or UDP port. The server
is installed under `/usr/local`; the client remains per-user under `$HOME/.local`.
Python is used by the installer; GUI, server and command-line client are C++ programs.

The installer enables the per-user `airctrl-server.service`. In these client calls, replace `server` with the actual hostname or IP address:

```bash
airctrl-client --host server --port 5680 status
airctrl-client --host server --port 5680 watch
airctrl-client --host server --port 5680 set pwr=1
airctrl-client --host server set mode=S om=s uil=0
airctrl-client --host server refresh
```

Clients use line-delimited JSON over TCP. This protocol has no authentication
or encryption; expose TCP port 5680 only to trusted hosts on the local network.
Allow the fixed UDP device port only from the AC2729 address so delayed Observe
notifications are not rejected after connection tracking expires.

Server dependencies: C and C++17 compilers, CMake ≥ 3.16, OpenSSL Crypto,
nlohmann/json ≥ 3.9 and Threads; **Qt is not required**. Matplotlib is optional
and used only for the status PDF. The client additionally
needs Qt ≥ 6.2 (Core/Gui/Widgets/DBus/Network). Presets require
CMake ≥ 3.21 and Ninja.
[Build instructions and other distributions](docs/DEVELOPMENT.md)

## Controls and warnings

Right-click opens settings; drag a value or status area to move the widget.
F1 opens diagnostics, F5 explicitly reconnects. Left-click either circle for alarms.
Power stays clickable: orange when disconnected, white when off, green when on.

Data age defaults to green below 45 s, yellow from 45 s, red from 90 s.
The alarm circle is independent of data freshness. Both are 26 px at the default font.

## Lua automation

Open **right-click → Lua-Automatik** to acquire the server-wide edit lock and
download the current server script. Saving sends it back for validation, atomic
storage and reload. Only one client may edit at a time; closing the editor or
losing the TCP connection releases the lock.

Automation is disabled by default. Once enabled it runs only in
`airctrl-server`, even when no desklet is open. The supplied example schedules night mode at 22:00 and
automatic day mode at 07:00. Its comments also form a complete event, status-field
and control-value reference. `on_event(event)` receives `startup`, `time`,
`connected`, `disconnected`, `status`, `alarm` and `command`; status events expose
both `event.status` and `event.changed`. Holding the timer button for at least
800 ms additionally calls the global server function `on_long_timer()` without
arguments. `airctrl.set { ... }` uses the same field
allow-list, serialized device queue and confirmed-state command path as the UI.

The verified official Lua 5.4.9 sources are embedded only in the Qt-free server.
The sandbox exposes no API
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

A captured v1.04 recovery starts after **90 s** without status, opens a new
session after another 9.7 s, and receives its first status 36.4 s after
registration: **136.1 s** without fresh data in total. A separate 65.8-s pause
while powered off does not restart the session.
[Packet evidence from 8 September](docs/PROTOCOL_VALIDATION_2026-09-08.md)

The **13 September 2026** capture identifies the current reconnect loop. Three
valid status notifications arrive 35–55 seconds after registration, but the
server host immediately rejects them with ICMP “administratively prohibited”.
When the first notification arrives after only 25 seconds, the same socket stays
healthy for eleven notifications including a 77-second quiet period. The server
therefore now uses a configurable fixed UDP source port, a 20-second keepalive,
a longer initial deadline and one same-session Observe refresh.
[Firewall evidence and hardening](docs/PROTOCOL_VALIDATION_2026-09-13.md)

On this AC2729/10, `dt=6` is followed by `dtrs=360`, then `359` about a minute
later. This supports interpreting `dtrs` as remaining timer minutes, but is
an observation, not a Philips specification. The raw field remains read-only.

Real-device reception/control and Fedora 44 + Cinnamon + X11 are confirmed by
the project maintainer. Wayland-aware handling exists, but full native Wayland verification is
outstanding. The v1.06 TCP multi-client build, central configuration and new UDP
hardening tests are locally verified; the hardened build still needs a physical
post-installation confirmation on Fedora.
Other Philips models are not claimed compatible.
[Local results and limitations](VALIDATION.md). The prepared GitHub workflow is
not a claim of an already successful CI run.

```bash
cmake --preset debug-server
cmake --build --preset debug-server --parallel 2
cmake --preset debug-client
cmake --build --preset debug-client --parallel 2
ctest --preset debug-client
python3 scripts/check_repository.py
```

[Contributing](CONTRIBUTING.md) · [Security](SECURITY.md) · [GitHub setup](docs/GITHUB_SETUP.md)

## Roles and acknowledgements

| Contributor | Role |
|---|---|
| **Project initiator (role)** | Project initiator, product owner and maintainer; requirements, UX direction, priorities, physical-device testing and release decisions |
| **OpenAI Codex** | AI development partner; collaborative C++/Qt/Lua implementation, protocol analysis, debugging, tests and documentation |
| **betaboon** | Author of upstream Python `aioairctrl`, underlying the server's internal C++ device transport |

Codex is credited as AI assistance, not a human maintainer or independent support
contact. The maintainer owns project decisions and publication. [Credits](AUTHORS.md)

## License

[MIT](LICENSE), with upstream notices retained. [Third-party notices](THIRD_PARTY_NOTICES.md)
System libraries are not bundled. This is an independent community project,
not an official Philips, Versuni or OpenAI product. Do not expose the device's UDP
service to the internet; protocol encryption does not replace network isolation.
