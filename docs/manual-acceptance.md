# Manual acceptance: TDLib reads and approved sends

This checklist exercises a real Telegram account without placing its phone
number, SMS code, 2FA password, API hash, bearer token, or message text in a
shell history, log, issue, or test fixture. Use a dedicated test account and a
test chat.

## Build and start

1. Build the complete local stack:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File tools/build-tdlib.ps1
   ```

2. Start the resulting `tggate_desktop.exe`. On first launch it creates the
   configuration templates in `data/config`.
3. Configure one enabled MCP client profile with only the target account,
   target chat id, and the read/write tool names it needs. Keep write policy as
   `require_approval`.
4. In the desktop Accounts page, enter the Telegram API id/hash and phone
   number, then submit the code and optional 2FA password in the UI. Do not
   paste these values into a terminal. Wait for `Authorized`.
5. Enable the local MCP host and copy the per-client bearer token only into the
   MCP client configuration. Treat it as a password.

## Read verification

Use the configured MCP client to call:

1. `telegram_list_chats` with the test account id. Confirm only allowlisted
   chats are returned.
2. `telegram_get_messages` with the allowlisted chat id and a small limit.
   Confirm text is returned as Telegram-sourced, untrusted external content.

## Approved write verification

1. Call `telegram_prepare_send_message` for the same allowlisted account and
   chat. Confirm the result is `pending_local_approval` and that no Telegram
   message has appeared.
2. Approve the displayed action in the desktop Pending Actions page before its
   expiry. Call `telegram_execute_approved_action` using the action id. Confirm
   the result has `ok: true` and `status: "sent"`, then confirm exactly one
   message appears in the test chat.
3. Call execute again with the same id. It must fail and send no second
   message.
4. Prepare a second action, enable Lockdown in the desktop UI, then attempt to
   execute it. It must fail and send no message.

If execution returns `status: "delivery_unknown"`, do not prepare or execute a
retry automatically: the approved action has already been consumed and Telegram
may still accept the original message. Reconcile the outcome manually in the
test chat before deciding whether a new, separately approved action is needed.

Inspect audit output only for metadata: it must contain neither message text
nor credentials. Remove the test message manually when finished. Finally stop
the desktop host and confirm the process shuts down without hanging or
crashing during TDLib shutdown.
