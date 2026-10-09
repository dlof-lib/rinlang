#!/usr/bin/env python3
"""اختبارات الطرفية التفاعلية عبر pty حقيقي (محرّر السطر، Tab، Ctrl-C، اللصق، التاريخ).

الاستخدام:  RIN_BIN=/path/to/rin python3 tests/terminal/pty_test.py
"""
import fcntl, os, pty, re, select, struct, sys, tempfile, termios, time

RIN = os.environ.get("RIN_BIN", "rin")
failures = []


def session(keys, cols=80, rows=24, histfile=None, args=()):
    hist = histfile or tempfile.mktemp(prefix="rin_hist_")
    pid, fd = pty.fork()
    if pid == 0:
        env = dict(os.environ, TERM="xterm-256color", LANG="C.UTF-8", RIN_LANG="en",
                   RIN_HISTFILE=hist, RIN_TERMINAL_RC="/nonexistent")
        env.pop("NO_COLOR", None)
        os.execve(RIN, ["rin", "terminal", "--no-banner", *args], env)
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
    out = b""

    def drain(t):
        nonlocal out
        end = time.time() + t
        while time.time() < end:
            r, _, _ = select.select([fd], [], [], 0.05)
            if r:
                try:
                    d = os.read(fd, 65536)
                except OSError:
                    return
                if not d:
                    return
                out += d

    drain(0.6)
    for k in keys:
        if isinstance(k, float):
            drain(k)
            continue
        os.write(fd, k if isinstance(k, bytes) else k.encode())
        drain(0.3)
    drain(0.3)
    try:
        os.close(fd)
        os.waitpid(pid, 0)
    except OSError:
        pass
    text = re.sub(r"\x1b\[[0-9;?]*[A-Za-z]", "", out.decode("utf-8", "replace"))
    return text.replace("\r\n", "\n").replace("\r", "\n"), hist


def check(name, cond, text=""):
    print(("PASS " if cond else "FAIL ") + name)
    if not cond:
        failures.append(name)
        print("---- output ----\n" + text[-800:] + "\n----------------")


# 1) تعبير حرّ + حالة تبقى + دالة متعددة الأسطر + فك } تلقائي
t, _ = session(["let x = 5;\r", "x * 3\r", "fun f(a) {\r", "return a * 2;\r", "}\r", "f(21)\r", "\x04"])
check("autoprint number", "=> 15" in t, t)
check("multi-line function + call", "=> 42" in t, t)
check("continuation prompt shown", "┆" in t, t)

# 2) Ctrl-C يوقف حلقة لا نهائية والحالة محفوظة
t, _ = session(["let keep = 41;\r", "while (true) { }\r", 1.0, "\x03", 0.8, "keep + 1\r", "\x04"])
check("Ctrl-C interrupts running program", "interrupted" in t, t)
check("state survives interrupt", "=> 42" in t, t)

# 3) Tab: قائمة المرشّحين ثم الإكمال
t, _ = session(["let alphabet = 1;\r", "let alpine = 2;\r", "alp\t", "\x15", "alph\t", " + 1\r", "\x04"])
check("tab lists candidates", "alphabet  alpine" in t, t)
check("tab completes unique prefix", "=> 2" in t, t)

# 4) لصق متعدد الأسطر (bracketed paste)
paste = b"\x1b[200~let a = 10;\nfun sq(v) {\n    return v * v;\n}\nsq(a)\n\x1b[201~"
t, _ = session([paste, 0.5, "\x04"])
check("bracketed multi-line paste", "=> 100" in t, t)

# 5) التاريخ يُحفظ ويُستعاد في جلسة جديدة (+ بحث بالبادئة بالسهم)
_, h = session(["let persisted = 7;\r", "\x04"])
t, _ = session(["let pers\x1b[A", "\r", "persisted\r", "\x04"], histfile=h)
check("history file written", os.path.exists(h) and "persisted" in open(h).read())
check("history recalled with Up (prefix search) and re-run", "=> 7" in t, t)

# 6) نافذة ضيّقة (30 عموداً) لا تكسر الرسم
t, _ = session(["let a_very_long_variable_name_here = 123456789 + 987654321;\r", "a_very_long_variable_name_here\r", "\x04"], cols=30)
check("narrow terminal (30 cols)", "=> 1111111110" in t, t)

# 7) input() داخل الطرفية
t, _ = session(['let n = input("name? ");\r', 0.3, "Rin\r", 'n\r', "\x04"])
check("input() reads from the terminal", '=> "Rin"' in t, t)

# 8) indsin داخل الطرفية: نقرة فأرة حقيقية (SGR 1006) على زر +1 ترفع العدّاد، ويُستعاد الطرف عند الخروج
import subprocess
proj_dir = tempfile.mkdtemp(prefix="rin_proj_")
subprocess.run([RIN, "terminal", "--batch", "--no-history", "--no-banner"], cwd=proj_dir, input=":new ui --template indsin\n", text=True, capture_output=True)
main_rin = os.path.join(proj_dir, "ui", "src", "main.rin")
dump = subprocess.run([RIN, "indsin", main_rin, "--plain", "--cols", "80"], capture_output=True, text=True).stdout.split("\n")
rows_with_btn = [i for i, l in enumerate(dump) if "+1" in l]
check("indsin --plain renders text", bool(rows_with_btn) and any("Count: 0" in l for l in dump), "\n".join(dump))
if rows_with_btn:
    r = rows_with_btn[0]; c = dump[r].index("+1")
    pid, fd = pty.fork()
    if pid == 0:
        env = dict(os.environ, TERM="xterm-256color", LANG="C.UTF-8", RIN_LANG="en")
        env.pop("NO_COLOR", None)
        os.execve(RIN, ["rin", "indsin", main_rin], env)
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", 40, 80, 0, 0))
    buf = b""
    def drain2(t):
        global buf
        end = time.time() + t
        while time.time() < end:
            rr, _, _ = select.select([fd], [], [], 0.05)
            if rr:
                try: d = os.read(fd, 1 << 20)
                except OSError: return
                if not d: return
                buf += d
    drain2(1.0)
    for _ in range(3):
        os.write(fd, ("\x1b[<0;%d;%dM\x1b[<0;%d;%dm" % (c + 2, r + 1, c + 2, r + 1)).encode()); drain2(0.6)
    os.write(fd, b"q"); drain2(0.6)
    t = buf.decode("utf-8", "replace")
    check("indsin TUI: alt screen + mouse enabled", "\x1b[?1049h" in t and "\x1b[?1006h" in t, t[:200])
    check("indsin TUI: 3 mouse clicks -> Count: 3", "Count: 3" in t, t[-400:])
    check("indsin TUI: terminal restored on quit", "\x1b[?1049l" in t and "\x1b[?1000l" in t, t[-200:])
    try: os.close(fd); os.waitpid(pid, 0)
    except OSError: pass

sys.exit(1 if failures else 0)
