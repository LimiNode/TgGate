# TgGate

TgGate is a Windows-first local Telegram agent gateway. It exposes a deliberately
small MCP surface and keeps Telegram access behind application services.

The first milestone is intentionally safe and useful without a Telegram account:

- per-client, per-account, per-tool and per-chat policy with **default deny**;
- separate read/write tool registries;
- single-use, time-limited write approvals;
- audit events that never contain message text;
- a JSON-RPC/MCP adapter with no TDLib dependency;
- an isolated TDLib integration point for `td::ClientManager`.

## Architecture

```text
MCP / transport -> application services -> Telegram port -> TDLib
        |                  |
        v                  v
  policy + audit      approval service
```

`src/mcp` must never include TDLib headers. `src/ui` only calls application
services. A write always follows `prepare -> approve -> execute`; it is never a
direct MCP call.

## Build

Dependencies are top-level Git submodules in `external/`; no nested submodule
initialisation is required.

```powershell
git submodule update --init --jobs 8
cmake -S . -B build -DTGGATE_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

On Windows, use the provided presets. Ninja is the primary profile; the
Makefiles profile is available for compatibility.

```powershell
cmake --preset mingw-ninja
cmake --build --preset mingw-ninja
ctest --preset mingw-ninja
```

`tggate_imguix_window_smoke` opens an SFML window, renders the ImGuiX dark
theme for five frames, reports a readiness marker, and closes itself.

The first launch creates `data/config/client-profiles.json` and
`data/config/accounts.json` only when they are absent. `api_hash` is entered in
the desktop UI once and stored as `api_hash_dpapi` with Windows DPAPI; the SMS
code and 2FA password stay only in memory.

The default build includes an ImGuiX + SFML smoke window. TDLib and the HTTP
host remain opt-in integration targets:

```powershell
cmake -S . -B build -DTGGATE_ENABLE_HTTP_HOST=ON
```

When enabled, the desktop composition root loads `data/config/mcp-host.json`
and starts only on loopback. It implements the MCP `2026-07-28` HTTP binding
through one endpoint:

```text
POST /mcp
```

Every configured MCP profile receives a separate DPAPI-protected bearer token.
The token identifies the profile on the server; clients never choose a profile
with a request header. Requests also carry `MCP-Protocol-Version`, `Mcp-Method`,
and, for named operations, `Mcp-Name`; the JSON-RPC metadata must agree with
those headers. `server/discover`, `tools/list`, and `tools/call` are available.
CORS preflight and cross-origin requests are denied unless their exact origin
appears in `allowed_origins`. The host applies configured body and
concurrent-request limits. The desktop **Server** page can copy or explicitly
rotate an individual client's token; rotation immediately invalidates its old
token. This is local pre-shared bearer authentication, not an MCP OAuth
authorization server. Tokens are never written to logs. The integration test
exercises this flow against the real local HTTP server.

TDLib is compiled and installed separately, then connected through its CMake
package. It requires OpenSSL, zlib and gperf in addition to a C++17 compiler;
the exact location is supplied explicitly rather than silently falling back to
untracked system libraries:

```powershell
cmake -S . -B build -DTGGATE_ENABLE_TDLIB=ON -DTGGATE_TDLIB_PREFIX=C:/path/to/tdlib-install
```

See [docs/architecture.md](docs/architecture.md) and
[docs/dependencies.md](docs/dependencies.md).
