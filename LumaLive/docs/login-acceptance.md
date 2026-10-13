# Account page and authentication acceptance

## Delivery scope (2026-10-13)

Development now takes place on `main`, at the user's request. The complete video/AI branch and account-registration branch were merged, preserving both histories. The original local index and changed files were backed up before reconciling the working copy to main.

The actual client at `output/next/Release/luma_studio.exe` hosts **Account** in its left sidebar. `--account` opens this page inside the same workspace, including the full application entry point. This is independent of the AI gateway; credentials are never sent to an AI service.

- Signed out: password or SMS sign-in, account creation with password confirmation, password recovery, service address/port.
- Signed in: profile editing, sign-out, inline account deletion, email/phone verification, password changes, two-step verification with a visible authenticator key and recovery code, security activity, device listing/removal/sign-out of other devices and session refresh.
- Background requests retain page state while the user switches modules. Closing the workspace does not wait for the UI worker. Password fields clear after submission; raw server diagnostics and tokens are not placed in status messages. The authenticator setup key/recovery code is deliberately displayed for the user to save, and clears when navigating account tabs.
- Confirmation captures the selected device identity; changing a phone number requires a matching new verification challenge. Escape cancels inline confirmation. Enter submits a focused single-line field except during IME composition.
- Simplified/traditional Chinese and English reuse the existing system-language resources, native edit accessibility, dark controls, keyboard focus and reduced-motion-aware hover treatment. Other system languages currently fall back to English.

## Corrected authentication defects

1. Natural access-token expiry no longer destroys a still-valid refresh session. Expired refresh credentials and revoked sessions are removed; client user/session caches clear on revocation.
2. Password reset accepts a valid token obtained by a different client instance and generates fresh password salt. The server validates token, salt and verifier formats.
3. Connection establishment and each complete protocol send/receive have a fixed five-second deadline. Partial bytes do not reset it. Failed transport closes the connection and clears cached identity. These are phase deadlines, not a five-second limit on an entire multi-step login or password derivation.
4. Empty edit fields return empty strings in the legacy diagnostic form. The production main entry now routes account access to the hosted page.
5. CMake no longer references the nonexistent server-local AuthCrypto.cpp. Shared authentication cryptography links into both client and server, including the standalone client build. Authentication assertions remain enabled in Release regression targets.

## Verification

- `luma_auth_integration_tests`: real local client/server protocol against an isolated in-memory store, including expired access/refresh, revoke/cache cleanup, cross-instance reset, verification/MFA, password changes and audit events.
- `luma_client_account_tests`: an unresponsive local peer, bounded receive failure and prompt Stop after failure.
- `scripts/windows/test_account_ui.py`: actual shipped EXE at 1024x700 with a separate test-only in-memory auth server; connection/port validation, required fields, mismatched passwords, register, incorrect/correct login, profile update, navigation retention, inline delete cancellation, device list, MFA on/off, activity, logout and recovery request.
- `scripts/windows/verify_client.py` includes these three checks in the existing actual-EXE regression, records the binary SHA256 and all logs. Check `output/client-verification.json` for the current run result rather than inferring that a build equals full acceptance.
- Account screenshots are under `output/ui-account-20261013`. Synthetic IME messages do not replace a real input-method or Narrator acceptance session.

Latest run: all 23 actual-EXE checks passed. EXE SHA256 `9b22c046cc4140e9d43a79786550f32ef11adc3354014991f741b8c3ae7bd68c`. Compact Chinese sign-in/registration/recovery/connection captures were inspected.

## How to try it

Open `output/next/Release/luma_studio.exe`, choose **Account**, then **Connection**. Use the address/port of your running authentication service (default 127.0.0.1:9100), connect and create/sign in to an account.

For automated local acceptance only, run `C:/Python314/python.exe scripts/windows/test_account_ui.py output/next/Release/luma_studio.exe` from the repository root. It launches and cleans up its own in-memory fixture and temporary accounts, without touching a configured PostgreSQL database.

## Remaining production gates

The login UI is connected to the repository's real account protocol. This does **not** complete production identity deployment. The bundled email/SMS providers currently implement development/test delivery only; production providers, PostgreSQL deployment/persistence acceptance and authenticated encrypted transport still require implementation/configuration and acceptance. No real email or SMS was sent in these tests. The UI does not persist passwords or login tokens; remembered-session behavior is not implemented. Physical multi-monitor DPI, real IME/Narrator and final visual review remain separate from automated geometry checks.

Additional keyboard validation on the same EXE passed queued Enter submission and simulated IME Enter suppression. The IME assertion checks that no request/page transition occurs; native composition is allowed to change edit text. Traditional Chinese compact account captures were also inspected. This does not certify a physical IME.

Account details now hide the scrollbar for short text and recalculate overflow after content, layout and DPI changes. Actual-EXE coverage verifies short/long/short transitions and scrolling security activity; all 22 checks passed on this binary.

## Connection settings persistence (2026-10-13)

A successful explicit connection saves only the IPv4 address and port under `%LOCALAPPDATA%/LumaLive/account-connection.conf`. A fresh client restores these fields; it does not reconnect automatically or remember credentials. Failed connections do not replace the saved endpoint. Saving runs with the request worker; a save error is shown separately while the successful connection stays usable. Malformed/oversized configuration falls back to defaults with a visible message and leaves the file untouched.

The native settings regression covers bounded parsing, strict IPv4/port validation, Unicode paths, atomic replacement failure and preservation of prior contents. Actual-EXE UI tests use a temporary `LUMALIVE_ACCOUNT_SETTINGS_PATH`, verify restart restoration and empty credentials, then verify failed connections and malformed files preserve saved bytes. Tests never modify the user's account connection settings.

After this integration, all 23 actual-EXE checks passed, including the added native account settings regression.

## Authentication challenge bounds (2026-10-13)

Registration, password login and password change now validate the entire server challenge before password derivation: packet type/count, 16-byte salts, 32-byte nonce, hexadecimal encoding, login MFA flag and decimal iteration count from 1 through the current protocol ceiling of 600000. Invalid parameters disconnect the client, clear cached identity and return the stable `invalid_challenge` error, displayed as invalid service authentication data rather than a user verification-code error.

The client regression adds 77 scripted loopback-server cases for malformed/oversized iteration counts and malformed fields. Each establishes a synthetic prior identity and verifies prompt failure without an exception or a proof/verifier reply, followed by cleared session state. A separate process with a ten-second watchdog prevents an old unbounded calculation from hanging the suite. Existing real local-server tests cover the normal 600000-iteration path. These bounds do not supply the still-missing authenticated encrypted transport.

The full 23-check verifier passed after the challenge-boundary fix, including the 77 malformed-server cases and normal local-server account workflows.

## Full account workflow and transport deadline follow-up (2026-10-13)

The native client now uses nonblocking sockets with monotonic deadlines for connect, send and receive. Regression cases cover a peer that keeps sending one byte every 200 ms, a silent peer, refused connections, fragmented responses and two protocol frames arriving in a single write. A timed-out authenticated connection loses cached identity. The explicit deadline of a black-holed connect and send-buffer saturation are not separately injected in these tests; they use the same deadline-aware transport path.

The hosted account test now supplies isolated email/SMS adapters through explicit service construction. Only the test fixture exposes captured synthetic codes through its private stdin/stdout pipe; no external delivery, user database or production debug-code response is used. Coverage includes delivery failures with localized status, invalid/correct email verification, phone-number/challenge mismatch, successful phone verification and SMS sign-in, incorrect/correct password changes, rejected old password, reset completed in a fresh client, consumed reset-token rejection and confirmed account deletion. This validates local end-to-end workflows, not production email/SMS providers.

All 23 actual-EXE checks passed after the deadline and delivery-flow changes. The real-provider, TLS, database-deployment and physical-device gates remain open.
