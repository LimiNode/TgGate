# Architecture

The project follows the same directional layering used by `mgc-platform`:

```text
apps/ (future composition roots)
  -> ui/ and mcp/ adapters
  -> application/ use cases and ports
  -> domain/ policy and approval rules
  -> infrastructure/ TDLib, DPAPI, SQLite and transports
```

Dependencies point inward. In particular:

- `mcp` has no TDLib include or link dependency;
- `application` depends on `ITelegramService`, never `TdAccount`;
- `PolicyEngine` has no side effects;
- UI may approve/deny an action, but may not send a Telegram message itself;
- only `TdAccount` owns `td::ClientManager` and its receive thread.

## Security invariants

1. A missing profile, account, tool, or chat match is a denial.
2. The read and write MCP endpoints get distinct registries; neither reveals the other surface.
3. Telegram message content is returned only as structured `source=telegram`,
   `trust=untrusted_external_content` data. It is never interpreted as an instruction.
4. Every write first creates an expiring single-use action. Execution re-evaluates the current policy.
5. Audit entries record metadata and result state, never message text or arguments.
6. `lockdown()` denies all outstanding actions. The production host also pauses write registrations and revokes sessions atomically.

## Transport

The transport parses a request, validates its per-client DPAPI-protected bearer
token, and resolves that token to an enabled `McpClientProfile` before calling
an MCP protocol handler. A caller cannot select or override the profile with a
request header. It does not access TDLib. `LocalMcpHttpService` owns that
lifecycle and policy boundary; `SimpleWebHttpHost` only adapts ordinary HTTP
requests to `IHttpHost`.

The Simple-Web-Server integration is opt-in. It uses ordinary loopback HTTP
JSON-RPC: the `2026-07-28` adapter exposes one `POST /mcp` endpoint. It requires
matching `MCP-Protocol-Version`, `Mcp-Method`, and (when applicable) `Mcp-Name`
headers plus matching JSON-RPC metadata; batches are rejected. It provides
`server/discover`, `tools/list`, and `tools/call`. WebSocket is not a required
MCP transport here. The host rejects unknown paths, invalid bearer tokens,
disabled profiles, and origins absent from `allowed_origins`; it also applies
request-size, concurrent-request, and request/content-time limits. It requires
`Content-Type: application/json` and an `Accept` header covering both JSON and
SSE. An allowed browser origin receives
a narrow `OPTIONS` preflight response for `POST` with only the required MCP
headers. The stdio bridge is a separate process and forwards only to a
loopback, token-authenticated desktop host.

## TDLib account lifecycle

`TdAccount` is the only `td::ClientManager` owner. It drives TDLib's
authorization updates, sends phone/code/2FA inputs only while TDLib requests
them, and owns the decrypted API hash and database-encryption key in
`SecretBuffer` for the active lifecycle. The DPAPI-protected database key is
passed to `setTdlibParameters`; it is never persisted as plaintext.

`v2026_07_28` is the implemented protocol adapter. Future revisions must be
independent handlers rather than scattered version checks.

`mcp-host.json` is written as schema version 2 with one protected credential
per client profile. A legacy schema version 1 file is migrated on startup only
when exactly one profile exists: its token is verified with DPAPI, bound to that
profile, and written back as schema version 2. A legacy shared token is never
silently assigned across several profiles.
