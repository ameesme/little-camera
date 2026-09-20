-- What firmware the bridge app last saw on a camera (protocol §4.2). Reported
-- by the phone, so it is a record and not a fact: it decides what /status
-- offers as an update and nothing else. The camera checks the image it is
-- actually given against its SHA-256 either way (§3.7).

ALTER TABLE cameras ADD COLUMN firmware_version TEXT;
ALTER TABLE cameras ADD COLUMN firmware_seen_at INTEGER;
