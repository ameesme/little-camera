-- Little Camera schema. All timestamps are unix seconds (UTC).
-- Handles and emails compare case-insensitively (NOCASE) so that a
-- unique index catches Mees@Example.com vs mees@example.com.

CREATE TABLE profiles (
  id                  INTEGER PRIMARY KEY AUTOINCREMENT,
  handle              TEXT NOT NULL COLLATE NOCASE UNIQUE,
  name                TEXT NOT NULL,
  email               TEXT NOT NULL COLLATE NOCASE UNIQUE,
  email_verified_at   INTEGER,
  avatar_photo_id     INTEGER REFERENCES photos(id) ON DELETE SET NULL,
  avatar_requested_at INTEGER,
  created_at          INTEGER NOT NULL
);

CREATE TABLE cameras (
  id          TEXT PRIMARY KEY,               -- 12 hex chars, the BLE MAC
  secret_hash TEXT NOT NULL,                  -- sha256 of the 32-hex secret
  short_code  TEXT NOT NULL UNIQUE,           -- derived from id, see protocol §1
  profile_id  INTEGER REFERENCES profiles(id) ON DELETE SET NULL,
  bound_at    INTEGER,
  first_seen  INTEGER NOT NULL,
  last_seen   INTEGER NOT NULL
);
CREATE INDEX cameras_profile ON cameras(profile_id);

CREATE TABLE photos (
  id                 INTEGER PRIMARY KEY AUTOINCREMENT,
  public_id          TEXT NOT NULL UNIQUE,    -- 12 chars, used in URLs
  camera_id          TEXT NOT NULL REFERENCES cameras(id) ON DELETE CASCADE,
  profile_id         INTEGER REFERENCES profiles(id) ON DELETE SET NULL,  -- NULL while the camera is unbound
  kind               TEXT NOT NULL CHECK (kind IN ('photo', 'verification', 'avatar')),
  sha256             TEXT NOT NULL,
  path               TEXT NOT NULL,           -- relative to DATA_DIR
  camera_index       INTEGER NOT NULL,
  captured_at        INTEGER NOT NULL,
  captured_at_source TEXT NOT NULL CHECK (captured_at_source IN ('camera', 'phone', 'upload')),
  uploaded_at        INTEGER NOT NULL,
  UNIQUE (camera_id, sha256)
);
CREATE INDEX photos_feed ON photos(profile_id, uploaded_at DESC, id DESC);

CREATE TABLE verification_codes (
  code       TEXT PRIMARY KEY,                -- the 6 chars after "LC:"
  profile_id INTEGER NOT NULL REFERENCES profiles(id) ON DELETE CASCADE,
  created_at INTEGER NOT NULL,
  expires_at INTEGER NOT NULL,
  attempts   INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE subscribers (
  id               INTEGER PRIMARY KEY AUTOINCREMENT,
  profile_id       INTEGER NOT NULL REFERENCES profiles(id) ON DELETE CASCADE,
  email            TEXT NOT NULL COLLATE NOCASE,
  name             TEXT,
  status           TEXT NOT NULL CHECK (status IN ('pending', 'approved', 'blocked')),
  added_by         TEXT NOT NULL CHECK (added_by IN ('owner', 'self')),
  created_at       INTEGER NOT NULL,
  approved_at      INTEGER,
  last_notified_at INTEGER,
  UNIQUE (profile_id, email)
);

CREATE TABLE access_tokens (
  id            INTEGER PRIMARY KEY AUTOINCREMENT,
  token_hash    TEXT NOT NULL UNIQUE,         -- sha256 of the token; the token itself is only in the email
  kind          TEXT NOT NULL CHECK (kind IN ('subscriber', 'owner_login', 'email_verify')),
  profile_id    INTEGER NOT NULL REFERENCES profiles(id) ON DELETE CASCADE,
  subscriber_id INTEGER REFERENCES subscribers(id) ON DELETE CASCADE,
  photo_id      INTEGER REFERENCES photos(id) ON DELETE SET NULL,
  expires_at    INTEGER NOT NULL,
  first_used_at INTEGER,
  created_at    INTEGER NOT NULL
);

CREATE TABLE comments (
  id            INTEGER PRIMARY KEY AUTOINCREMENT,
  photo_id      INTEGER NOT NULL REFERENCES photos(id) ON DELETE CASCADE,
  subscriber_id INTEGER REFERENCES subscribers(id) ON DELETE CASCADE,  -- NULL = the owner
  body          TEXT NOT NULL CHECK (length(body) <= 1000),
  created_at    INTEGER NOT NULL
);
CREATE INDEX comments_photo ON comments(photo_id, id);

CREATE TABLE email_outbox (
  id         INTEGER PRIMARY KEY AUTOINCREMENT,
  to_email   TEXT NOT NULL,
  subject    TEXT NOT NULL,
  text_body  TEXT NOT NULL,
  html_body  TEXT NOT NULL,
  -- Optional inline picture: the newsletter attaches the newest photo as
  -- cid:photo. Storing the photo id (not the bytes) keeps the outbox small
  -- and lets the drainer render the PNG when it sends.
  photo_id   INTEGER REFERENCES photos(id) ON DELETE SET NULL,
  status     TEXT NOT NULL CHECK (status IN ('pending', 'sent', 'failed')) DEFAULT 'pending',
  attempts   INTEGER NOT NULL DEFAULT 0,
  last_error TEXT,
  created_at INTEGER NOT NULL,
  sent_at    INTEGER
);
CREATE INDEX email_outbox_pending ON email_outbox(status, id);
