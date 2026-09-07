package com.dlof.rinlang

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.RectF
import android.graphics.Shader
import android.text.TextPaint
import android.text.TextUtils
import android.util.AttributeSet
import android.view.GestureDetector
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.widget.FrameLayout
import android.widget.VideoView
import android.widget.MediaController
import android.webkit.WebView
import android.webkit.WebViewClient
import android.net.Uri
import org.json.JSONObject
import kotlin.math.max
import kotlin.math.roundToInt
import kotlin.math.min

/**
 * Draws a Loomtime Fabric tree (the JSON [RinEngine.renderView] / [RinEngine.LoomSession] return —
 * see `loom::fabricToJsonString` on the native side) directly onto a [Canvas]. This is the real
 * pixel-accurate preview surface: every rectangle/text run drawn here comes straight from the
 * geometry & resolved attributes the native Loom layout engine computed, not a simulation.
 *
 * Coordinate space: the Fabric's x/y/w/h are in the same "root px" space [RinEngine.renderView]
 * was asked to lay out at (see [rootWidthPx]). This view maps that space onto itself at [zoom],
 * with 1 root-px == 1dp at zoom = 1 so a 390-wide Fabric roughly fills a 390dp-wide phone.
 */
class LoomFabricView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : FrameLayout(context, attrs) {

    /** Node kinds, mirroring `loom::StrandKind` (rin_loom_strand.h) exactly. */
    private object Kind {
        const val TEXT = "Text"; const val IMAGE = "Image"; const val BUTTON = "Button"
        const val CARD = "Card"; const val COLUMN = "Column"; const val ROW = "Row"
        const val STACK = "Stack"; const val DIVIDER = "Divider"
        // New: app chrome, media, and table — mirror the StrandKind additions noted in
        // rin_loom_layout.h (HEADER/TOPBAR/BOTTOMBAR/DRAWER/MENU/MENUITEM/TABLE/TABLEROW/
        // VIDEO/AUDIO/WEBVIEW/SCAFFOLD/SPLASH). Video remains a Canvas thumbnail for local files,
        // while `video_url=` gets a real remote playback overlay supplied by the host view.
        // The source language stays provider-neutral.
        const val HEADER = "Header"; const val TOPBAR = "TopBar"; const val BOTTOMBAR = "BottomBar"
        const val DRAWER = "Drawer"; const val MENU = "Menu"; const val MENUITEM = "MenuItem"
        const val TABLE = "Table"; const val TABLEROW = "TableRow"
        const val VIDEO = "Video"; const val AUDIO = "Audio"; const val WEBVIEW = "WebView"
        const val SCAFFOLD = "Scaffold"; const val SPLASH = "Splash"
        const val BANNER = "Banner"
        // Overlay Engine (rin_loom_overlay.h / rin_loom_paint.h's colorForKind): Dialog paints
        // like a Card (theme surface role); Tooltip paints like a small neutral chip.
        const val DIALOG = "Dialog"; const val TOOLTIP = "Tooltip"
        // Missing-components pass (rin_loom_layout.h): Badge is a small self-drawn pill+label,
        // never a plain box. Spacer is a pure layout strand -- an invisible flexible gap -- and
        // must draw nothing at all, same as the native Dye backend (rin_loom_paint.h) treats it.
        const val BADGE = "Badge"; const val SPACER = "Spacer"
        // §21: Object Inspector (rin_loom_object.h) — a live, read-only card synthesized from a
        // registered `.object("id") ... container.(); .end/object` value. Native already lowers
        // it to a Card-like Box with plain Text children (title + "key: value" rows) via
        // applyObjectConveniences(), so no bespoke drawing beyond the background box below is
        // needed — the Text children draw themselves through the ordinary recursive walk.
        const val OBJECT = "Object"

        // ---- Live preview closes the parity gap: these kinds already exist natively
        // (loom::StrandKind, rin_loom_paint.h) — the "missing components" pass and the
        // ready-elements expansion — but previously had no case below, so they fell into the
        // generic else-branch as a plain, undecorated box: a Checkbox drew as an empty square
        // with no tick, a Progress bar never showed its fill, an Input never showed its
        // placeholder. Every one of these now gets the same content a real render would show.
        const val PROGRESS = "Progress"; const val CHECKBOX = "Checkbox"; const val SWITCH = "Switch"
        const val AVATAR = "Avatar"; const val INPUT = "Input"; const val TEXTAREA = "TextArea"
        const val TABS = "Tabs"; const val TABITEM = "TabItem"
        const val ICON = "Icon"; const val ICONBUTTON = "IconButton"
        // Ready-elements expansion (docs/RIN_ELEMENTS.md):
        const val LINK = "Link"; const val RADIO = "Radio"; const val SLIDER = "Slider"
        const val SEARCH = "Search"; const val SELECT = "Select"; const val FILE = "File"
        const val DATE = "Date"; const val TIME = "Time"; const val CODE_EDITOR = "CodeEditor"
        const val CALCULATOR = "Calculator"; const val LIST = "List"; const val LISTITEM = "ListItem"
        // Sidebar/Popup never appear here: they're pure tag aliases the native side already
        // resolves onto Drawer/Dialog (see strandKindFromTag() in rin_loom_strand.h), so the
        // Fabric JSON this view reads always says "kind":"Drawer"/"Dialog" already.
    }

    private val defaultBar = Color.rgb(30, 31, 40)
    private val defaultDrawer = Color.rgb(22, 23, 30)
    private val defaultMedia = Color.rgb(18, 18, 26)
    private val defaultTableLine = Color.argb(60, 255, 255, 255)
    private val defaultTableHeaderBg = Color.rgb(34, 36, 48)

    // ---- default palette — must match loom::colorForKind() in rin_loom_paint.h exactly ----
    private val defaultCard = Color.rgb(40, 42, 54)
    private val defaultButton = Color.rgb(124, 92, 255)
    private val defaultText = Color.rgb(230, 230, 240)
    private val defaultImage = Color.rgb(70, 70, 90)
    private val defaultDivider = Color.rgb(51, 51, 63)
    private val defaultContainer = Color.rgb(24, 25, 32) // Column/Row/Stack/Custom root fallback
    private val defaultBanner = Color.rgb(44, 47, 61) // neutral Banner default; see bannerTypeColor()
    private val defaultDialog = Color.rgb(40, 42, 54) // matches loom::colorForKind()'s Theme::surface for DIALOG
    private val defaultTooltip = Color.rgb(60, 62, 74) // matches Theme::neutral for TOOLTIP
    private val scrimColor = Color.argb(140, 0, 0, 0)

    // Remote media overlays: the Canvas renderer remains the design-time fallback, while
    // `video_url=` gets a real playback surface on top of the matching Fabric rectangle.
    private val remoteMediaViews = linkedMapOf<String, View>()
    private var lastRemoteMediaSignature = ""
    private val mainHandler = android.os.Handler(android.os.Looper.getMainLooper())
    private val remoteFontCache = mutableMapOf<String, android.graphics.Typeface>()
    private val pendingFontUrls = mutableSetOf<String>()
    // Remote images: `src="https://..."` (Image/Avatar/etc.) downloads once into app cache and
    // decodes from disk from then on — same "download once, cache, reuse offline" shape as
    // remoteFontCache/pendingFontUrls above, just keyed by the target box size too (a small
    // Avatar and a large Image sharing one URL still want independently-downsampled bitmaps).
    private val pendingImageUrls = mutableSetOf<String>()
 // ~55% black — matches loom::scrimColor()'s RGB, opacity is this renderer's own convention (see rin_loom_paint.h's SCRIM_RECT comment)

    // ---- live-preview additions: field boxes (Input/TextArea/Search/Select/File/Date/Time/
    // CodeEditor all share the same bordered look natively — see paintField in rin_loom_paint.h)
    // and the small controls (Checkbox/Switch/Progress/Radio/Slider/Avatar/Icon). ----
    private val defaultFieldBg = Color.rgb(30, 31, 40) // Theme::surface, slightly darker than Card
    private val defaultFieldBorder = Color.argb(120, 255, 255, 255)
    private val defaultPlaceholder = Color.argb(140, 230, 230, 240) // dimmer than defaultText
    private val defaultTrack = Color.rgb(51, 51, 63) // unfilled Progress/Slider track — matches defaultDivider
    private val defaultLink = Color.rgb(95, 211, 255) // link-toned text — matches toneColor("info")
    private val defaultVisitedLink = Color.rgb(160, 130, 255) // Link `visited="true"` — a muted
        // violet, distinct from both the unvisited link tone and defaultButton's brighter purple

    // ---- must match loom::bannerTypeColor() in rin_loom_paint.h exactly ----
    private fun bannerTypeColor(type: String): Int = when (type) {
        "success" -> Color.rgb(46, 160, 67)
        "warning" -> Color.rgb(212, 167, 44)
        "error" -> Color.rgb(209, 69, 69)
        "action" -> Color.rgb(124, 92, 255)
        "progress", "info" -> Color.rgb(58, 110, 196)
        else -> defaultBanner
    }

    // ---- tone= semantic role -> color, same roles rin_loom_paint.h's resolveColor() resolves
    // against the active Theme (falls back to this project's own @theme=Professional values when
    // no theme lookup is wired up on this side yet, so Badge/Button tone= isn't just ignored). ----
    private fun toneColor(tone: String): Int = when (tone) {
        "primary" -> defaultButton // #7C5CFF
        "secondary" -> Color.rgb(0x22, 0xC8, 0x8E)
        "success" -> Color.rgb(0x22, 0xC8, 0x8E)
        "danger" -> Color.rgb(0xF1, 0x4C, 0x4C)
        "warning" -> Color.rgb(0xE8, 0xB2, 0x3D)
        "info" -> Color.rgb(0x5F, 0xD3, 0xFF)
        "neutral" -> Color.rgb(0x91, 0x98, 0xA3)
        else -> defaultButton
    }

    var rootWidthPx: Int = 390
    var rootHeightPx: Int = 640
        private set

    var zoom: Float = 1f
        set(value) {
            field = value.coerceIn(0.25f, 3f)
            onZoomChanged?.invoke(field)
            requestLayout()
            invalidate()
        }

    /** Fired whenever [zoom] changes (toolbar +/- buttons or, in future, a pinch gesture on the canvas). */
    var onZoomChanged: ((zoom: Float) -> Unit)? = null

    var showGrid: Boolean = false
        set(value) { field = value; invalidate() }

    var showSafeArea: Boolean = false
        set(value) { field = value; invalidate() }

    /** The Fabric root node (the object under the top-level `"fabric"` key), or null while empty/erroring. */
    private var fabric: JSONObject? = null

    // ---- Overlay Engine (rin_loom_overlay.h) ----
    //
    // The top-level result JSON's `"overlays"` array (see overlayLayerJson() in rin_loom_c_api.cpp).
    // Each entry's `box`/`scrimRect` are already the corrected, viewport-relative coordinates
    // (buildOverlayLayer() on the native side mutated the Strand's own x/y in place, so the
    // Fabric tree walked by [drawNode] already has them too) — what this array adds is what a
    // single node's generic {kind,x,y,w,h,attrs} shape can't carry: *which* nodes are overlays
    // (so the normal recursive walk skips them and their descendants), their back-to-front
    // stacking order, and their scrim/modal metadata.
    private var overlayEntries: List<JSONObject> = emptyList()
    private var overlayNames: Set<String> = emptySet()

    /** Fired with root-px coordinates on a single tap — forwarded straight to [LoomPreviewManager.tap]. */
    var onTap: ((x: Double, y: Double) -> Unit)? = null

    /** Fired on long-press with the deepest Fabric node under the finger (or null if none) — Inspector. */
    var onInspect: ((node: JSONObject?) -> Unit)? = null

    /**
     * New: page navigation, e.g. going from `main.rin` to `mu.rin`.
     * Fired with the target filename whenever:
     *  (a) the tapped node (or an ancestor) has an attr `onTap="navigate:mu.rin"`, or
     *  (b) [duration] ms elapse after [setFabric] on a page whose root has `navigate="mu.rin"`
     *      (a splash/loading screen that auto-advances — see `duration=`).
     * The actual file switch (loading/parsing/rendering "mu.rin") is the host app's job; this
     * view only detects *when* to navigate and *where* to.
     */
    var onNavigate: ((target: String) -> Unit)? = null

    /**
     * Link concepts (docs/link.md): fired with an absolute URL whenever a tapped node resolves
     * to an *external* link — either `onTap="open:https://..."` or a bare `href=` attribute
     * (Link's shorthand for onTap) whose value carries a URL scheme. Internal targets (a bare
     * filename/route, or one starting with "/") go through [onNavigate] instead, exactly like
     * `onTap="navigate:..."` already does — `href` is just sugar over that same split, so a
     * `Link` never needs an explicit `onTap` for either case.
     */
    var onOpenUrl: ((url: String) -> Unit)? = null

    private val navHandler = android.os.Handler(android.os.Looper.getMainLooper())
    private var pendingAutoNavigate: Runnable? = null

    /** True for `href`/`onTap="open:..."` values that name an external resource rather than an
     * in-app route — a URL scheme (`http:`, `https:`, `mailto:`, `tel:`) present. A bare filename
     * like "mu.rin" or a path like "/settings" is never external. */
    private fun isExternalTarget(target: String): Boolean =
        Regex("^(https?|mailto|tel):", RegexOption.IGNORE_CASE).containsMatchIn(target)

    /** Walks up from the hit node to find the nearest `onTap="navigate:...."` instruction, or a
     * bare `href=` attribute (Link's shorthand — see [onOpenUrl]) that resolves to an internal
     * route rather than an external URL. */
    private fun navigateTargetForTap(node: JSONObject?): String? {
        val attrs = node?.optJSONObject("attrs") ?: return null
        val onTap = attrs.optString("onTap")
        if (onTap.startsWith("navigate:")) return onTap.removePrefix("navigate:").trim()
        val href = attrs.optString("href").trim()
        if (href.isNotEmpty() && onTap.isEmpty() && !isExternalTarget(href)) return href
        return null
    }

    /** Mirror of [navigateTargetForTap] for the external-URL half of the same shorthand: an
     * `onTap="open:URL"` instruction, or a bare `href=` that carries a URL scheme. */
    private fun openUrlTargetForTap(node: JSONObject?): String? {
        val attrs = node?.optJSONObject("attrs") ?: return null
        val onTap = attrs.optString("onTap")
        if (onTap.startsWith("open:")) return onTap.removePrefix("open:").trim()
        val href = attrs.optString("href").trim()
        if (href.isNotEmpty() && onTap.isEmpty() && isExternalTarget(href)) return href
        return null
    }

    /*
     * Fabric coordinates are logical Loom units (the same dp-like unit used by the native
     * layout engine). The old renderer multiplied them by Android density, which made a
     * 390-wide design become ~1000px on a phone and then forced it into an EXACTLY-sized
     * ScrollView child. The result was clipping/stretch-like visual corruption.
     *
     * The live preview is a design canvas, not a native dp surface: at zoom=1 the complete
     * logical device width is fitted to the actual preview viewport. Zoom is then applied on
     * top of that single scale. All drawing, hit testing and media overlays use this exact
     * same transform.
     */
    private fun previewScale(): Float {
        val rw = rootWidthPx.coerceAtLeast(1).toFloat()
        val viewportW = width.toFloat().coerceAtLeast(1f)
        return (viewportW / rw) * zoom
    }

    private val fillPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.FILL }
    private val strokePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = 1.2f
    }
    private val textPaint = TextPaint(Paint.ANTI_ALIAS_FLAG)
    private val gridPaint = Paint().apply { color = Color.argb(28, 255, 255, 255); strokeWidth = 1f }
    private val safeAreaPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = 1.5f
        color = Color.argb(140, 124, 92, 255)
        pathEffect = android.graphics.DashPathEffect(floatArrayOf(8f, 6f), 0f)
    }
    private val inspectHighlightPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = 2.5f
        color = Color.rgb(255, 196, 77)
    }
    private val bitmapPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { isFilterBitmap = true }

    // ---- real image loading: `<Image src="...">` now decodes an actual file instead of only
    // ever drawing the placeholder glyph. `src` resolves relative to the *same* project root
    // save/installation/file already write to (RinEngine.currentBaseDir()) — so an image a Rin
    // program just saved or that was imported into the project shows up for real here. Absolute
    // paths and file:// URIs work too. Falls back to the old placeholder when nothing decodable
    // is found, so unresolved/mistyped src= still reads clearly as "image, not yet there" rather
    // than a blank box or a crash. ----
    private val bitmapCache = object : android.util.LruCache<String, android.graphics.Bitmap>(24) {
        override fun entryRemoved(evicted: Boolean, key: String, oldValue: android.graphics.Bitmap, newValue: android.graphics.Bitmap?) {
            if (evicted && oldValue !== newValue) oldValue.recycle()
        }
    }
    private val missingSrc = HashSet<String>()

    // ---- real video thumbnails: `<Video src="...">` now decodes an actual frame from the file
    // instead of only ever drawing the placeholder glyph — same upgrade [loadBitmapForRect]
    // already gave Image, just via MediaMetadataRetriever since the source is a video container,
    // not a still image. Kept as a separate cache from images: different decode path/cost, and a
    // video src should never collide with an unrelated image cached under the same string. ----
    private val videoThumbCache = object : android.util.LruCache<String, android.graphics.Bitmap>(8) {
        override fun entryRemoved(evicted: Boolean, key: String, oldValue: android.graphics.Bitmap, newValue: android.graphics.Bitmap?) {
            if (evicted && oldValue !== newValue) oldValue.recycle()
        }
    }
    private val missingVideoSrc = HashSet<String>()

    /** Node currently highlighted by the Inspector (long-press), drawn on top after the tree. */
    private var inspectedNode: JSONObject? = null

    init {
        // setShadowLayer() (used for Card `shadow=` attrs) requires a software layer.
        setLayerType(LAYER_TYPE_SOFTWARE, null)
    }

    private val gestureDetector = GestureDetector(context, object : GestureDetector.SimpleOnGestureListener() {
        override fun onSingleTapUp(e: MotionEvent): Boolean {
            val (rx, ry) = viewToRoot(e.x, e.y)
            onTap?.invoke(rx, ry)
            val hit = fabric?.let { hitTest(it, rx.toFloat(), ry.toFloat()) }
            navigateTargetForTap(hit)?.let { onNavigate?.invoke(it) }
            openUrlTargetForTap(hit)?.let { onOpenUrl?.invoke(it) }
            return true
        }

        override fun onLongPress(e: MotionEvent) {
            val (rx, ry) = viewToRoot(e.x, e.y)
            val hit = fabric?.let { hitTest(it, rx.toFloat(), ry.toFloat()) }
            inspectedNode = hit
            onInspect?.invoke(hit)
            invalidate()
        }
    })

    override fun onTouchEvent(event: MotionEvent): Boolean {
        gestureDetector.onTouchEvent(event)
        return true
    }

    private fun viewToRoot(vx: Float, vy: Float): Pair<Double, Double> {
        val scale = previewScale().coerceAtLeast(0.0001f)
        return (vx / scale).toDouble() to (vy / scale).toDouble()
    }

    /** Clears the Inspector highlight (call when the info panel is dismissed). */
    fun clearInspection() {
        inspectedNode = null
        invalidate()
    }

    /**
     * Replaces the drawn Fabric with [node] (pass null to show an empty canvas — e.g. while the
     * very first render is pending). [rootW]/[rootH] size the view's intrinsic content bounds.
     * [overlays] is the result JSON's top-level `"overlays"` array (see the Overlay Engine note
     * above) — omit it (or pass null) for a caller that hasn't been updated for the Overlay
     * Engine yet; every open Dialog/anchored Tooltip then simply draws inline at whatever
     * position the Fabric tree gives it, exactly as before this feature existed.
     */
    fun setFabric(node: JSONObject?, rootW: Int, rootH: Int, overlays: org.json.JSONArray? = null) {
        fabric = node
        rootWidthPx = max(1, rootW)
        rootHeightPx = max(1, rootH)

        overlayEntries = (0 until (overlays?.length() ?: 0)).mapNotNull { overlays?.optJSONObject(it) }
        overlayNames = overlayEntries.mapNotNull { it.optString("name").takeIf(String::isNotEmpty) }.toSet()

        // New: splash/loading pages — root attrs `duration="2000" navigate="mu.rin"` mean "auto
        // advance to mu.rin after 2000ms". Re-armed on every setFabric so switching pages cancels
        // whatever timer belonged to the previous page.
        pendingAutoNavigate?.let { navHandler.removeCallbacks(it) }
        pendingAutoNavigate = null
        val rootAttrs = node?.optJSONObject("attrs")
        val navigateTarget = rootAttrs?.optString("navigate")?.takeIf { it.isNotBlank() }
        val durationMs = rootAttrs?.optString("duration")?.toLongOrNull()
        if (navigateTarget != null && durationMs != null && durationMs >= 0) {
            val r = Runnable { onNavigate?.invoke(navigateTarget) }
            pendingAutoNavigate = r
            navHandler.postDelayed(r, durationMs)
        }

        requestLayout()
        syncRemoteMediaOverlays(node)
        invalidate()
    }

    override fun onDetachedFromWindow() {
        pendingAutoNavigate?.let { navHandler.removeCallbacks(it) }
        remoteMediaViews.values.forEach { v ->
            if (v is VideoView) v.stopPlayback()
            if (v is WebView) v.stopLoading()
        }
        remoteMediaViews.clear()
        super.onDetachedFromWindow()
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val widthMode = MeasureSpec.getMode(widthMeasureSpec)
        val widthSize = MeasureSpec.getSize(widthMeasureSpec)

        /*
         * At 100% the logical device is always fitted to the available preview width.
         * For zoom > 100%, grow the child so the ScrollView can expose the enlarged canvas.
         * Do not use Android display density here: the native Fabric is already in logical
         * preview units and previewScale() is the sole coordinate transform.
         */
        val baseScale = when (widthMode) {
            MeasureSpec.UNSPECIFIED -> 1f
            else -> widthSize.toFloat().coerceAtLeast(1f) /
                rootWidthPx.coerceAtLeast(1).toFloat()
        }
        val contentW = (rootWidthPx * baseScale * zoom).roundToInt().coerceAtLeast(1)
        val contentH = (rootHeightPx * baseScale * zoom).roundToInt().coerceAtLeast(1)

        val finalWidth = when (widthMode) {
            MeasureSpec.EXACTLY -> if (zoom <= 1f) widthSize else contentW
            MeasureSpec.AT_MOST -> min(contentW, widthSize).coerceAtLeast(suggestedMinimumWidth)
            else -> contentW.coerceAtLeast(suggestedMinimumWidth)
        }

        val heightMode = MeasureSpec.getMode(heightMeasureSpec)
        val heightSize = MeasureSpec.getSize(heightMeasureSpec)
        val finalHeight = when (heightMode) {
            MeasureSpec.EXACTLY -> if (zoom <= 1f) heightSize else contentH
            MeasureSpec.AT_MOST -> max(contentH, heightSize).coerceAtLeast(suggestedMinimumHeight)
            else -> max(contentH, suggestedMinimumHeight)
        }

        setMeasuredDimension(finalWidth, finalHeight)
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        canvas.save()
        canvas.scale(previewScale(), previewScale())

        val root = fabric
        if (root != null) {
            drawNode(canvas, root)
        }

        // Overlay Engine (rin_loom_overlay.h): paint the overlay layer strictly after the main
        // document, in its own back-to-front order — a real second z-layer, not a hope that the
        // Fabric's own tree order happened to put Dialog/Tooltip last (see drawNode's own
        // overlayNames skip below for the other half of this). Each modal entry's scrim is drawn
        // immediately before that entry's own subtree, matching native paintWithOverlay()'s order.
        if (root != null) {
            for (entry in overlayEntries) {
                if (entry.optBoolean("scrim", false)) {
                    entry.optJSONObject("scrimRect")?.let { r ->
                        val rx = r.optDouble("x", 0.0).toFloat(); val ry = r.optDouble("y", 0.0).toFloat()
                        val rw = r.optDouble("w", 0.0).toFloat(); val rh = r.optDouble("h", 0.0).toFloat()
                        fillPaint.shader = null; fillPaint.clearShadowLayer()
                        fillPaint.color = scrimColor
                        canvas.drawRect(rx, ry, rx + rw, ry + rh, fillPaint)
                    }
                }
                val name = entry.optString("name")
                findNodeByName(root, name)?.let { overlayNode -> drawNode(canvas, overlayNode, skipOverlays = false) }
            }
        }

        if (showGrid) drawGrid(canvas)
        if (showSafeArea) drawSafeArea(canvas)
        inspectedNode?.let { drawInspectHighlight(canvas, it) }

        canvas.restore()
    }

    /** Finds the first node (pre-order) whose `name` equals [target] — mirrors the native side's
     * findStrandByName() in rin_loom_overlay.h, used the same way: correlating an overlay entry
     * back to its Fabric node. */
    private fun findNodeByName(node: JSONObject, target: String): JSONObject? {
        if (target.isEmpty()) return null
        if (node.optString("name") == target) return node
        val children = node.optJSONArray("children") ?: return null
        for (i in 0 until children.length()) {
            val child = children.optJSONObject(i) ?: continue
            findNodeByName(child, target)?.let { return it }
        }
        return null
    }

    // ---- tree walk ----

    /**
     * [skipOverlays] is true for the main document walk (so an open Dialog / anchored Tooltip —
     * named in [overlayNames] — and everything under it is skipped here and drawn only once,
     * later, by [onDraw]'s explicit overlay pass) and false when [onDraw] calls back into this
     * function to actually draw that overlay's own subtree.
     */
    private fun drawNode(canvas: Canvas, node: JSONObject, skipOverlays: Boolean = true) {
        if (skipOverlays && overlayNames.contains(node.optString("name"))) return
        val kind = node.optString("kind")
        val x = node.optDouble("x", 0.0).toFloat()
        val y = node.optDouble("y", 0.0).toFloat()
        val w = node.optDouble("w", 0.0).toFloat()
        val h = node.optDouble("h", 0.0).toFloat()
        val attrs = node.optJSONObject("attrs") ?: JSONObject()
        val rect = RectF(x, y, x + w, y + h)
        // The engine's own resolved color for this node (tone=/color=<role>/@theme= already
        // baked in) — used below in place of each kind's hardcoded default so the preview
        // reflects what the code actually specifies, not just a literal color="#RRGGBB". Null
        // (old/cached session) means "fall back to the hardcoded default exactly as before".
        val resolved = resolvedColor(node)

        when (kind) {
            // `format=` is a plain generic attr (see rin_loom_paint.h's fabricToJson — every
            // attrs.attrs key/value round-trips as-is, no native change needed) set by
            // sendMessage()/botReply()/botReplyMarkdown()/botReplyCode() (rin_interpreter.cpp) on
            // a chat message's `format` field: "text" (default)/"markdown"/"code". A chat-bubble
            // Text node binds its own `format=` attr to that same field (e.g.
            // `format = msg["format"];`) so the bubble renders accordingly — no StrandKind change
            // needed, this is still an ordinary Text node.
            Kind.TEXT -> when (attrs.optString("format")) {
                "code" -> drawCodeText(canvas, rect, attrs, attrs.optString("text"), defaultText)
                "markdown" -> drawMarkdownText(canvas, rect, attrs, attrs.optString("text"), resolved ?: defaultText)
                else -> drawText(canvas, rect, attrs, attrs.optString("text"), resolved ?: defaultText)
            }
            Kind.DIVIDER -> drawDivider(canvas, rect, attrs, resolved ?: defaultDivider)
            Kind.IMAGE -> drawImage(canvas, rect, attrs)
            Kind.BUTTON -> {
                drawBox(canvas, rect, attrs, resolved ?: defaultButton, defaultRadius = 10f)
                drawText(canvas, rect, attrs, attrs.optString("label"), Color.WHITE, centered = true, boldHint = true, singleLine = true)
            }
            Kind.CARD -> drawBox(canvas, rect, attrs, resolved ?: defaultCard, defaultRadius = 14f)
            Kind.OBJECT -> drawBox(canvas, rect, attrs, resolved ?: defaultCard, defaultRadius = 12f)

            // ---- new kinds ----
            Kind.HEADER, Kind.TOPBAR, Kind.BOTTOMBAR -> drawBox(canvas, rect, attrs, resolved ?: defaultBar, defaultRadius = 0f)
            Kind.DRAWER -> {
                if (rect.width() > 0f) {
                    drawBox(canvas, rect, attrs, resolved ?: defaultDrawer, defaultRadius = 0f)
                    // subtle edge shadow so an open drawer reads as "above" the content behind it
                    fillPaint.shader = null; fillPaint.clearShadowLayer()
                    fillPaint.color = Color.argb(70, 0, 0, 0)
                    canvas.drawRect(rect.right, rect.top, rect.right + 6f, rect.bottom, fillPaint)
                }
            }
            Kind.MENU -> if (rect.width() > 0f) drawBox(canvas, rect, attrs, resolved ?: defaultCard, defaultRadius = 10f)
            Kind.MENUITEM -> {
                drawBox(canvas, rect, attrs, resolved ?: defaultCard, defaultRadius = 0f)
                drawText(canvas, rect, attrs, attrs.optString("label").ifBlank { attrs.optString("text") }, defaultText, singleLine = true)
            }
            Kind.TABLE -> drawTable(canvas, rect, node, attrs)
            Kind.TABLEROW -> { /* cells are children; the header/grid lines are drawn by Table itself */ }
            Kind.VIDEO -> drawVideoPreview(canvas, rect, attrs)
            Kind.AUDIO -> drawMediaPlaceholder(canvas, rect, attrs, "♪", mediaSource(attrs))
            Kind.WEBVIEW -> drawMediaPlaceholder(canvas, rect, attrs, "🌐", mediaSource(attrs))
            Kind.SCAFFOLD -> { /* pure layout container: TopBar/Content/BottomBar/Drawer children draw themselves */ }
            Kind.SPLASH -> drawBox(canvas, rect, attrs, resolved ?: defaultContainer, defaultRadius = 0f)
            Kind.BANNER -> {
                // A Banner is a padded box (like Card) whose default fill depends on its `type`
                // attr (info/success/warning/error/action/progress/custom) rather than one fixed
                // color; an explicit tone=/color= on the strand still wins — [resolved] already
                // carries that resolution (see loom::resolveColor()'s BANNER-specific fallback).
                if (rect.width() > 0f && rect.height() > 0f) {
                    drawBox(canvas, rect, attrs, resolved ?: bannerTypeColor(attrs.optString("type")), defaultRadius = 12f)
                }
            }
            Kind.DIALOG -> {
                // Overlay Engine (rin_loom_overlay.h): when closed, w/h are 0 and this draws
                // nothing, same as any other zero-size Strand. When open, drawNode is reached
                // for this node a second time from onDraw's explicit overlay pass (never from
                // the normal recursive walk, which skips it via overlayNames) at its corrected,
                // scrim-backed, viewport-centered position.
                if (rect.width() > 0f && rect.height() > 0f) {
                    drawBox(canvas, rect, attrs, resolved ?: defaultDialog, defaultRadius = 16f)
                }
            }
            Kind.TOOLTIP -> {
                if (rect.width() > 0f && rect.height() > 0f) {
                    drawBox(canvas, rect, attrs, resolved ?: defaultTooltip, defaultRadius = 6f)
                    drawText(canvas, rect, attrs, attrs.optString("text"), defaultText, centered = true, singleLine = true)
                }
            }

            // Badge (rin_loom_layout.h's measureBadge): a small self-contained pill + label, not
            // a plain box -- previously had no case here at all, so it silently fell into the
            // generic else-branch below (a flat, square-cornered, unlabeled box), which is why a
            // Badge rendered as an empty dark rectangle instead of its "Beta"/"جديد" text.
            Kind.BADGE -> {
                if (rect.width() > 0f && rect.height() > 0f) {
                    val tone = resolved ?: toneColor(attrs.optString("tone").ifBlank { "primary" })
                    drawBox(canvas, rect, attrs, tone, defaultRadius = rect.height() / 2f)
                    drawText(canvas, rect, attrs, attrs.optString("text"), Color.WHITE, centered = true, boldHint = true, singleLine = true)
                }
            }

            // Spacer: a pure layout strand (flexible empty gap) -- must draw nothing, same as the
            // native Dye backend. Previously had no case here either, so it fell into the generic
            // else-branch and painted a solid, visible box exactly where it should have been
            // blank space (the stray rectangle artifacts inside gradient header/nav rows).
            Kind.SPACER -> { /* intentionally draws nothing */ }

            // ---- live-preview parity pass: see the Kind object's comment for why these were
            // missing. Each reuses the same field/control drawing this pass adds below. ----
            Kind.INPUT -> drawField(canvas, rect, attrs, singleLine = true, monospace = false)
            Kind.TEXTAREA -> drawField(canvas, rect, attrs, singleLine = false, monospace = false)
            Kind.PROGRESS -> drawProgress(canvas, rect, attrs, resolved)
            Kind.CHECKBOX -> drawCheckbox(canvas, rect, attrs, resolved)
            Kind.SWITCH -> drawSwitch(canvas, rect, attrs, resolved)
            Kind.AVATAR -> drawAvatar(canvas, rect, attrs, resolved)
            Kind.TABS -> { /* pure row of TabItem children -- they draw their own selected state */ }
            Kind.TABITEM -> drawTabItem(canvas, rect, attrs)
            Kind.ICON -> drawIcon(canvas, rect, attrs, resolved ?: defaultText)
            Kind.ICONBUTTON -> {
                drawBox(canvas, rect, attrs, resolved ?: defaultButton, defaultRadius = min(rect.width(), rect.height()) / 2f)
                drawIcon(canvas, rect, attrs, Color.WHITE)
            }

            // Ready-elements expansion (docs/RIN_ELEMENTS.md):
            Kind.LINK -> drawLink(canvas, rect, attrs, resolved)
            Kind.RADIO -> drawRadio(canvas, rect, attrs, resolved)
            Kind.SLIDER -> drawSlider(canvas, rect, attrs, resolved)
            Kind.SEARCH, Kind.SELECT, Kind.DATE, Kind.TIME -> drawField(canvas, rect, attrs, singleLine = true, monospace = false)
            Kind.FILE -> drawField(canvas, rect, attrs, singleLine = true, monospace = false,
                emptyPlaceholder = "لم يتم اختيار ملف")
            Kind.CODE_EDITOR -> drawField(canvas, rect, attrs, singleLine = false, monospace = true)
            Kind.CALCULATOR -> {
                drawBox(canvas, rect, attrs, resolved ?: defaultCard, defaultRadius = 14f)
                if ((node.optJSONArray("children")?.length() ?: 0) == 0 && rect.width() > 0f && rect.height() > 0f) {
                    drawText(canvas, rect, attrs, "🖩", defaultText, centered = true, singleLine = true)
                }
            }
            Kind.LIST -> { /* plain Column of rows -- children draw themselves */ }
            Kind.LISTITEM -> drawBox(canvas, rect, attrs, resolved ?: defaultContainer, defaultRadius = 0f)

            else -> drawBox(canvas, rect, attrs, resolved ?: defaultContainer, defaultRadius = 0f) // Column/Row/Stack/Box/Grid/Wrap/Custom
        }

        // Table draws its own header + cell children explicitly (needs column geometry), so it
        // walks its rows itself below and must not also be recursed into generically.
        if (kind == Kind.TABLE) return

        val children = node.optJSONArray("children")
        if (children != null) {
            for (i in 0 until children.length()) {
                drawNode(canvas, children.optJSONObject(i) ?: continue, skipOverlays)
            }
        }
    }

    /**
     * New: renders a `<Table columns="A,B,C">` — shaded header row using the column labels, thin
     * grid lines, then each TableRow's cell children drawn at the geometry the native Loom already
     * computed for them (see `layoutTable` in rin_loom_layout.h). This is the render backing
     * "printing"/displaying tables; exporting that same grid to an actual printer/PDF is a
     * host-app feature layered on top (e.g. via Android's PrintManager) using this same geometry.
     */
    private fun drawTable(canvas: Canvas, rect: RectF, node: JSONObject, attrs: JSONObject) {
        if (rect.width() <= 0f || rect.height() <= 0f) return
        val fontSize = attrs.optString("size").toFloatOrNull() ?: 14f
        val rowH = fontSize * 1.4f + 12f
        val columns = attrs.optString("columns").split(',').map { it.trim() }.filter { it.isNotEmpty() }

        fillPaint.shader = null; fillPaint.clearShadowLayer()

        var headerBottom = rect.top
        if (columns.isNotEmpty()) {
            val headerRect = RectF(rect.left, rect.top, rect.right, rect.top + rowH)
            fillPaint.color = defaultTableHeaderBg
            canvas.drawRect(headerRect, fillPaint)
            val perColW = rect.width() / columns.size
            for ((i, label) in columns.withIndex()) {
                val cellRect = RectF(rect.left + i * perColW, headerRect.top, rect.left + (i + 1) * perColW, headerRect.bottom)
                drawText(canvas, cellRect, attrs, label, defaultText, boldHint = true, singleLine = true)
            }
            headerBottom = headerRect.bottom
            strokePaint.color = defaultTableLine
            strokePaint.strokeWidth = 1f
            canvas.drawLine(rect.left, headerBottom, rect.right, headerBottom, strokePaint)
        }

        // Row separators + each row's own cells (the cells are real Fabric children with their
        // own geometry, so just recurse into them normally after drawing the separator line).
        val children = node.optJSONArray("children") ?: return
        for (i in 0 until children.length()) {
            val row = children.optJSONObject(i) ?: continue
            val ry = row.optDouble("y", 0.0).toFloat()
            strokePaint.color = defaultTableLine
            canvas.drawLine(rect.left, ry, rect.right, ry, strokePaint)
            drawNode(canvas, row)
        }
    }

    /** New: shared placeholder for Video/Audio/WebView(incl. YouTube links) — a dark box with a
     * play/link glyph and the src URL/label, matching how Image already previews as a placeholder
     * rather than a decoded bitmap (the real player/embed is a host-app runtime concern). */
    private fun drawMediaPlaceholder(canvas: Canvas, rect: RectF, attrs: JSONObject, glyph: String, src: String) {
        if (rect.width() <= 0f || rect.height() <= 0f) return
        fillPaint.shader = null; fillPaint.clearShadowLayer()
        fillPaint.color = defaultMedia
        val radius = 8f
        canvas.drawRoundRect(rect, radius, radius, fillPaint)

        textPaint.color = Color.argb(220, 255, 255, 255)
        textPaint.textSize = min(rect.height() * 0.35f, 28f)
        textPaint.isFakeBoldText = false
        val gw = textPaint.measureText(glyph)
        canvas.drawText(glyph, rect.left + (rect.width() - gw) / 2f, rect.top + rect.height() / 2f - (textPaint.descent() + textPaint.ascent()) / 2f, textPaint)

        if (src.isNotBlank()) {
            drawText(canvas, RectF(rect.left + 6f, rect.bottom - 18f, rect.right - 6f, rect.bottom - 2f),
                attrs, src, Color.argb(200, 255, 255, 255), singleLine = true)
        }
    }

    private fun drawBox(canvas: Canvas, rect: RectF, attrs: JSONObject, fallback: Int, defaultRadius: Float) {
        if (rect.width() <= 0f || rect.height() <= 0f) return
        val radius = attrs.optString("radius").toFloatOrNull() ?: defaultRadius
        val shadow = attrs.optString("shadow").toFloatOrNull()
        val gradient = attrs.optString("gradient").takeIf { it.contains(',') }

        fillPaint.shader = null
        fillPaint.clearShadowLayer()

        if (gradient != null) {
            val parts = gradient.split(',')
            val c1 = parseHexColor(parts.getOrNull(0)?.trim(), fallback)
            val c2 = parseHexColor(parts.getOrNull(1)?.trim(), fallback)
            fillPaint.shader = LinearGradient(rect.left, rect.top, rect.left, rect.bottom, c1, c2, Shader.TileMode.CLAMP)
        } else {
            fillPaint.color = parseHexColor(attrs.optString("color").ifBlank { null }, fallback)
        }

        if (shadow != null && shadow > 0f) {
            fillPaint.setShadowLayer(shadow, 0f, shadow / 2.5f, Color.argb(120, 0, 0, 0))
        }

        canvas.drawRoundRect(rect, radius, radius, fillPaint)

        // border= / borderColor=: a real stroked edge, inset by half its own width so it's drawn
        // fully inside the box's bounds (matches the border-box inset the native layout already
        // reserved for children — see loom::layoutSingleChildBox / layoutLinear).
        val borderWidthPx = attrs.optString("border").toFloatOrNull()
        if (borderWidthPx != null && borderWidthPx > 0f && rect.width() > 1f && rect.height() > 1f) {
            val strokeW = borderWidthPx
            strokePaint.color = parseHexColor(attrs.optString("borderColor").ifBlank { null }, Color.WHITE)
            strokePaint.strokeWidth = strokeW
            val inset = strokeW / 2f
            val strokeRect = RectF(rect.left + inset, rect.top + inset, rect.right - inset, rect.bottom - inset)
            val strokeRadius = max(0f, radius - inset)
            canvas.drawRoundRect(strokeRect, strokeRadius, strokeRadius, strokePaint)
        }
    }

    /**
     * Live-preview additions (see the Kind object's comment above for why these were missing).
     * Every one of these mirrors its native counterpart in rin_loom_paint.h in spirit — same
     * shape, same fill-fraction math — not pixel-identical, exactly the same relationship
     * [drawBox]/[drawText] already have to loom::paintBox()/paintText().
     */

    /** Input/TextArea/Search/Select/File/Date/Time/CodeEditor: a bordered field box (paintField's
     * native look) with left-aligned value= text, or a dimmer placeholder= when value is empty.
     * [emptyPlaceholder] lets File supply "لم يتم اختيار ملف" as its own convention when the
     * author left placeholder= unset, matching docs/RIN_ELEMENTS.md's description of it. */
    private fun drawField(
        canvas: Canvas, rect: RectF, attrs: JSONObject,
        singleLine: Boolean, monospace: Boolean, emptyPlaceholder: String = ""
    ) {
        if (rect.width() <= 0f || rect.height() <= 0f) return
        drawBox(canvas, rect, attrs, defaultFieldBg, defaultRadius = 8f)
        strokePaint.color = defaultFieldBorder
        strokePaint.strokeWidth = 1f
        canvas.drawRoundRect(rect, 8f, 8f, strokePaint)

        val value = attrs.optString("value")
        val placeholder = attrs.optString("placeholder").ifBlank { emptyPlaceholder }
        val (text, color) = if (value.isNotEmpty()) value to defaultText else placeholder to defaultPlaceholder
        if (text.isEmpty()) return

        val savedTypeface = textPaint.typeface
        if (monospace) textPaint.typeface = android.graphics.Typeface.MONOSPACE
        try {
            drawText(canvas, rect, attrs, text, color, singleLine = singleLine)
        } finally {
            textPaint.typeface = savedTypeface
        }
    }

    /** Progress: a pill-shaped track with a filled portion sized by value=/min=/max= — matches
     * loom::paintProgress()'s own track+fill (no thumb; that's what distinguishes it from Slider). */
    private fun drawProgress(canvas: Canvas, rect: RectF, attrs: JSONObject, resolved: Int?) {
        if (rect.width() <= 0f || rect.height() <= 0f) return
        val radius = rect.height() / 2f
        fillPaint.shader = null; fillPaint.clearShadowLayer()
        fillPaint.color = defaultTrack
        canvas.drawRoundRect(rect, radius, radius, fillPaint)

        val frac = progressFraction(attrs)
        if (frac > 0f) {
            fillPaint.color = resolved ?: defaultButton
            canvas.drawRoundRect(RectF(rect.left, rect.top, rect.left + rect.width() * frac, rect.bottom), radius, radius, fillPaint)
        }
    }

    /** Slider: Progress's own track+fill, plus a round thumb drawn at the current value's
     * position so it reads as draggable — matches loom::paintSlider() in rin_loom_paint.h. */
    private fun drawSlider(canvas: Canvas, rect: RectF, attrs: JSONObject, resolved: Int?) {
        drawProgress(canvas, rect, attrs, resolved)
        if (rect.width() <= 0f || rect.height() <= 0f) return
        val frac = progressFraction(attrs)
        val thumbSize = min(rect.height() * 1.6f, rect.height() + 6f)
        val cx = rect.left + rect.width() * frac
        val cy = rect.top + rect.height() / 2f
        fillPaint.shader = null; fillPaint.clearShadowLayer()
        fillPaint.color = resolved ?: defaultButton
        canvas.drawCircle(cx, cy, thumbSize / 2f, fillPaint)
    }

    private fun progressFraction(attrs: JSONObject): Float {
        val minV = attrs.optString("min").toFloatOrNull() ?: 0f
        val maxV = attrs.optString("max").toFloatOrNull() ?: 100f
        val value = (attrs.optString("value").toFloatOrNull() ?: minV).coerceIn(minV, maxV)
        return if (maxV > minV) (value - minV) / (maxV - minV) else 0f
    }

    /** Checkbox: a bordered square that fills solid with a tick mark when checked="true" —
     * matches the native filled-square look (as opposed to Radio's ring+dot below). */
    private fun drawCheckbox(canvas: Canvas, rect: RectF, attrs: JSONObject, resolved: Int?) {
        if (rect.width() <= 0f || rect.height() <= 0f) return
        val checked = attrs.optString("checked") == "true"
        val radius = attrs.optString("radius").toFloatOrNull() ?: 4f
        fillPaint.shader = null; fillPaint.clearShadowLayer()
        fillPaint.color = defaultFieldBg
        canvas.drawRoundRect(rect, radius, radius, fillPaint)
        strokePaint.color = if (checked) (resolved ?: defaultButton) else defaultFieldBorder
        strokePaint.strokeWidth = if (checked) 2f else 1.5f
        canvas.drawRoundRect(rect, radius, radius, strokePaint)
        if (checked) {
            fillPaint.color = resolved ?: defaultButton
            val inset = min(rect.width(), rect.height()) * 0.22f
            canvas.drawRoundRect(RectF(rect.left + inset, rect.top + inset, rect.right - inset, rect.bottom - inset), radius / 2f, radius / 2f, fillPaint)
            // simple tick
            strokePaint.color = Color.WHITE
            strokePaint.strokeWidth = max(1.5f, min(rect.width(), rect.height()) * 0.12f)
            val path = android.graphics.Path()
            path.moveTo(rect.left + rect.width() * 0.26f, rect.top + rect.height() * 0.52f)
            path.lineTo(rect.left + rect.width() * 0.44f, rect.top + rect.height() * 0.72f)
            path.lineTo(rect.left + rect.width() * 0.76f, rect.top + rect.height() * 0.30f)
            canvas.drawPath(path, strokePaint)
        }
    }

    /** Radio: same box Checkbox uses, drawn as a ring with a filled center dot when checked —
     * matches loom::paintRadio() (never a filled square, unlike Checkbox above). */
    private fun drawRadio(canvas: Canvas, rect: RectF, attrs: JSONObject, resolved: Int?) {
        if (rect.width() <= 0f || rect.height() <= 0f) return
        val checked = attrs.optString("checked") == "true"
        val radius = min(rect.width(), rect.height()) / 2f
        fillPaint.shader = null; fillPaint.clearShadowLayer()
        fillPaint.color = defaultFieldBg
        canvas.drawRoundRect(rect, radius, radius, fillPaint)
        strokePaint.color = if (checked) (resolved ?: defaultButton) else defaultFieldBorder
        strokePaint.strokeWidth = if (checked) 2f else 1.5f
        canvas.drawRoundRect(rect, radius, radius, strokePaint)
        if (checked) {
            fillPaint.color = resolved ?: defaultButton
            val inset = max(4f, min(rect.width(), rect.height()) * 0.28f)
            canvas.drawRoundRect(RectF(rect.left + inset, rect.top + inset, rect.right - inset, rect.bottom - inset),
                (min(rect.width(), rect.height()) - inset * 2f) / 2f, (min(rect.width(), rect.height()) - inset * 2f) / 2f, fillPaint)
        }
    }

    /** Switch: a pill track (tinted when on=true) with a circular knob at the corresponding end —
     * matches the native toggle look. */
    private fun drawSwitch(canvas: Canvas, rect: RectF, attrs: JSONObject, resolved: Int?) {
        if (rect.width() <= 0f || rect.height() <= 0f) return
        val on = attrs.optString("checked") == "true"
        val radius = rect.height() / 2f
        fillPaint.shader = null; fillPaint.clearShadowLayer()
        fillPaint.color = if (on)
