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

Latest run: all 23 actual-EXE checks passed. EXE SHA256 `14427495494c0670884b65ff499f3ba68db7e6c8e68dcf818ee230655bca8266`. Compact Chinese sign-in/registration/recovery/connection captures were inspected.

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

Password visibility now works for registration, recovery, password changes and other password fields; it resets when changing pages. Actions follow the form instead of sitting at the window bottom. Native UI regression verifies masking/reveal/restoration and hidden reveal controls on profile, with all 23 checks passed. Compact Chinese captures are in output/ui-account-forms-20261013.


## 2026-10-14 authenticated workspace gate

Production starts at sign-in/registration only. Registration does not unlock modules. Valid sign-in shows the workspace and sidebar name/email. Direct hidden commands and legacy meeting switches cannot bypass authentication; meeting renders now use a separate diagnostic executable requiring an absolute output path.

Immutable UI snapshots avoid reading worker-owned service state. Quiet 60-second validation preserves edits and pending confirmations on success, and returns to sign-in on failure even while the account page is hidden. Pending confirmations no longer suspend validation. Already-running account requests finish before validation. Logout stops primary media and AI speech before destroying pages that may wait for HTTP, stops the owned gateway, and restores controls for re-login. Physical device/audio shutdown remains separate acceptance.

All 24 unified checks passed on client SHA256 `99ce034743201dbb3bffa463ebacf74baecde48a1eed8c1532778a1664d6bed4`; report `output/client-verification.json`. Checks combine native tests, actual-client workflows and independent meeting render diagnostics. New tests cover real local registration/sign-in, identity, locked commands, logout/re-entry, hidden pending confirmation during server loss, owned AI gateway logout cleanup and restart. Independent review findings were fixed and re-reviewed. No user database or real provider was contacted.

Compact Chinese/English captures: `output/ui-auth-gate-20261014/{zh-CN,en-US}`. Login, registration and account profile were inspected. Known visual follow-up: the English account title needs DT_NOPREFIX to render its ampersand literally; post-login account layout and product-wide final visual acceptance remain pending. Real SMS/email, TLS, PostgreSQL deployment, physical media/TURN/provider checks remain open.


## 2026-10-14 account settings visual refinement

The authenticated Account page now uses a centered, bounded content region and quiet native tabs. Form results and inline confirmations follow their initiating actions. Short profile/connection details use measured text height without a large empty frame; device, MFA and activity records retain native selection and scrolling. Literal ampersands in page titles render correctly. No authentication protocol or storage behavior changed.

All 24 unified checks passed on EXE SHA256 `5ed686e1eaf1a113d3398ef685c206cce5bcdfc2531513efc92cb2b322f070b2`. The account workflow additionally exercises all authenticated pages, five security modes, delete/device/MFA confirmations and cancellation, result proximity and short/long text scrolling at 1024x700 and 1386x788. The requested 1440x900 window was capped by Windows at 1386x788, so full 1440x900 and physical DPI acceptance are not claimed. The independent code review found no blocking issue.

Captures in `output/ui-account-refinement-20261014/en-US`, `zh-CN` and `zh-wide` include login, profile, deletion confirmation, security, password, device and connection pages. Their capture-manifest.json files bind every set to this EXE hash and actual client dimensions. English profile/delete and Chinese password/wide-profile images were inspected. Physical IME/Narrator/multiple monitors, real provider/media/TURN and final whole-product visual acceptance remain separate.
