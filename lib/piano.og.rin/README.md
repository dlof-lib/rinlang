# piano 1.0.0

مكتبة رسم متجهي احترافي لـ Rin: أشكال، مسارات، تدرّجات، ظلال وتوهّج، أنماط، قصّ وأقنعة، حركة، مخططات، مكوّنات واجهة وأيقونات وإطارات أجهزة، ودمج مباشر مع indsin.
`@import "piano";` ثم `pn_canvas` … `pn_save(c, "a.svg")`.

التوثيق الكامل: [`docs/PIANO.md`](../../docs/PIANO.md) · الأمثلة: `examples/piano/` · المعاينة: `web/piano/index.html`.

```
disconom.json   main.zpp   core/{style,shapes,path,transform,render,chart,design,indsin,dsl,film}.zpp
data/themes.json   native/{piano.hpp,piano_cli.cpp,piano.js}
```

لغة الكتل: `piano.Poster { $Hero.Circle => "240 135 60" { fill: "#1db143"; }; };` — انظر docs/PIANO.md.

حركة بكود المستخدم: `pn_film(w, h, fps, secs, theme, draw)` — انظر docs/PIANO.md.
