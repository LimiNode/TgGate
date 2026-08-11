# Dependencies

All source dependencies are first-level Git submodules in `external/`.
Do not initialise a nested dependency tree. When a dependency brings a shared
library (such as JSON or Asio), add that repository once at `external/<name>`
and make every consumer point to it.

| Directory | Purpose |
| --- | --- |
| `external/ImGuiX` | Desktop operator UI framework |
| `external/imgui`, `external/imgui-sfml`, `external/SFML` | Dear ImGui and the SFML backend |
| `external/fmt`, `external/json` | Shared formatting and JSON dependencies |
| `external/hmac-cpp` | Upstream secure-buffer primitive, pinned at the reviewed `main` security fixes |
| `external/freetype`, `external/dlg` | Direct font stack modules retained in the top-level graph |
| `external/Simple-Web-Server` | Local HTTP/JSON-RPC transport foundation |
| `external/asio` | Standalone Asio for the transport |
| `external/openssl` | Reviewed Win64 OpenSSL 3.4.0 package used when building TDLib |
| `external/zlib` | zlib 1.3.2, built statically for TDLib |
| `external/gperf` | GNU gperf 3.1 source, built as a local TDLib build tool |
| `external/tdlib` | Telegram client implementation, built separately upstream |

Current versions are pinned by the gitlinks in the parent repository. Update a
dependency only through a reviewed commit which updates both its gitlink and
this document when its role or build requirements change.

Further GUI features add their direct ImGuiX dependencies to this same directory
(not `external/ImGuiX/libs`). This preserves the linear graph and follows the
bootstrap convention in `mgc-platform`.

TDLib itself requires OpenSSL, zlib and the build-time `gperf` executable.
`external/openssl` provides the reviewed Win64 OpenSSL 3.4.0 package. Its
`lib/VC/x64/MD` import libraries are compatible with the project MinGW toolchain;
the matching DLLs live in `external/openssl/bin` and must be available when a
TDLib-enabled executable runs. `external/zlib` and `external/gperf` complete
the remaining prerequisites. Run `tools/build-tdlib.ps1` to build these local
dependencies, build and install TDLib, compile the real TgGate adapter target,
and run its smoke test. The resulting TDLib installation is supplied explicitly
through `TGGATE_TDLIB_PREFIX`.
