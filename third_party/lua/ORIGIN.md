# Lua source provenance

- Upstream: <https://www.lua.org/>
- Release: **Lua 5.4.9**, the final Lua 5.4 release
- Archive: `https://www.lua.org/ftp/lua-5.4.9.tar.gz`
- SHA-256: `2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6`
- License: MIT; retained in [LICENSE](LICENSE)

The source under `src/` is copied without functional modifications from the
verified official release. AirCtrl-Desklet builds only the interpreter core,
auxiliary library and the base, table, string, math and UTF-8 libraries. The
standalone interpreter/compiler and the `io`, `os`, `package`, `debug` and
coroutine libraries are not built into the application.
