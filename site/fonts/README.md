# Fonts

PP Neue Montreal is licensed, so the files are not in this repository. Drop
them here before deploying:

```
site/fonts/ppneuemontreal-book.woff     (400, body)
site/fonts/ppneuemontreal-bold.woff     (700, the headline)
```

`index.html` declares both with `@font-face` at exactly these paths, matching
amaranthstudio.com's own declarations. Until the files are present the stack
falls back to Helvetica Neue, which is the nearest grotesque widely installed.

Nothing breaks either way: the headline is fitted by measuring the font that
actually resolved, so it fills the measure correctly in the fallback and
re-fits itself once PP Neue Montreal loads.
