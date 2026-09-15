# Little Camera protocol

The single source of truth for how the three parties talk. The firmware (`firmware/src/sync.cpp`), the iOS bridge (`ios/LittleCamera/BLE/Protocol.swift`) and the server (`server/src/routes/camera.ts`) are all written from this file. Change it here first.

```
┌──────────┐   BLE (GATT)   ┌──────────┐   HTTPS (JSON / PBM)   ┌──────────┐
│  camera  │ ─────────────▶ │  iPhone  │ ─────────────────────▶ │  server  │
│ ESP32-S3 │ ◀───────────── │  bridge  │ ◀───────────────────── │  blog    │
└──────────┘                └──────────┘                        └──────────┘
```

The phone is a dumb pipe. It never authenticates as a person; it authenticates **as the camera**, using an id and a secret it read from the camera over an encrypted BLE link. Everything that needs a human (email verification, the QR verification picture, approving subscribers) happens on the server, on the web, or is exposed to the phone as camera-scoped API calls.

All multi-byte integers on BLE are **little-endian**. All timestamps are **unix seconds, UTC**.

---

## 1. Identity

| Name | Definition | Example |
|---|---|---|
| `camera_id` | The camera's Bluetooth MAC (`esp_read_mac(..., ESP_MAC_BT)`), 12 lowercase hex characters, no separators | `7cdfa1e2b3c4` |
| `secret` | 16 random bytes generated once on first boot (`esp_fill_random`), stored in NVS. Sent as 32 lowercase hex characters over HTTP | `9f2c…` |
| `short_code` | 6 characters derived from `camera_id` (below). Shown in the app; typed on the web as the fallback when the QR photo cannot be decoded | `KQ7M2X` |
| BLE device name | `lc-` + last 4 hex characters of `camera_id`, uppercase | `lc-B3C4` |

### Alphabet

Codes that a human may have to read or type use this 32-character alphabet (no `0 O 1 I`):

```
ABCDEFGHJKLMNPQRSTUVWXYZ23456789
```

Index 0 = `A` … index 31 = `9`.

### `short_code` derivation

```
h = sha256(ascii bytes of camera_id)          # 32 bytes
v = h[0]<<32 | h[1]<<24 | h[2]<<16 | h[3]<<8 | h[4]   # first 40 bits, big-endian
code = ""
for i in 0..5:
    code += ALPHABET[(v >> (35 - 5*i)) & 31]   # six 5-bit groups from the top, 30 bits used
```

Test vectors (the C, Swift and TS implementations are all checked against these):

| `camera_id` | `short_code` |
|---|---|
| `7cdfa1e2b3c4` | `MM48F3` |
| `000000000000` | `882TLC` |
| `ffffffffffff` | `XMPHSF` |
| `a1b2c3d4e5f6` | `ZZWB7E` |

---

## 2. Photo file format

Photos are binary **PBM (P4)**, 320×240, exactly as the firmware stores them. A set bit is **black** (PBM convention). Rows are 40 bytes, MSB first, no padding. Files written by firmware from this version on carry one comment line:

```
P4
# boot=17 up=48213 t=1757789000
320 240
<9600 bytes of raster>
```

| Field | Meaning |
|---|---|
| `boot` | Boot counter (NVS) at capture time |
| `up` | `millis()` at capture time |
| `t` | Unix time at capture time, `0` if the camera did not know the time (no phone had connected since boot) |

Files from older firmware have no comment line. Every reader must accept both. Readers must skip any `#` comment between tokens, per the PBM spec.

Dating rule (applied by the phone at sync time, and by the server as a fallback):

1. `t > 0` → `captured_at = t`, source `camera`.
2. else if `boot == Info.boot` (same boot as now) → `captured_at = now − (Info.uptime_ms − up) / 1000`, source `phone`.
3. else → `captured_at = upload time`, source `upload`. Ordering by `camera_index` is preserved server-side by subtracting one second per position when several such photos arrive in one batch.

---

## 3. BLE GATT service

Base UUID `1C0000xx-4C43-4D52-8000-6C6974746C65` (`4C43 4D52` = "LCMR", suffix spells `little`).

| xx | Name | Properties | Size |
|---|---|---|---|
| `01` | Service | advertised in the primary advertisement | |
| `02` | `Info` | READ | 25 bytes |
| `03` | `Secret` | READ, **encrypted + authenticated** (bonded, passkey) | 16 bytes |
| `04` | `Control` | WRITE (with response) | 1–7 bytes |
| `05` | `Data` | NOTIFY | up to MTU − 3 |

Advertising: connectable, 100 ms interval, service UUID `…01` in the advertisement so iOS background scans match it. The camera advertises while awake and for a while behind the sleep face, not in light sleep (see §3.6).

MTU: the camera requests 512. The usable notification payload is `MTU − 3` ATT bytes; the frame header takes 3, so `chunk = MTU − 6` (iOS typically grants 185 or 251 → 179 or 245-byte chunks; a 9.6 KB photo is ~40–55 notifications).

### 3.1 `Info` (READ)

| Offset | Type | Field |
|---|---|---|
| 0 | u8 | `version` = 1 |
| 1 | u8[6] | `mac` (same bytes as `camera_id`, in order) |
| 7 | u16 | `photo_count` |
| 9 | u16 | `unsynced_count` |
| 11 | u16 | `newest_index` (0 if no photos) |
| 13 | u16 | `boot` |
| 15 | u32 | `uptime_ms` |
| 19 | u32 | `epoch` (0 if the clock is unset) |
| 23 | u8 | `flags`: bit0 `time_valid`, bit1 `storage_ok`, bit2 `busy` (a stream is running) |
| 24 | u8 | reserved (0) |

Read it after connecting and after every `ACK`; it is not notified.

### 3.2 `Secret` (READ, encrypted)

16 raw bytes. Reading it triggers pairing on first contact: the camera uses **passkey display** (it shows `pair 123456` on its screen), the phone asks the user to type it. After bonding the read succeeds silently on every reconnect. The phone stores the secret in the Keychain and never asks again.

### 3.3 `Control` (WRITE)

`[u8 op][args…]`

| op | Name | Args | Effect |
|---|---|---|---|
| `0x01` | `SET_TIME` | `u32 epoch` [, `i16 utc_offset_min`] | Sets the camera clock. Photos taken afterwards carry `t=`. The optional offset (minutes east of UTC, e.g. 120 for CEST) gives the camera local time for its night rule ([mood.md](mood.md)); a 4-byte write is still accepted and leaves any stored offset alone |
| `0x02` | `LIST` | `u16 from_index`, `u8 flags` (bit0 = unsynced only) | Streams `LIST_DATA` frames, ascending index, starting at `from_index` (inclusive) |
| `0x03` | `GET` | `u16 index`, `u32 offset` | Streams the raw file (header + raster) from `offset` as `PHOTO_DATA` frames |
| `0x04` | `ACK` | `u16 index` | The phone has the file safely. Camera marks it synced (rename `/0007.pbm` → `/0007s.pbm`) |
| `0x05` | `DELETE` | `u16 index` | Deletes the file. The bridge does not use this by default |
| `0x06` | `ABORT` | | Stops the running stream |

Only one stream runs at a time; a `LIST`/`GET` while busy ends immediately with status `busy`. Writes are processed on the camera's main loop, not in the BLE callback, so a response frame can lag a write by one loop iteration (~30 ms).

### 3.4 `Data` (NOTIFY)

Frame: `[u8 kind][u16 seq][payload]`

| kind | Name | Payload |
|---|---|---|
| `0x01` | `LIST_DATA` | 1..N entries of 16 bytes (below) |
| `0x02` | `PHOTO_DATA` | raw file bytes |
| `0x7F` | `END` | `u8 op`, `u8 status`, `u32 total_len`, `u32 crc32` |

`seq` starts at 0 for each stream and increments by one per frame; `END` carries the next value. `total_len` is the number of payload bytes streamed (for `GET`: bytes from `offset` to end of file). `crc32` is the IEEE CRC-32 of those payload bytes (for `GET` with `offset > 0` it covers only the streamed part).

`status`: `0` ok, `1` not found, `2` aborted, `3` storage error, `4` busy.

`LIST_DATA` entry (16 bytes):

| Offset | Type | Field |
|---|---|---|
| 0 | u16 | `index` |
| 2 | u8 | `flags`: bit0 `synced`, bit1 `epoch_is_estimate` (from uptime), bit2 `epoch_from_clock` |
| 3 | u8 | reserved |
| 4 | u32 | `size` (bytes on disk, header included) |
| 8 | u32 | `epoch` (0 unknown; already computed with the dating rule in §2 where possible) |
| 12 | u32 | `uptime_ms` at capture (`up`) |

### 3.5 Bridge algorithm (what the phone does on every connection)

```
connect → read Info → if Secret unknown: read Secret (pair)
SET_TIME(now, utc_offset_min)
LIST(from_index = 1, flags = unsynced_only)
for each entry ascending:
    GET(index, 0)                     # resume with offset after a disconnect
    verify seq continuity + crc32
    upload to server (§4.3); 200/201/409 all count as success
    ACK(index)
read Info; done when unsynced_count == 0
```

### 3.6 Power rules

- The camera runs the radio while awake and for 30 s after the 10 s idle timeout: the sleep face is already up, but the chip keeps advertising behind it, which gives a backgrounded iPhone time to notice.
- While a phone is connected and active (connect, write, notification, ACK within the last 60 s) that window stretches.
- Then light sleep tears the BLE stack down; a one-second hold on the shutter wakes the camera and brings the radio back.
- Pressing the shutter always wins; a transfer in progress simply continues alongside.

---

## 4. HTTP camera API

Base: `https://<apex>/api/camera`. JSON unless noted. Errors are `{ "error": "<code>", "message": "<human text>" }`.

### 4.1 Authentication

```
Authorization: Camera <camera_id>:<secret_hex>
```

Every route except `hello` requires it. The server stores `sha256(secret)` and compares in constant time.

### 4.2 `POST /hello`

Body `{ "camera_id": "7cdfa1e2b3c4", "secret": "<32 hex>" }`.
First contact creates the camera (trust on first use). Later calls must present the same secret.

Response `200`:

```json
{
  "camera_id": "7cdfa1e2b3c4",
  "short_code": "KQ7M2X",
  "bound": null | { "handle": "mees", "name": "Mees", "url": "https://mees.example.com" },
  "server_time": 1757789000
}
```

### 4.3 `POST /photos`

Raw PBM body.

| Header | Required | Meaning |
|---|---|---|
| `Content-Type: image/x-portable-bitmap` | yes | |
| `X-Photo-Index: 7` | yes | `index` on the camera |
| `X-Captured-At: 1757789000` | no | phone's estimate when the file has `t=0` |
| `X-Captured-At-Source: phone` | no | `phone` or `upload` |

Size limit 16 KB. Must parse as `P4 320 240`.

Response `201` (new) or `200` (`"status": "duplicate"`, same `camera_id` + sha256 seen before):

```json
{
  "id": "k3m9q2z8x1w4",
  "status": "stored" | "duplicate",
  "kind": "photo" | "verification" | "avatar",
  "bound": null | { "handle": "mees", "name": "Mees", "url": "https://mees.example.com" }
}
```

Server-side, in order: parse + hash → dedupe → date (rule in §2) → if the camera is unbound and any verification code is live, try to decode a QR (§5) → if bound and an avatar was requested and `captured_at` is after the request, this photo becomes the avatar.

### 4.4 `GET /status`

```json
{
  "camera_id": "7cdfa1e2b3c4",
  "short_code": "KQ7M2X",
  "bound": null | { "handle": "mees", "name": "Mees", "url": "…", "photo_count": 12, "battery": 68 },
  "avatar": { "requested_at": 1757789000 | null, "has_avatar": false },
  "subscribers": [ { "id": 3, "email": "sanne@example.com", "name": "Sanne" | null, "status": "pending" | "approved" | "blocked", "added_by": "self" | "owner" } ],
  "server_time": 1757789000
}
```

`subscribers` is empty and `bound` is `null` until the camera is bound.

### 4.5 Subscribers (bound cameras only)

- `POST /subscribers` `{ "email": "…", "name": "…" | null }` → `201 { "id": 4 }`. Owner-added subscribers are approved immediately and receive a welcome link.
- `POST /subscribers/:id/approve` → `200`. Sends the welcome link.
- `POST /subscribers/:id/block` → `200`.

### 4.6 `POST /request-avatar`

`{}` → `200 { "requested_at": 1757789000 }`. The next photo captured after this moment becomes the profile picture.

---

## 5. Verification picture

The web page `https://<apex>/me` for a verified-but-unbound profile shows a QR code. Payload:

```
LC:<6 chars from the alphabet in §1>
```

QR version 1 (21×21 modules), error correction **H**, rendered as large as the phone's screen allows with a 4-module quiet zone on white. Code lifetime 15 minutes; the page regenerates on reload.

The server decodes the uploaded 1-bit photo with these variants until one yields `^LC:[A-Z2-9]{6}$`: `{as-is, 3×3 majority filter}` × nearest-neighbour scale `{2, 3, 1}`, with both polarities. A match binds `camera_id` to the profile, tags the photo `kind = verification`, back-fills the camera's earlier photos onto the profile and emails the owner.

Fallback: the owner types the `short_code` from the app on the same page. It is accepted only if that camera has uploaded a photo within the last 10 minutes (proof of possession).

---

## 6. USB serial console (export fallback)

The firmware answers on the USB-CDC console (115200). Lines end with `\n`.

| Command | Reply |
|---|---|
| `ls` | one line per photo: `<index> <size> <synced 0/1>`, then `ok` |
| `get <index>` | `begin <index> <size>`, then the file as hex, 64 bytes per line, then `end <crc32 hex>` |
| `stat` | `photos=<n> used=<bytes> total=<bytes> boot=<n> id=<camera_id>` |

Anything else → `err unknown`. `firmware/tools/export/pull_serial.py` speaks this.
