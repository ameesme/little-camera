# Fonts

PP Neue Montreal (Pangram Pangram), Book 400 and Bold 700, the same two faces amaranthstudio.com serves, taken from `ameesme/amaranthstudio-web`. Declared as two families rather than two weights of one, because that is how the studio's own site does it and it keeps `local()` matching honest.

Until the files are present the stack falls back to Helvetica Neue and nothing breaks, because the headline is fitted by measuring whichever font actually resolved and re-fits on `document.fonts.ready`.

## Licence

These files are **licensed, not free**. The typeface is commercial and the webfont licence is Amaranth Studio's, covering the domains it was bought for. Serving them from lttl.cam is inside that; redistributing them is not, and `ameesme/little-camera` is public — Mees made that call knowingly (`../DEPLOYMENT.md`). Treat this directory as deployment payload rather than source.
