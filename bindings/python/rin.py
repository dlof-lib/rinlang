"""
rin.py — استدعاء لغة Rin مباشرة من بايثون عبر ctypes.

المتطلبات: بناء المكتبة المشتركة أولاً (مرة واحدة):
    cd bindings
    cmake -B build && cmake --build build
سينتج build/librin.so (لينكس) أو build/librin.dylib (macOS) أو build/rin.dll (ويندوز).

الاستخدام:
    from rin import Rin

    rin = Rin("./build/librin.so")
    print(rin.run('print "Hello from Rin, called from Python!";'))

    # جلسة تحافظ على basePath ثابت بين عدة تشغيلات:
    session = rin.session("./rin_data")
    session.run('@container=c1 text a="x"; installation c1; .end/container')
    session.close()
"""
import ctypes
import os
import platform


def _default_lib_name() -> str:
    system = platform.system()
    if system == "Windows":
        return "rin.dll"
    if system == "Darwin":
        return "librin.dylib"
    return "librin.so"


class Rin:
    def __init__(self, lib_path: str | None = None):
        path = lib_path or os.path.join(os.path.dirname(__file__), "..", "build", _default_lib_name())
        self._lib = ctypes.CDLL(path)

        self._lib.rin_run.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
        self._lib.rin_run.restype = ctypes.c_void_p

        self._lib.rin_free_string.argtypes = [ctypes.c_void_p]
        self._lib.rin_free_string.restype = None

        self._lib.rin_engine_version.argtypes = []
        self._lib.rin_engine_version.restype = ctypes.c_char_p

        self._lib.rin_session_create.argtypes = [ctypes.c_char_p]
        self._lib.rin_session_create.restype = ctypes.c_void_p

        self._lib.rin_session_run.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        self._lib.rin_session_run.restype = ctypes.c_void_p

        self._lib.rin_session_free.argtypes = [ctypes.c_void_p]
        self._lib.rin_session_free.restype = None

    def version(self) -> str:
        return self._lib.rin_engine_version().decode("utf-8")

    def run(self, source: str, base_path: str = "") -> str:
        """يشغّل برنامج Rin كاملاً مرة واحدة، ويُرجع كل ما طُبع (نص)."""
        ptr = self._lib.rin_run(source.encode("utf-8"), base_path.encode("utf-8"))
        try:
            return ctypes.cast(ptr, ctypes.c_char_p).value.decode("utf-8")
        finally:
            self._lib.rin_free_string(ptr)

    def session(self, base_path: str = "") -> "RinSession":
        """جلسة تحافظ على basePath واحد بين عدة استدعاءات run() متتالية."""
        handle = self._lib.rin_session_create(base_path.encode("utf-8"))
        return RinSession(self._lib, handle)


class RinSession:
    def __init__(self, lib, handle):
        self._lib = lib
        self._handle = handle

    def run(self, source: str) -> str:
        ptr = self._lib.rin_session_run(self._handle, source.encode("utf-8"))
        try:
            return ctypes.cast(ptr, ctypes.c_char_p).value.decode("utf-8")
        finally:
            self._lib.rin_free_string(ptr)

    def close(self):
        if self._handle:
            self._lib.rin_session_free(self._handle)
            self._handle = None

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()
