CREATE TABLE IF NOT EXISTS luma_auth_users (
    user_id TEXT PRIMARY KEY,
    username TEXT NOT NULL,
    email TEXT NOT NULL,
    display_name TEXT NOT NULL,
    avatar_url TEXT NOT NULL DEFAULT '',
    phone TEXT NOT NULL DEFAULT '',
    password_salt TEXT NOT NULL,
    password_verifier TEXT NOT NULL,
    password_kdf_iterations INTEGER NOT NULL DEFAULT 600000,
    email_verified BOOLEAN NOT NULL DEFAULT FALSE,
    phone_verified BOOLEAN NOT NULL DEFAULT FALSE,
    mfa_enabled BOOLEAN NOT NULL DEFAULT FALSE,
    mfa_recovery_hash TEXT NOT NULL DEFAULT '',
    mfa_totp_secret_hex TEXT NOT NULL DEFAULT '',
    email_verify_hash TEXT NOT NULL DEFAULT '',
    email_verify_expires BIGINT NOT NULL DEFAULT 0,
    reset_token_hash TEXT NOT NULL DEFAULT '',
    reset_token_expires BIGINT NOT NULL DEFAULT 0
);

CREATE UNIQUE INDEX IF NOT EXISTS luma_auth_users_username_idx
    ON luma_auth_users (lower(username));

CREATE UNIQUE INDEX IF NOT EXISTS luma_auth_users_email_idx
    ON luma_auth_users (lower(email));

CREATE UNIQUE INDEX IF NOT EXISTS luma_auth_users_phone_idx
    ON luma_auth_users (phone)
    WHERE phone <> '';

CREATE TABLE IF NOT EXISTS luma_auth_security_events (
    event_id TEXT PRIMARY KEY,
    user_id TEXT NOT NULL,
    type TEXT NOT NULL,
    detail TEXT NOT NULL,
    created_at BIGINT NOT NULL
);

CREATE INDEX IF NOT EXISTS luma_auth_security_events_user_idx
    ON luma_auth_security_events (user_id, created_at DESC);
