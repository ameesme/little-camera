# Flows

Who does what, in order, across the three codebases. Byte-level details live in [protocol.md](protocol.md).

## Registering a blog

The owner registers on the website with their name, email and a handle, which becomes their subdomain. A link emailed to them verifies the address and logs them in, and `/app/me` then shows the **verification picture**: a big QR code, good for 15 minutes.

Meanwhile the app finds the camera, pairs with the six digits the camera puts on its own screen, and says hello to the server. The owner photographs the phone screen and presses send; the app uploads that photo, and the server decodes the QR, binds the camera to the blog, moves any earlier photos from that camera onto it, and emails that the camera is linked. `/app/me` polls and flips to the blog URL.

Photographing the code is the only way to bind, which makes holding the camera the proof. It also works on a camera that is already linked, so passing the camera on is just the new owner photographing their own code; the photos already posted stay where they are.

After binding, the app offers "take your profile picture", and the next photo captured becomes the avatar as well as a normal post.

## Publishing a picture

Press, review, press again to save it to flash as unsynced. When the phone is near and the camera is awake the app pulls the unsynced photos, uploads each and ACKs, and the camera marks it synced. Every 10 minutes the newsletter job emails each approved subscriber **one** email per batch of new photos, with a fresh 48-hour link to the newest.

## Subscribing and reading

A visitor can enter their email in the blog's Join form, which the owner then approves from the app; the owner can also add an email directly, which is approved on the spot. Approval sends a welcome email with a 48-hour link.

Without a link the blog shows the header, the Join form and the newest photo as a teaser. With one it shows the whole feed, scrolled to that photo, and allows comments; the cookie expires with the token. An expired link offers a fresh one, which only an approved subscriber actually gets — the response looks the same either way.

## Updating the firmware

The app asks `GET /api/firmware/latest?installed=<what Info reported>`, and if there is something newer the owner sees it and a button. The app downloads the image over HTTPS and checks it against the digest the server gave before it touches the camera, because a megabyte over Bluetooth takes a minute and a bad download should cost none of it.

Then `UPDATE_BEGIN` with the size and the digest, and the image in ~500-byte chunks that each carry their own offset, inside the window the camera grants (§3.7). The camera writes them to the app slot it is not running from, reports progress every half window, shows a bar and refuses to sleep. A dropped chunk comes back as the offset it wants next; a phone that walks off has 90 s to reconnect and carry on where it stopped.

On `UPDATE_END` the camera checks the SHA-256 of everything it received, lets the bootloader check the image's own digest, and only then points the bootloader at the new slot and reboots. The new firmware has twenty seconds of running to confirm itself; three boots without that and the camera goes back to the slot it came from. Nothing here can leave it unbootable and nothing here touches the photos.

Back the other way, over USB: `ota revert` on the serial console, because `pio run -t upload` writes the first slot while the bootloader still points at the second.

## Battery

The camera reads its own VBAT and shows the raw millivolts on the sleep face, but never sends them: `Info` (§3.1) has no battery field. So the blog's indicator is a deterministic fake, a 7-day curve seeded by the camera id.

## Sleep vs Bluetooth

Ten seconds after the last activity the sleep face goes up, 30 s on the review and gallery screens. Behind it the radio advertises for another 30 s, longer while a phone is pulling, then the chip light-sleeps and the radio dies with it. Waking is a one-second hold, so a tap does nothing. For the owner: take the picture, press send, and the phone in your pocket does the rest within half a minute.
