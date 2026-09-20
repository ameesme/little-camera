# Fonts

```
ppneuemontreal-book.woff     (400, body)
ppneuemontreal-bold.woff     (700, the headline)
```

PP Neue Montreal (Pangram Pangram), the same two faces amaranthstudio.com
serves, taken from `ameesme/amaranthstudio-web` so the teaser sits in the same
world. Both are OpenType/CFF in a WOFF wrapper, ~62 KB each. The internal
metrics match what `index.html` declares — Book is `usWeightClass` 400, Bold is
700 with the macStyle bold bit set — so neither face is synthetically
emboldened by the browser.

They are declared as two families rather than two weights of one, because that
is how the studio's own site declares them and it keeps `local()` name matching
honest: a machine with the retail font installed resolves the same faces.

`index.html` references them at exactly these paths and preloads both. Until
the files are present the stack falls back to Helvetica Neue, the nearest
grotesque widely installed; nothing breaks either way, because the headline is
fitted by measuring the font that actually resolved and re-fits itself on
`document.fonts.ready`.

## Licence

These are **licensed, not free** files. The typeface is commercial and the
webfont licence is Amaranth Studio's, covering the domains it is bought for.
Serving them from lttl.cam is inside that; redistributing them is not, so
treat this directory as deployment payload rather than as source. If the repo
is public, see the note in `PLAN_VERCEL_DEPLOYMENT.md` before pushing.
