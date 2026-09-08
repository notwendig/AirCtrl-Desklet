# Third-party notices

AirCtrl-Desklet is distributed under the [MIT license](LICENSE).
The original upstream copyright notice is retained.

## Included source

- **aioairctrl / betaboon** — MIT. The `third_party/aioairctrl` directory contains
  the C++ port used by this application, derived from the Python original.
  See its [license](third_party/aioairctrl/LICENSE) and
  [provenance record](third_party/aioairctrl/ORIGIN.md), including the upstream
  commit. This port is not represented as an official upstream C++ release.
- **Lua 5.4.9 / Lua.org, PUC-Rio** — MIT. The verified official release source is
  embedded under `third_party/lua`; only the libraries required by the sandbox
  are built. See its [license](third_party/lua/LICENSE) and
  [provenance](third_party/lua/ORIGIN.md).

## System dependencies (not bundled in the source archive)

| Dependency | Use | Upstream licensing information |
|---|---|---|
| Qt 6: Core, Gui, Widgets, DBus, Network; Test for tests | Desktop UI, local Unix-socket IPC and event handling | [Qt licensing](https://www.qt.io/licensing/) — use the license applicable to the installed modules and distribution. |
| OpenSSL libcrypto | Protocol cryptography | [OpenSSL license](https://openssl-library.org/source/license/) |
| nlohmann/json | JSON handling in the AirControl server transport | [MIT license](https://github.com/nlohmann/json/blob/develop/LICENSE.MIT) |
| DejaVu Sans | Default UI font supplied by the OS | [DejaVu license](https://dejavu-fonts.github.io/License.html) |

The project's MIT license does not relicense these dependencies. Anyone
redistributing binaries must review the licenses and notices of the actual
libraries and fonts included in their distribution.

## Images and names

README screenshots and their provenance are documented in
[docs/images/README.md](docs/images/README.md). The public diagnostics example
uses synthetic values. It is not an uploaded device report.

Philips, Versuni, Qt, OpenAI and GitHub names identify compatibility or tools.
No endorsement, affiliation or trademark license is implied.
