# RelyRIN

مكتبة Rin للمعاينة الحية لملفات Markdown والوسائط.

## أسلوب API

أسماء الدوال منظمة إلى مجموعات واضحة:

- `relyLive*` — المعاينة والوثائق الحية
- `relyMd*` — Markdown والملفات
- `relyStyle*` — النمط وCSS
- `relyMedia*` — الصور والصوت والفيديو وYouTube

## مثال

```rin
@import "lib/relyRIN.og.rin";

let style = relyStyle({
    "accent": "#22c88e",
    "bg": "#ffffff"
});

let preview = relyLiveDocument(
    "# Hello Rin\n\n**Live Preview**",
    style
);

writeFile("preview.html", preview);
```

## Markdown

```rin
let html = relyMdRender(markdown);
let preview = relyMdPreview("README.md");
let styled = relyMdPreviewStyled("README.md", style);
```

## Media

```rin
let image = relyMediaImage("image.png", "Rin");
let audio = relyMediaAudio("audio.mp3", true);
let video = relyMediaVideo("video.mp4", true, false, false, false);
let youtube = relyMediaYoutube("https://www.youtube.com/watch?v=VIDEO_ID");
```

## Directives

داخل Markdown:

```text
::image image.png | Rin
::audio audio.mp3
::video video.mp4
::youtube https://www.youtube.com/watch?v=VIDEO_ID
```

RelyRIN يحول المحتوى إلى HTML/CSS جاهز للعرض في WebView أو المتصفح.
