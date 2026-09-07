// tools/rin_loom_desktop.cpp — Rin Loom Desktop: a real native window for the Loomtime engine.
//
// This is the third real Loomtime host, alongside the Kotlin/Compose Android renderer
// (LoomFabricView.kt, driven over JNI) and the CLI (rin_loom_run.cpp, driven over stdin). It is
// NOT a mock or a re-implementation of paint/layout logic: every pixel on screen comes from
// rin_loom_session_render_rgb(), which is the exact same Dye::paintWithOverlay() +
// rasterizeToBuffer() pass the engine's own PNG export uses (see rin_loom_paint.h's
// "the ONE rasterizer in the engine" comment) — this tool only blits that buffer to an X11
// window with XPutImage. Taps are dispatched through rin_loom_session_tap(), the same Needle
// hit-test/dispatch path a real onTap handler (including a `fun` with a `while` loop) runs
// through on Android. Only Xlib is used — no SDL/GTK/Qt/Skia/Cairo dependency — so the build
// stays as dependency-light as the rest of this repo's tools/ (matches rin_loom_run.cpp's own
// "no external UI framework" convention, just applied to a real window instead of stdout).
//
// Usage:
//   rin_loom_desktop window <file.rin> [rootWidth=390]
//       Opens a live X11 window against the DISPLAY environment already set (works headlessly
//       under Xvfb for CI/testing — see tools/README_DESKTOP.md).
//         - Left-click anywhere    -> dispatches a real tap at that pixel (onTap runs for real;
//                                     Warp state changes and the window re-lays-out + repaints).
//         - 'r'                    -> force re-reads the file from disk and hot-diffs it in
//                                     (Shuttle), keeping current Warp state, exactly like the
//                                     CLI's `edit` command.
//         - the file is also polled for mtime changes every ~400ms, so saving the .rin file in
//           an editor hot-reloads the window automatically with no keypress.
//         - 'q' / Esc / the window's close button -> quit.
//
//   rin_loom_desktop shot <file.rin> <out.png> [rootWidth=390]
//       Headless one-shot: renders the file and writes a real PNG, no X server touched at all.
//       Useful wherever `window` mode's event loop can't run (plain CI, no Xvfb).
//
// Build (see tools/README_DESKTOP.md for the full command / canonical source list):
//   g++ -std=c++17 -O2 -o rin_loom_desktop tools/rin_loom_desktop.cpp \
//       app/src/main/cpp/loom/rin_loom_c_api.cpp <...core .cpp files...> \
//       -lz -lX11 -I app/src/main/cpp

#include "../app/src/main/cpp/loom/rin_loom_c_api.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/select.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

time_t mtimeOf(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return 0;
    return st.st_mtime;
}

// ---------------------------------------------------------------------------------------------
// Headless one-shot: `rin_loom_desktop shot <file.rin> <out.png> [rootWidth]`
// ---------------------------------------------------------------------------------------------
int cmdShot(const std::string& path, const std::string& outPng, int width) {
    std::string src;
    if (!readFile(path, src)) {
        fprintf(stderr, "error: cannot read %s\n", path.c_str());
        return 1;
    }
    void* session = rin_loom_session_create(src.c_str(), width);
    if (!session) {
        fprintf(stderr, "error: could not create Loomtime session\n");
        return 1;
    }
    int ok = rin_loom_session_export_png(session, outPng.c_str());
    rin_loom_session_free(session);
    if (!ok) {
        fprintf(stderr, "error: rin_loom_session_export_png failed (parse error in %s?)\n", path.c_str());
        return 1;
    }
    fprintf(stderr, "wrote %s\n", outPng.c_str());
    return 0;
}

// ---------------------------------------------------------------------------------------------
// Real interactive window: `rin_loom_desktop window <file.rin> [rootWidth]`
// ---------------------------------------------------------------------------------------------
struct DesktopWindow {
    Display* display = nullptr;
    int screen = 0;
    Window win = 0;
    GC gc = nullptr;
    Atom wmDeleteWindow;
    int rootWidth = 390;
    int curW = 0, curH = 0; // last size the window/XImage were sized to

    void* session = nullptr;
    std::string filePath;
    time_t lastMtime = 0;

    bool open(int width) {
        rootWidth = width;
        display = XOpenDisplay(nullptr);
        if (!display) {
            fprintf(stderr,
                "error: could not open X display (DISPLAY=%s). If you're on a headless\n"
                "machine, run this under Xvfb, e.g.:\n"
                "  Xvfb :99 -screen 0 %dx900x24 &\n"
                "  DISPLAY=:99 ./rin_loom_desktop window <file.rin>\n"
                "or use `rin_loom_desktop shot <file.rin> <out.png>` for a headless PNG instead.\n",
                getenv("DISPLAY") ? getenv("DISPLAY") : "(unset)", rootWidth);
            return false;
        }
        screen = DefaultScreen(display);
        curW = rootWidth;
        curH = 600; // placeholder until the first real render tells us the true content height
        win = XCreateSimpleWindow(display, RootWindow(display, screen), 0, 0,
                                   curW, curH, 1,
                                   BlackPixel(display, screen), WhitePixel(display, screen));
        XStoreName(display, win, "Rin Loom — Desktop");
        XSelectInput(display, win, ExposureMask | ButtonPressMask | KeyPressMask | StructureNotifyMask);
        wmDeleteWindow = XInternAtom(display, "WM_DELETE_WINDOW", False);
        XSetWMProtocols(display, win, &wmDeleteWindow, 1);
        gc = XCreateGC(display, win, 0, nullptr);
        XMapWindow(display, win);
        return true;
    }

    // Pulls the session's current Fabric through the engine's real rasterizer and blits it,
    // resizing the X11 window first if the content's measured height has changed.
    void repaint() {
        int w = 0, h = 0;
        unsigned char* rgb = rin_loom_session_render_rgb(session, &w, &h);
        if (!rgb || w <= 0 || h <= 0) {
            if (rgb) rin_loom_free_buffer(rgb);
            return; // session has no valid Fabric right now (e.g. a Snag) -- leave last frame up
        }
        if (w != curW || h != curH) {
            curW = w; curH = h;
            XResizeWindow(display, win, curW, curH);
        }

        XImage* img = XCreateImage(display, DefaultVisual(display, screen),
                                    DefaultDepth(display, screen), ZPixmap, 0,
                                    nullptr, w, h, 32, 0);
        img->data = static_cast<char*>(std::malloc(img->bytes_per_line * h));
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                size_t i = ((size_t)y * w + x) * 3;
                unsigned long pixel = (unsigned long)((rgb[i] << 16) | (rgb[i + 1] << 8) | rgb[i + 2]);
                XPutPixel(img, x, y, pixel); // handles the display's actual byte order/masks for us
            }
        }
        rin_loom_free_buffer(rgb);

        XPutImage(display, win, gc, img, 0, 0, 0, 0, w, h);
        XFlush(display);
        std::free(img->data);
        img->data = nullptr;
        XDestroyImage(img);
    }

    void reloadFromDisk() {
        std::string src;
        if (!readFile(filePath, src)) return;
        char* j = rin_loom_session_update_source(session, src.c_str());
        if (j) rin_free_string(j); // window mode doesn't need the JSON envelope, just the repaint
        lastMtime = mtimeOf(filePath);
        repaint();
    }

    void dispatchTap(int x, int y) {
        char* j = rin_loom_session_tap(session, (double)x, (double)y);
        if (j) rin_free_string(j); // window mode doesn't need the JSON envelope, just the repaint
        repaint();
    }

    void run(const std::string& path) {
        filePath = path;
        std::string src;
        if (!readFile(path, src)) {
            fprintf(stderr, "error: cannot read %s\n", path.c_str());
            return;
        }
        session = rin_loom_session_create(src.c_str(), rootWidth);
        lastMtime = mtimeOf(path);
        repaint();

        int xfd = ConnectionNumber(display);
        bool running = true;
        while (running) {
            // Drain any events already queued before we consider blocking on select(), same as
            // any Xlib main loop -- XPending() never blocks.
            while (XPending(display) > 0) {
                XEvent ev;
                XNextEvent(display, &ev);
                if (ev.type == Expose) {
                    repaint();
                } else if (ev.type == ButtonPress) {
                    dispatchTap(ev.xbutton.x, ev.xbutton.y);
                } else if (ev.type == KeyPress) {
                    KeySym ks = XLookupKeysym(&ev.xkey, 0);
                    if (ks == XK_q || ks == XK_Escape) { running = false; break; }
                    if (ks == XK_r) reloadFromDisk();
                } else if (ev.type == ClientMessage) {
                    if ((Atom)ev.xclient.data.l[0] == wmDeleteWindow) { running = false; break; }
                }
            }
            if (!running) break;

            // Block on the X connection with a short timeout so external file edits (saved from
            // an editor, not this process) still get picked up without needing a keypress.
            fd_set fds; FD_ZERO(&fds); FD_SET(xfd, &fds);
            struct timeval tv; tv.tv_sec = 0; tv.tv_usec = 400 * 1000;
            int r = select(xfd + 1, &fds, nullptr, nullptr, &tv);
            if (r == 0) {
                time_t mt = mtimeOf(filePath);
                if (mt != 0 && mt != lastMtime) reloadFromDisk();
            }
        }
    }

    void close() {
        if (session) { rin_loom_session_free(session); session = nullptr; }
        if (gc) XFreeGC(display, gc);
        if (win) XDestroyWindow(display, win);
        if (display) XCloseDisplay(display);
    }
};

int cmdWindow(const std::string& path, int width) {
    DesktopWindow dw;
    if (!dw.open(width)) return 1;
    dw.run(path);
    dw.close();
    return 0;
}

void usage(const char* prog) {
    fprintf(stderr,
        "Rin Loom Desktop — real X11 window for the real Loomtime engine\n"
        "usage:\n"
        "  %s window <file.rin> [rootWidth=390]\n"
        "  %s shot   <file.rin> <out.png> [rootWidth=390]\n",
        prog, prog);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) { usage(argv[0]); return 1; }
    std::string mode = argv[1];
    std::string path = argv[2];

    if (mode == "shot") {
        if (argc < 4) { usage(argv[0]); return 1; }
        std::string outPng = argv[3];
        int width = argc > 4 ? std::atoi(argv[4]) : 390;
        return cmdShot(path, outPng, width);
    }
    if (mode == "window") {
        int width = argc > 3 ? std::atoi(argv[3]) : 390;
        return cmdWindow(path, width);
    }

    usage(argv[0]);
    return 1;
}
