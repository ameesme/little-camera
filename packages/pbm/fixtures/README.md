# Fixtures

Eight real 320×240 1-bit photos (the ones from the blog mockup), in exactly the
format the firmware writes: `P4\n320 240\n` + 9600 bytes, set bit = black.
`avatar-88.png` is the mockup's profile picture. Used by the pbm package tests,
the server tests, and `pnpm seed` for a local demo blog.
