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
import org.json.JSONObject
import kotlin.math.max
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
) : View(context, attrs) {

    /** Node kinds, mirroring `loom::StrandKind` (rin_loom_strand.h) exactly. */
    private object Kind {
        const val TEXT = "Text"; const val IMAGE = "Image"; const val BUTTON = "Button"
        const val CARD = "Card"; const val COLUMN = "Column"; const val ROW = "Row"
        const val STACK = "Stack"; const val DIVIDER = "Divider"
        // New: app chrome, media, and table — mirror the StrandKind additions noted in
        // rin_loom_layout.h (HEADER/TOPBAR/BOTTOMBAR/DRAWER/MENU/MENUITEM/TABLE/TABLEROW/
        // VIDEO/AUDIO/WEBVIEW/SCAFFOLD/SPLASH). These are *design-time placeholders*: the real
        // video/audio/web playback and the real page-navigation-on-tap-or-timer are wired up by
        // the host app/runtime, not drawn here — same relationship Image already has with a
        // real <img>.
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
    private val scrimColor = Color.argb(140, 0, 0, 0) // ~55% black — matches loom::scrimColor()'s RGB, opacity is this renderer's own convention (see rin_loom_paint.h's SCRIM_RECT comment)

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

    private val density = resources.displayMetrics.density

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
        val scale = density * zoom
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
        invalidate()
    }

    override fun onDetachedFromWindow() {
        super.onDetachedFromWindow()
        pendingAutoNavigate?.let { navHandler.removeCallbacks(it) }
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val scale = density * zoom
        val contentW = (rootWidthPx * scale).toInt()
        val contentH = (rootHeightPx * scale).toInt()

        // نحترم القيد الحقيقي القادم من الحاوية (match_parent + fillViewport في التخطيط) بدل
        // فرض حجم المحتوى دائماً، حتى تملأ المعاينة الشاشة كاملة عند التكبير 100% الافتراضي —
        // ونستخدم حجم المحتوى الطبيعي فقط عندما لا يوجد قيد صارم (UNSPECIFIED)، أي عندما يكون
        // المحتوى نفسه أكبر من المساحة المتاحة (تكبير > 100% مثلاً)، فيبقى قابلاً للتمرير.
        val widthMode = MeasureSpec.getMode(widthMeasureSpec)
        val widthSize = MeasureSpec.getSize(widthMeasureSpec)
        val finalWidth = when (widthMode) {
            MeasureSpec.EXACTLY -> widthSize
            MeasureSpec.AT_MOST -> min(contentW, widthSize).coerceAtLeast(suggestedMinimumWidth)
            else -> max(contentW, suggestedMinimumWidth)
        }

        val heightMode = MeasureSpec.getMode(heightMeasureSpec)
        val heightSize = MeasureSpec.getSize(heightMeasureSpec)
        val finalHeight = when (heightMode) {
            MeasureSpec.EXACTLY -> heightSize
            MeasureSpec.AT_MOST -> min(contentH, heightSize).coerceAtLeast(suggestedMinimumHeight)
            else -> max(contentH, suggestedMinimumHeight)
        }

        setMeasuredDimension(finalWidth, finalHeight)
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        canvas.save()
        canvas.scale(density * zoom, density * zoom)

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
            Kind.AUDIO -> drawMediaPlaceholder(canvas, rect, attrs, "♪", attrs.optString("src"))
            Kind.WEBVIEW -> drawMediaPlaceholder(canvas, rect, attrs, "🌐", attrs.optString("src"))
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
        fillPaint.color = if (on) (resolved ?: defaultButton) else defaultTrack
        canvas.drawRoundRect(rect, radius, radius, fillPaint)

        val knobRadius = rect.height() / 2f - 3f
        val knobCx = if (on) rect.right - radius else rect.left + radius
        fillPaint.color = Color.WHITE
        canvas.drawCircle(knobCx, rect.top + radius, knobRadius, fillPaint)
    }

    /** Avatar: a circular image (reusing the same decode/cache path Image already has) when
     * src= is set, else a tinted circle with the first letter of label=/name= as initials. */
    private fun drawAvatar(canvas: Canvas, rect: RectF, attrs: JSONObject, resolved: Int?) {
        if (rect.width() <= 0f || rect.height() <= 0f) return
        val src = attrs.optString("src")
        val bmp = if (src.isNotBlank()) loadBitmapForRect(src, rect.width().toInt(), rect.height().toInt()) else null
        val radius = min(rect.width(), rect.height()) / 2f
        if (bmp != null) {
            canvas.save()
            val clip = android.graphics.Path().apply { addCircle(rect.centerX(), rect.centerY(), radius, android.graphics.Path.Direction.CW) }
            canvas.clipPath(clip)
            val bw = bmp.width.toFloat(); val bh = bmp.height.toFloat()
            val scale = max(rect.width() / bw, rect.height() / bh)
            val dw = bw * scale; val dh = bh * scale
            canvas.drawBitmap(bmp, null, RectF(rect.centerX() - dw / 2f, rect.centerY() - dh / 2f, rect.centerX() + dw / 2f, rect.centerY() + dh / 2f), bitmapPaint)
            canvas.restore()
            return
        }
        fillPaint.shader = null; fillPaint.clearShadowLayer()
        fillPaint.color = resolved ?: defaultButton
        canvas.drawCircle(rect.centerX(), rect.centerY(), radius, fillPaint)
        val initial = (attrs.optString("label").ifBlank { attrs.optString("name") }).trim().firstOrNull()?.uppercaseChar()?.toString() ?: ""
        if (initial.isNotEmpty()) drawText(canvas, rect, attrs, initial, Color.WHITE, centered = true, boldHint = true, singleLine = true)
    }

    /** TabItem: its label, underlined/tinted when selected="true" — Tabs itself (the row
     * container) draws nothing beyond its children, same as Kind.TABS above. */
    private fun drawTabItem(canvas: Canvas, rect: RectF, attrs: JSONObject) {
        if (rect.width() <= 0f || rect.height() <= 0f) return
        val selected = attrs.optString("selected") == "true"
        drawText(canvas, rect, attrs, attrs.optString("label").ifBlank { attrs.optString("text") },
            if (selected) defaultButton else defaultText, centered = true, boldHint = selected, singleLine = true)
        if (selected) {
            strokePaint.color = defaultButton
            strokePaint.strokeWidth = 2f
            canvas.drawLine(rect.left, rect.bottom - 1f, rect.right, rect.bottom - 1f, strokePaint)
        }
    }

    /**
     * Link concepts (docs/link.md): plain link-toned text, underlined by default like any real
     * hyperlink (`underline="false"` turns that off — e.g. for a Link used as a plain nav item),
     * and switched to a muted [defaultVisitedLink] tone when `visited="true"` so a user can tell
     * an already-followed link apart from a fresh one, same convention the web uses. Behavior
     * (where it navigates/opens) is a separate concept — see `href`/`onOpenUrl`/`onNavigate`.
     */
    private fun drawLink(canvas: Canvas, rect: RectF, attrs: JSONObject, resolved: Int?) {
        if (rect.width() <= 0f || rect.height() <= 0f) return
        val visited = attrs.optString("visited") == "true"
        val color = resolved ?: (if (visited) defaultVisitedLink else defaultLink)
        val text = attrs.optString("text")
        drawText(canvas, rect, attrs, text, color)
        val underline = attrs.optString("underline").ifBlank { "true" } == "true"
        if (underline && text.isNotEmpty()) {
            val sizeSp = attrs.optString("size").toFloatOrNull() ?: 14f
            textPaint.textSize = sizeSp
            val hPad = 4f
            val textWidth = min(rect.width() - hPad * 2f, textPaint.measureText(text))
            if (textWidth > 0f) {
                val baseline = rect.top + rect.height() / 2f - (textPaint.descent() + textPaint.ascent()) / 2f
                val underlineY = baseline + textPaint.descent() * 0.9f
                strokePaint.color = color
                strokePaint.strokeWidth = max(1f, sizeSp / 14f)
                canvas.drawLine(rect.left + hPad, underlineY, rect.left + hPad + textWidth, underlineY, strokePaint)
            }
        }
    }

    /** Icon/IconButton: no glyph atlas on this preview surface (real icon rendering is a
     * host-app/runtime concern, same relationship Image's real bitmap decode has to a raw
     * placeholder) — draws the first letter of name= as a plain-text stand-in so an icon still
     * reads as "something is here" rather than an empty box. */
    private fun drawIcon(canvas: Canvas, rect: RectF, attrs: JSONObject, fallbackColor: Int) {
        if (rect.width() <= 0f || rect.height() <= 0f) return
        val glyph = attrs.optString("name").trim().firstOrNull()?.uppercaseChar()?.toString() ?: "•"
        drawText(canvas, rect, attrs, glyph, fallbackColor, centered = true, boldHint = true, singleLine = true)
    }

    /**
     * Greedy word-wrap using REAL measured glyph widths (mirrors `loom::wrapText` in
     * rin_loom_layout.h, which only has a rough per-codepoint estimate to size the box before
     * paint). Matching algorithms means the line count — and therefore the box height the native
     * layout already committed to — lines up with what actually gets drawn here.
     */
    private fun wrapLines(text: String, paint: TextPaint, maxWidth: Float): List<String> {
        if (text.isEmpty()) return listOf("")
        if (maxWidth <= 0f) return listOf(text)
        val words = text.split(" ").filter { it.isNotEmpty() }
        if (words.isEmpty()) return listOf("")
        val lines = mutableListOf<String>()
        var line = ""
        for (w in words) {
            val candidate = if (line.isEmpty()) w else "$line $w"
            line = if (line.isEmpty() || paint.measureText(candidate) <= maxWidth) candidate
            else { lines.add(line); w }
        }
        if (line.isNotEmpty() || lines.isEmpty()) lines.add(line)
        return lines
    }

    private fun drawText(
        canvas: Canvas, rect: RectF, attrs: JSONObject, text: String,
        fallbackColor: Int, centered: Boolean = false, boldHint: Boolean = false, singleLine: Boolean = false
    ) {
        if (text.isEmpty() || rect.width() <= 0f || rect.height() <= 0f) return
        val sizeSp = attrs.optString("size").toFloatOrNull() ?: 14f
        textPaint.color = parseHexColor(attrs.optString("color").ifBlank { null }, fallbackColor)
        textPaint.textSize = sizeSp
        textPaint.isFakeBoldText = boldHint
        textPaint.isAntiAlias = true

        val hPad = 4f
        val available = max(4f, rect.width() - hPad * 2f)

        if (singleLine) {
            val truncated = TextUtils.ellipsize(text, textPaint, available, TextUtils.TruncateAt.END)
            val textWidth = textPaint.measureText(truncated, 0, truncated.length)
            val startX = if (centered) rect.left + (rect.width() - textWidth) / 2f else rect.left + hPad
            val baseline = rect.top + rect.height() / 2f - (textPaint.descent() + textPaint.ascent()) / 2f
            canvas.drawText(truncated, 0, truncated.length, startX, baseline, textPaint)
            return
        }

        // Real multi-line body text: wrap, then draw one line per row filling the box top-down —
        // the box's height was sized by loom::measureText for exactly this many lines * lineHeight,
        // so no vertical centering of the whole block is needed (it already fills the box).
        val lineHeight = textPaint.textSize * 1.4f
        var maxLines = max(1, (rect.height() / lineHeight).toInt())
        attrs.optString("maxLines").toIntOrNull()?.let { if (it > 0) maxLines = min(maxLines, it) }

        var lines = wrapLines(text, textPaint, available)
        val overflowed = lines.size > maxLines
        if (overflowed) lines = lines.subList(0, maxLines)

        var baseline = rect.top - textPaint.ascent()
        for ((i, rawLine) in lines.withIndex()) {
            val isLastVisible = i == lines.lastIndex
            val line = if (overflowed && isLastVisible)
                TextUtils.ellipsize(rawLine, textPaint, available, TextUtils.TruncateAt.END).toString()
            else rawLine
            val lineWidth = textPaint.measureText(line)
            val startX = if (centered) rect.left + (rect.width() - lineWidth) / 2f else rect.left + hPad
            canvas.drawText(line, startX, baseline, textPaint)
            baseline += lineHeight
        }
    }

    // ---- chat message formatting (`format=` on a Text node — see the Kind.TEXT dispatch above)
    // ----

    private val defaultCodeBg = Color.rgb(18, 19, 26) // dark, distinct from defaultCard/Bubble fill
    private val defaultCodeText = Color.rgb(180, 230, 180) // faint terminal-green, readable on dark bg

    /** `format="code"`: a monospace block with its own dark rounded background, matching how a
     * real code fence renders in a chat client. Reuses [drawBox]/[drawText] as-is (no new paint
     * fields) — only the typeface and the two default colors differ from plain body text. */
    private fun drawCodeText(canvas: Canvas, rect: RectF, attrs: JSONObject, text: String, fallbackColor: Int) {
        if (text.isEmpty() || rect.width() <= 0f || rect.height() <= 0f) return
        drawBox(canvas, rect, JSONObject().apply {
            // an explicit color= on the Strand still governs the bubble itself (drawBox already
            // reads attrs.color); the code block only supplies its own fallback background.
            if (attrs.has("color")) put("color", attrs.optString("color"))
            if (attrs.has("radius")) put("radius", attrs.optString("radius"))
        }, defaultCodeBg, defaultRadius = 8f)

        val savedTypeface = textPaint.typeface
        textPaint.typeface = android.graphics.Typeface.MONOSPACE
        try {
            drawText(canvas, rect, attrs, text, if (attrs.has("color")) fallbackColor else defaultCodeText)
        } finally {
            textPaint.typeface = savedTypeface // textPaint is a shared field — never leak this elsewhere
        }
    }

    /** One word plus whether it should render bold (i.e. it came from inside a `**...**` span). */
    private data class MdWord(val text: String, val bold: Boolean)

    /** Splits `text` on `**bold**` markers into a flat word list tagged bold/not-bold. Only bold
     * spans are supported (the one inline style `botReplyMarkdown` callers reach for most, e.g.
     * "بإمكانك تفعيل الحساب من **الإعدادات > الحساب > تفعيل**" from
     * examples/chatbot_container_demo.rin) — anything fancier (headings, links, code spans inside
     * markdown) still shows as plain text with its literal `**`/`` ` ``/`#` markers rather than
     * silently dropping content, which is the safer failure mode for a chat transcript. */
    private fun parseMarkdownWords(text: String): List<MdWord> {
        val words = mutableListOf<MdWord>()
        var bold = false
        var i = 0
        val sb = StringBuilder()
        fun flushWord() { if (sb.isNotEmpty()) { words.add(MdWord(sb.toString(), bold)); sb.clear() } }
        while (i < text.length) {
            val c = text[i]
            if (c == '*' && i + 1 < text.length && text[i + 1] == '*') {
                flushWord()
                bold = !bold
                i += 2
                continue
            }
            if (c == ' ') { flushWord(); i++; continue }
            sb.append(c)
            i++
        }
        flushWord()
        return words
    }

    /** `format="markdown"`: wraps like plain body text (same `available`/`lineHeight` box the
     * caller's box was already sized for) but renders `**bold**` spans in real bold, word by
     * word, instead of drawing the literal asterisks. RTL scripts (Arabic, as in this project's
     * own chat examples) still lay out left-to-right word-by-word here, same simplification
     * [wrapLines] already makes for plain text above — a real bidi run reorder is out of scope for
     * this renderer. */
    private fun drawMarkdownText(canvas: Canvas, rect: RectF, attrs: JSONObject, text: String, fallbackColor: Int) {
        if (text.isEmpty() || rect.width() <= 0f || rect.height() <= 0f) return
        val sizeSp = attrs.optString("size").toFloatOrNull() ?: 14f
        textPaint.color = parseHexColor(attrs.optString("color").ifBlank { null }, fallbackColor)
        textPaint.textSize = sizeSp
        textPaint.isAntiAlias = true

        val hPad = 4f
        val available = max(4f, rect.width() - hPad * 2f)
        val spaceWidth = run { textPaint.isFakeBoldText = false; textPaint.measureText(" ") }

        // Greedy word-wrap identical in spirit to wrapLines(), but keeping each word's bold flag
        // (measuring width for bold words with isFakeBoldText=true, since bold glyphs are wider).
        val words = parseMarkdownWords(text)
        val lines = mutableListOf<MutableList<MdWord>>(mutableListOf())
        var lineWidth = 0f
        for (w in words) {
            textPaint.isFakeBoldText = w.bold
            val wWidth = textPaint.measureText(w.text)
            val current = lines.last()
            val candidateWidth = if (current.isEmpty()) wWidth else lineWidth + spaceWidth + wWidth
            if (current.isNotEmpty() && candidateWidth > available) {
                lines.add(mutableListOf(w))
                lineWidth = wWidth
            } else {
                current.add(w)
                lineWidth = candidateWidth
            }
        }

        val lineHeight = textPaint.textSize * 1.4f
        var maxLines = max(1, (rect.height() / lineHeight).toInt())
        attrs.optString("maxLines").toIntOrNull()?.let { if (it > 0) maxLines = min(maxLines, it) }
        val visibleLines = if (lines.size > maxLines) lines.subList(0, maxLines) else lines

        var baseline = rect.top - textPaint.ascent()
        for (line in visibleLines) {
            var x = rect.left + hPad
            for (w in line) {
                textPaint.isFakeBoldText = w.bold
                canvas.drawText(w.text, x, baseline, textPaint)
                x += textPaint.measureText(w.text) + spaceWidth
            }
            baseline += lineHeight
        }
        textPaint.isFakeBoldText = false // textPaint is shared — never leave bold set for the next node
    }

    private fun drawDivider(canvas: Canvas, rect: RectF, attrs: JSONObject, fallback: Int) {
        fillPaint.shader = null
        fillPaint.clearShadowLayer()
        fillPaint.color = parseHexColor(attrs.optString("color").ifBlank { null }, fallback)
        canvas.drawRect(rect, fillPaint)
    }

    /** Resolves `src=` to a real file: relative paths are relative to the current project's root
     * (same root save/installation/file already use — [RinEngine.currentBaseDir]); absolute paths
     * and `file://` URIs are used as-is. Null if nothing exists there. */
    private fun resolveImageFile(src: String): java.io.File? {
        if (src.isBlank() || src.contains("://") && !src.startsWith("file://")) return null // http(s) etc. not fetched here
        val stripped = src.removePrefix("file://")
        val direct = java.io.File(stripped)
        if (direct.isAbsolute) return if (direct.isFile) direct else null
        val base = RinEngine.currentBaseDir()
        if (base.isBlank()) return null
        val relative = java.io.File(base, stripped)
        return if (relative.isFile) relative else null
    }

    /** Decodes (and caches, downsampled to roughly [targetW]x[targetH] to keep memory sane) the
     * bitmap for [src], or null if it can't be resolved/decoded — cached too, so a bad src isn't
     * re-stat'd on every single frame while the preview is live. */
    private fun loadBitmapForRect(src: String, targetW: Int, targetH: Int): android.graphics.Bitmap? {
        if (src.isBlank() || targetW <= 0 || targetH <= 0) return null
        val cacheKey = "$src|$targetW|$targetH"
        bitmapCache.get(cacheKey)?.let { return it }
        if (cacheKey in missingSrc) return null

        val file = resolveImageFile(src)
        if (file == null) { missingSrc.add(cacheKey); return null }
        return try {
            val bounds = android.graphics.BitmapFactory.Options().apply { inJustDecodeBounds = true }
            android.graphics.BitmapFactory.decodeFile(file.absolutePath, bounds)
            var sample = 1
            while (bounds.outWidth / (sample * 2) >= targetW && bounds.outHeight / (sample * 2) >= targetH) sample *= 2
            val opts = android.graphics.BitmapFactory.Options().apply { inSampleSize = sample }
            val bmp = android.graphics.BitmapFactory.decodeFile(file.absolutePath, opts)
            if (bmp != null) bitmapCache.put(cacheKey, bmp) else missingSrc.add(cacheKey)
            bmp
        } catch (t: Throwable) {
            missingSrc.add(cacheKey)
            null
        }
    }

    /** Decodes (and caches) a representative frame near the start of the video at [src] as a real
     * thumbnail — same "decode once, cache, remember failures" shape as [loadBitmapForRect] just
     * above, only using MediaMetadataRetriever instead of BitmapFactory since the source is a
     * video container, not a still image. Downsampled to roughly [targetW]x[targetH] for memory. */
    private fun loadVideoThumbnail(src: String, targetW: Int, targetH: Int): android.graphics.Bitmap? {
        if (src.isBlank() || targetW <= 0 || targetH <= 0) return null
        val cacheKey = "$src|$targetW|$targetH"
        videoThumbCache.get(cacheKey)?.let { return it }
        if (cacheKey in missingVideoSrc) return null

        val file = resolveImageFile(src)
        if (file == null) { missingVideoSrc.add(cacheKey); return null }
        val retriever = android.media.MediaMetadataRetriever()
        return try {
            retriever.setDataSource(file.absolutePath)
            val frame = retriever.getFrameAtTime(0L, android.media.MediaMetadataRetriever.OPTION_CLOSEST_SYNC)
            if (frame == null) {
                missingVideoSrc.add(cacheKey)
                null
            } else {
                val scale = min(1f, max(targetW.toFloat() / frame.width, targetH.toFloat() / frame.height))
                val bmp = if (scale < 1f) {
                    android.graphics.Bitmap.createScaledBitmap(
                        frame, max(1, (frame.width * scale).toInt()), max(1, (frame.height * scale).toInt()), true
                    ).also { if (it !== frame) frame.recycle() }
                } else frame
                videoThumbCache.put(cacheKey, bmp)
                bmp
            }
        } catch (t: Throwable) {
            missingVideoSrc.add(cacheKey)
            null
        } finally {
            try { retriever.release() } catch (t: Throwable) { /* best-effort cleanup */ }
        }
    }

    /** Real `<Video src="...">`: draws an actual decoded frame from the file — the same
     * cover-crop + rounded-corner treatment [drawImage] gives a still image — with a dark scrim
     * and a centered play glyph on top so it still reads as "video", not a photo. Falls back to
     * [drawMediaPlaceholder]'s plain glyph+label box when src is blank/unresolvable/undecodable,
     * same relationship [drawImage] already has to [drawImagePlaceholder]. */
    private fun drawVideoPreview(canvas: Canvas, rect: RectF, attrs: JSONObject) {
        if (rect.width() <= 0f || rect.height() <= 0f) return
        val src = attrs.optString("src")
        val thumb = if (src.isNotBlank()) loadVideoThumbnail(src, rect.width().toInt(), rect.height().toInt()) else null
        if (thumb == null) { drawMediaPlaceholder(canvas, rect, attrs, "▶", src); return }

        val radius = attrs.optString("radius").toFloatOrNull() ?: 8f
        canvas.save()
        val clip = android.graphics.Path().apply { addRoundRect(rect, radius, radius, android.graphics.Path.Direction.CW) }
        canvas.clipPath(clip)
        val bw = thumb.width.toFloat()
        val bh = thumb.height.toFloat()
        val scale = max(rect.width() / bw, rect.height() / bh)
        val dw = bw * scale
        val dh = bh * scale
        val dstLeft = rect.left + (rect.width() - dw) / 2f
        val dstTop = rect.top + (rect.height() - dh) / 2f
        canvas.drawBitmap(thumb, null, RectF(dstLeft, dstTop, dstLeft + dw, dstTop + dh), bitmapPaint)

        // dark scrim so the play glyph reads clearly over any frame content, bright or dark
        fillPaint.shader = null; fillPaint.clearShadowLayer()
        fillPaint.color = Color.argb(70, 0, 0, 0)
        canvas.drawRect(rect, fillPaint)
        canvas.restore()

        // centered play button: translucent white circle + glyph
        val knobRadius = min(rect.width(), rect.height()) * 0.16f
        if (knobRadius > 2f) {
            fillPaint.color = Color.argb(210, 255, 255, 255)
            canvas.drawCircle(rect.centerX(), rect.centerY(), knobRadius, fillPaint)
            textPaint.color = Color.rgb(30, 31, 40)
            textPaint.textSize = knobRadius * 1.1f
            textPaint.isFakeBoldText = false
            val glyph = "▶"
            val gw = textPaint.measureText(glyph)
            canvas.drawText(glyph, rect.centerX() - gw / 2f + knobRadius * 0.08f,
                rect.centerY() - (textPaint.descent() + textPaint.ascent()) / 2f, textPaint)
        }

        if (src.isNotBlank()) {
            drawText(canvas, RectF(rect.left + 6f, rect.bottom - 18f, rect.right - 6f, rect.bottom - 2f),
                attrs, src, Color.argb(220, 255, 255, 255), singleLine = true)
        }
    }

    /** Real `<Image src="...">`: draws the actual decoded bitmap, cropped/rounded to the box the
     * native layout already computed. `fit="contain"` letterboxes instead of the default
     * cover-and-crop. Falls back to [drawImagePlaceholder] when src is blank/unresolvable. */
    private fun drawImage(canvas: Canvas, rect: RectF, attrs: JSONObject) {
        if (rect.width() <= 0f || rect.height() <= 0f) return
        val src = attrs.optString("src")
        val bmp = loadBitmapForRect(src, rect.width().toInt(), rect.height().toInt())
        if (bmp == null) { drawImagePlaceholder(canvas, rect, attrs); return }

        val radius = attrs.optString("radius").toFloatOrNull() ?: 8f
        canvas.save()
        val clip = android.graphics.Path().apply { addRoundRect(rect, radius, radius, android.graphics.Path.Direction.CW) }
        canvas.clipPath(clip)

        val bw = bmp.width.toFloat()
        val bh = bmp.height.toFloat()
        val scale = if (attrs.optString("fit") == "contain") min(rect.width() / bw, rect.height() / bh)
        else max(rect.width() / bw, rect.height() / bh)
        val dw = bw * scale
        val dh = bh * scale
        val dstLeft = rect.left + (rect.width() - dw) / 2f
        val dstTop = rect.top + (rect.height() - dh) / 2f
        canvas.drawBitmap(bmp, null, RectF(dstLeft, dstTop, dstLeft + dw, dstTop + dh), bitmapPaint)
        canvas.restore()
    }

    private fun drawImagePlaceholder(canvas: Canvas, rect: RectF, attrs: JSONObject) {
        if (rect.width() <= 0f || rect.height() <= 0f) return
        fillPaint.shader = null
        fillPaint.clearShadowLayer()
        fillPaint.color = defaultImage
        val radius = 8f
        canvas.drawRoundRect(rect, radius, radius, fillPaint)

        // simple "picture" glyph (mountain + dot) so an <Image> reads as an image, not a blank card
        strokePaint.color = Color.argb(160, 255, 255, 255)
        val pad = min(rect.width(), rect.height()) * 0.22f
        val glyph = RectF(rect.left + pad, rect.top + pad, rect.right - pad, rect.bottom - pad)
        if (glyph.width() > 2f && glyph.height() > 2f) {
            canvas.drawCircle(glyph.left + glyph.width() * 0.22f, glyph.top + glyph.height() * 0.28f, glyph.width() * 0.09f, strokePaint)
            val path = android.graphics.Path()
            path.moveTo(glyph.left, glyph.bottom)
            path.lineTo(glyph.left + glyph.width() * 0.38f, glyph.top + glyph.height() * 0.4f)
            path.lineTo(glyph.left + glyph.width() * 0.62f, glyph.bottom - glyph.height() * 0.2f)
            path.lineTo(glyph.left + glyph.width() * 0.8f, glyph.top + glyph.height() * 0.55f)
            path.lineTo(glyph.right, glyph.bottom)
            canvas.drawPath(path, strokePaint)
        }

        val label = attrs.optString("src").ifBlank { null }
        if (label != null) drawText(canvas, RectF(rect.left, rect.bottom - 16f, rect.right, rect.bottom), attrs, label, Color.argb(210, 255, 255, 255), singleLine = true)
    }

    private fun drawGrid(canvas: Canvas) {
        val step = 8f // 8dp baseline grid, standard mobile design unit
        var gx = 0f
        while (gx <= rootWidthPx) { canvas.drawLine(gx, 0f, gx, rootHeightPx.toFloat(), gridPaint); gx += step }
        var gy = 0f
        while (gy <= rootHeightPx) { canvas.drawLine(0f, gy, rootWidthPx.toFloat(), gy, gridPaint); gy += step }
    }

    private fun drawSafeArea(canvas: Canvas) {
        val margin = 16f
        canvas.drawRect(margin, margin, rootWidthPx - margin, rootHeightPx - margin, safeAreaPaint)
    }

    private fun drawInspectHighlight(canvas: Canvas, node: JSONObject) {
        val x = node.optDouble("x", 0.0).toFloat()
        val y = node.optDouble("y", 0.0).toFloat()
        val w = node.optDouble("w", 0.0).toFloat()
        val h = node.optDouble("h", 0.0).toFloat()
        if (w <= 0f || h <= 0f) return
        canvas.drawRect(x, y, x + w, y + h, inspectHighlightPaint)
    }

    // ---- hit test (client-side; independent of the native tap-dispatch used for onTap handlers) ----

    private fun hitTest(node: JSONObject, x: Float, y: Float): JSONObject? {
        val nx = node.optDouble("x", 0.0).toFloat()
        val ny = node.optDouble("y", 0.0).toFloat()
        val nw = node.optDouble("w", 0.0).toFloat()
        val nh = node.optDouble("h", 0.0).toFloat()
        if (x < nx || y < ny || x > nx + nw || y > ny + nh) return null

        val children = node.optJSONArray("children")
        if (children != null) {
            for (i in children.length() - 1 downTo 0) {
                val child = children.optJSONObject(i) ?: continue
                val hit = hitTest(child, x, y)
                if (hit != null) return hit
            }
        }
        return node
    }

    private fun parseHexColor(hex: String?, fallback: Int): Int {
        if (hex.isNullOrBlank() || hex.length < 7 || hex[0] != '#') return fallback
        return try { Color.parseColor(hex) } catch (t: Throwable) { fallback }
    }

    /**
     * Reads the native engine's already-resolved paint color for [node] — the
     * `"resolvedColor"` field `loom::fabricToJson` now emits from `loom::resolveColor()` (see
     * rin_loom_paint.h), i.e. `tone=`, a semantic `color="primary"`-style role name, and the
     * active `@theme=` already baked in exactly as the real Dye rasterizer would paint it.
     *
     * [parseHexColor] above can only ever understand a literal `color="#RRGGBB"` written
     * straight into the .rin source, so before this every one of those three cases was
     * completely invisible to this view and it silently fell back to its own hardcoded
     * per-kind palette instead — the preview not matching the code's actual colors. Null only
     * for a cached/old session JSON that predates this field, in which case callers fall back
     * to that same hardcoded default as before.
     */
    private fun resolvedColor(node: JSONObject): Int? {
        val hex = node.optString("resolvedColor")
        if (hex.length != 7 || hex[0] != '#') return null
        return try { Color.parseColor(hex) } catch (t: Throwable) { null }
    }
}
