# RelyRIN

**RelyRIN** هي مكتبة Rin للمعاينة الحية والوسائط، مكتوبة بالكامل بلغة Rin.

## القدرات

- Markdown → HTML.
- وثيقة Preview كاملة جاهزة للـ WebView/المتصفح.
- Themes وCSS قابلة للتخصيص من Rin.
- عناوين H1–H6.
- Bold / Italic / Strike / Inline Code.
- روابط وصور.
- كتل كود.
- اقتباسات وقوائم ومهام.
- جداول Markdown.
- HTML5 Audio.
- HTML5 Video.
- YouTube Embed عبر `youtube-nocookie.com`.
- Directives للوسائط داخل Markdown:
  - `::image URL | ALT`
  - `::audio URL`
  - `::video URL`
  - `::youtube URL`
- قراءة `.md` من القرص وبناء Preview HTML.
- كتابة ملف Preview مباشرة.

## الاستخدام

```rin
@import "lib/relyRIN.og.rin";

let html = relyLive("# Hello\n\n**Rin**");
writeFile("preview.html", html);
```

### YouTube

```rin
let video = relyYoutube("https://www.youtube.com/watch?v=VIDEO_ID");
let page = relyLive("## Video\n\n" + video);
```

أو داخل Markdown:

```text
::youtube https://www.youtube.com/watch?v=VIDEO_ID
```

### الوسائط

```rin
let image = relyImage("images/rin.png", "Rin");
let audio = relyAudio("audio/theme.mp3", true);
let video = relyVideo("video/demo.mp4", true, false, false, false);
```

### Theme

```rin
let html = relyLiveStyled("# Rin", {
    "accent": "#22c88e",
    "bg": "#ffffff",
    "text": "#202124",
    "radius": "16px"
});
```

## API الأساسية

`relyLive` · `relyLiveStyled` · `relyDocument` · `relyRenderMarkdown` · `relyMarkdownFile` · `relyMarkdownFileStyled` · `relyWritePreview` · `relyWritePreviewStyled` · `relyBuildMarkdown` · `relyImage` · `relyAudio` · `relyVideo` · `relyYoutube` · `relyYoutubeId` · `relyMedia` · `relyTheme` · `relyCss` · `relyInfo`

> **ملاحظة:** RelyRIN تنتج HTML/CSS قياسيين. تشغيل المعاينة فعلياً داخل Android يتطلب أن يقوم WebView/واجهة التطبيق بعرض HTML الناتج، مع السماح بالإنترنت إذا كانت المعاينة تحتوي على YouTube.

## Architecture

RelyRIN is a bridge library, not a replacement renderer. It uses Rin's existing
`@container`, `@theme`, `@view`, Loom/Fabric/Dye pipeline and View implementations.
RelyRIN adds Markdown parsing, media descriptors, YouTube URL handling and live-preview
source generation on top of those existing systems.
