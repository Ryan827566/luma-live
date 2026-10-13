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
3. A peer that accepts a request but never responds triggers a five-second socket inactivity timeout; failed transport closes the connection and clears cached identity. This is not a total request/connect deadline.
4. Empty edit fields return empty strings in the legacy diagnostic form. The production main entry now routes account access to the hosted page.
5. CMake no longer references the nonexistent server-local AuthCrypto.cpp. Shared authentication cryptography links into both client and server, including the standalone client build. Authentication assertions remain enabled in Release regression targets.

## Verification

- `luma_auth_integration_tests`: real local client/server protocol against an isolated in-memory store, including expired access/refresh, revoke/cache cleanup, cross-instance reset, verification/MFA, password changes and audit events.
- `luma_client_account_tests`: an unresponsive local peer, bounded receive failure and prompt Stop after failure.
- `scripts/windows/test_account_ui.py`: actual shipped EXE at 1024x700 with a separate test-only in-memory auth server; connection/port validation, required fields, mismatched passwords, register, incorrect/correct login, profile update, navigation retention, inline delete cancellation, device list, MFA on/off, activity, logout and recovery request.
- `scripts/windows/verify_client.py` includes these three checks in the existing actual-EXE regression, records the binary SHA256 and all logs. Check `output/client-verification.json` for the current run result rather than inferring that a build equals full acceptance.
- Account screenshots are under `output/ui-account-20261013`. Synthetic IME messages do not replace a real input-method or Narrator acceptance session.

Latest run: all 23 actual-EXE checks passed. EXE SHA256 `eccbe99b8d3bbba6f6c0ac58aa40b2f4ae7459e8ed2413db4ea3fb35d24b8732`. Compact Chinese sign-in/registration/recovery/connection captures were inspected.

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
