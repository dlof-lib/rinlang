/**
 * rin.js — استدعاء لغة Rin من Node.js عبر ffi-napi + ref-napi.
 *
 * التثبيت:
 *   npm install ffi-napi ref-napi
 * (ابنِ المكتبة المشتركة أولاً كما في bindings/CMakeLists.txt)
 *
 * الاستخدام:
 *   const { Rin } = require("./rin");
 *   const rin = new Rin("../build/librin.so"); // أو .dylib / .dll حسب النظام
 *   console.log(rin.run('print "Hello from Rin, called from Node.js!";'));
 */
const ffi = require("ffi-napi");
const ref = require("ref-napi");

const voidPtr = ref.refType(ref.types.void);

class Rin {
  constructor(libPath) {
    this._lib = ffi.Library(libPath, {
      rin_run: [voidPtr, ["string", "string"]],
      rin_free_string: ["void", [voidPtr]],
      rin_engine_version: ["string", []],
      rin_session_create: [voidPtr, ["string"]],
      rin_session_run: [voidPtr, [voidPtr, "string"]],
      rin_session_free: ["void", [voidPtr]],
    });
  }

  version() {
    return this._lib.rin_engine_version();
  }

  /** يشغّل برنامج Rin كاملاً مرة واحدة، ويُرجع كل ما طُبع. */
  run(source, basePath = "") {
    const ptr = this._lib.rin_run(source, basePath);
    const text = ref.readCString(ptr, 0);
    this._lib.rin_free_string(ptr);
    return text;
  }

  /** جلسة تحافظ على basePath ثابت بين عدة استدعاءات run() متتالية. */
  session(basePath = "") {
    const handle = this._lib.rin_session_create(basePath);
    const lib = this._lib;
    return {
      run(source) {
        const ptr = lib.rin_session_run(handle, source);
        const text = ref.readCString(ptr, 0);
        lib.rin_free_string(ptr);
        return text;
      },
      close() {
        lib.rin_session_free(handle);
      },
    };
  }
}

module.exports = { Rin };
