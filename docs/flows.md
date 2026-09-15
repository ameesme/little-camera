# Flows

Who does what, in order. Byte-level details live in [protocol.md](protocol.md).

## Registering a blog

1. Owner opens `https://<apex>/register` on their phone, enters **name**, **email**, and a **handle** (the subdomain).
2. Server emails a verification link (`email_verify` token, 24 h).
3. Clicking it marks the email verified, logs the owner in (signed `owner` cookie) and lands on `/me`.
4. `/me` shows the **verification picture**: a large QR code (`LC:XXXXXX`, 15 min) and the instruction to photograph it with the camera. Below it, a small form: "Camera not recognised? Type the code from the app."
5. Meanwhile the owner installs the bridge app, which finds the camera (`lc-XXXX`), pairs (passkey shown on the camera), reads the secret and calls `POST /api/camera/hello`.
6. The owner photographs the phone screen with the camera and presses send on the camera. The app pulls the photo and uploads it. The server decodes the QR, **binds the camera to the profile**, tags the photo `verification`, moves any earlier photos from that camera onto the profile, and emails "your camera is linked".
7. `/me` polls `/me/status` every 3 s and flips to "linked" with the blog URL. The blog is live at `https://<handle>.<apex>`.

Fallback for step 6: the app shows the camera's short code; typing it on `/me` binds the camera if it uploaded something in the last 10 minutes.

## Profile picture

1. After binding, the app shows "Take your profile picture" → `POST /api/camera/request-avatar`.
2. The next photo captured after that moment becomes the avatar (`kind = avatar`, centre-cropped to 240×240 for the header) and is also a normal post.
3. The prompt can be repeated later from the app to replace it.

## Taking and publishing a picture

1. Press the shutter → review → press = save to flash (marked unsynced).
2. When the phone is near and the camera is awake, the app pulls unsynced photos, uploads each, and ACKs. The camera renames the file to `NNNNs.pbm`.
3. Server stores the photo under the profile. Every 10 minutes the newsletter job emails each approved subscriber **one** email per batch of new photos, with a fresh 48-hour link to the newest one.

## Subscribing

- **Self**: a visitor enters their email in the blog's Join form → `subscribers(status = pending, added_by = self)`; the owner gets an email and sees them in the app.
- **Owner**: adds an email in the app → approved immediately.
- **Approve**: from the app (or `/me/subscribers` on the web) → the subscriber receives a welcome email with a 48-hour link.

## Reading the blog

- **Anonymous**: header (avatar, "MEES'S LITTLE CAMERA", battery), the Join form, and the newest photo as a teaser with "N more pictures for subscribers".
- **With a link**: `/p/<photo>?t=<token>` verifies the token (48 h from sending), sets a cookie that expires with the token, shows the whole feed scrolled to that photo, and allows commenting.
- **Expired**: the page says the link has expired and offers "Send me a fresh link" (email field prefilled if known) → a new 48-hour link, only to approved subscribers; the response is the same either way.
- **Commenting**: the first comment asks for a name inline; it is remembered on the subscriber.

## Battery

There is no battery sensing on the board. The blog shows a deterministic fake: 100 % at bind time, declining to ~5 % over 7 days, then back to 100 %, with ±3 % daily jitter seeded by the camera id.

## Sleep vs Bluetooth

Ten seconds after the last activity the sleep face goes up. Behind it the radio keeps advertising for 30 s (longer while a phone is actively pulling); then the chip light-sleeps and the radio dies with it. Waking takes a one-second hold on the shutter and happens as the second completes; a tap does nothing. Practical rule for the owner: take the picture, press send, and the phone in your pocket does the rest within half a minute.
