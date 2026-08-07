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
| `external/tdlib` | Telegram client implementation, built separately upstream |

Current versions are pinned by the gitlinks in the parent repository. Update a
dependency only through a reviewed commit which updates both its gitlink and
this document when its role or build requirements change.

Further GUI features add their direct ImGuiX dependencies to this same directory
(not `external/ImGuiX/libs`). This preserves the linear graph and follows the
bootstrap convention in `mgc-platform`.

TDLib itself requires OpenSSL, zlib and the build-time `gperf` executable. The
current MinGW installation does not provide them, so TDLib is intentionally not
enabled by default. Before enabling it, we will vendor a reviewed Windows build
of these prerequisites under `external/` (or a reviewed TDLib SDK bundle that
contains them) and point `TGGATE_TDLIB_PREFIX` at the resulting installation.
