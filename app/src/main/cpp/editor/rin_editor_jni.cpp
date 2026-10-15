// rin_editor_jni.cpp
// يعرض rinedit::EditorEngine إلى Kotlin عبر JNI. الجانب الكوتلن المطابق: RinNativeEditor.kt.
//
// كل نسخة من المحرر هي مؤشر C++ (rinedit::EditorEngine*) يُمرَّر إلى Kotlin كـ jlong (handle)؛
// Kotlin مسؤول عن استدعاء nativeDestroy() عند التخلّص من المحرر (مثلاً في View.onDetachedFromWindow).
#include <jni.h>
#define RIN_JNI_UTF_WITH_JNI 1
#include "../rin_jni_utf.h" // UTF-8 القياسي (لا MUTF-8): يحفظ توافق إزاحات البايت مع Kotlin عند الإيموجي
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <mutex>
#include "rin_editor_engine.h"

using rinedit::EditorEngine;
using rinedit::EditorDiagnostic;
using rinedit::HighlightSpan;
using rinedit::Match;
using rinedit::Position;

namespace {

std::string jstringToUtf8(JNIEnv* env, jstring s) {
    return rin_jni::toStd(env, s);
}

jstring utf8ToJstring(JNIEnv* env, const std::string& s) {
    return rin_jni::newJString(env, s);
}

EditorEngine* handleToEngine(jlong handle) {
    return reinterpret_cast<EditorEngine*>(static_cast<intptr_t>(handle));
}

jintArray toIntArray(JNIEnv* env, const std::vector<jint>& values) {
    jintArray arr = env->NewIntArray((jsize)values.size());
    if (!values.empty()) env->SetIntArrayRegion(arr, 0, (jsize)values.size(), values.data());
    return arr;
}

jintArray cursorArray(JNIEnv* env, Position p) {
    return toIntArray(env, {p.line, p.col});
}

} // namespace

extern "C" {

JNIEXPORT void JNICALL
Java_com_dlof_rinlang_RinNativeEditor_nativeSetLanguage(JNIEnv* env, jclass, jlong handle, jstring extension) {
    EditorEngine* e = handleToEngine(handle);
    if (e == nullptr) return;
    e->setLanguage(jstringToUtf8(env, extension));
}

JNIEXPORT jlong JNICALL
Java_com_dlof_rinlang_RinNativeEditor_nativeCreate(JNIEnv*, jclass) {
    return static_cast<jlong>(reinterpret_cast<intptr_t>(new EditorEngine()));
}

JNIEXPORT void JNICALL
Java_com_dlof_rinlang_RinNativeEditor_nativeDestroy(JNIEnv*, jclass, jlong handle) {
    delete handleToEngine(handle);
}

JNIEXPORT void JNICALL
Java_com_dlof_rinlang_RinNativeEditor_nativeSetText(JNIEnv* env, jclass, jlong handle, jstring text) {
    handleToEngine(handle)->setText(jstringToUtf8(env, text));
}

JNIEXPORT jstring JNICALL
Java_com_dlof_rinlang_RinNativeEditor_nativeGetText(JNIEnv* env, jclass, jlong handle) {
    return utf8ToJstring(env, handleToEngine(handle)->getText());
}

JNIEXPORT jint JNICALL
Java_com_dlof_rinlang_RinNativeEditor_nativeGetLineCount(JNIEnv*, jclass, jlong handle) {
    return handleToEngine(handle)->lineCount();
}

JNIEXPORT jstring JNICALL
Java_com_dlof_rinlang_RinNativeEditor_nativeGetLine(JNIEnv* env, jclass, jlong handle, jint line) {
    return utf8ToJstring(env, handleToEngine(handle)->getLine(line));
}

JNIEXPORT jintArray JNICALL
Java_com_dlof_rinlang_RinNativeEditor_nativeGetCursor(JNIEnv* env, jclass, jlong handle) {
    return cursorArray(env, handleToEngine(handle)->getCursor());
}

// [hasSelection(0/1), startLine, startCol, endLine, endCol] (مُطبَّعة: start <= end)
JNIEXPORT jintArray JNICALL
Java_com_dlof_rinlang_RinNativeEditor_nativeGetSelection(JNIEnv* env, jclass, jlong handle) {
    EditorEngine* e = handleToEngine(handle);
    int sl = 0, sc = 0, el = 0, ec = 0;
    e->getSelection(&sl, &sc, &el, &ec);
    return toIntArray(env, {e->hasSelection() ? 1 : 0, sl, sc, el, ec});
}

JNIEXPORT void JNICALL
Java_com_dlof_rinlang_RinNativeEditor_nativeSetCursor(JNIEnv*, jclass, jlong handle, jint line, jint col, jboolean extend) {
    handleToEngine(handle)->setCursor(line, col, extend == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_com_dlof_rinlang_RinNativeEditor_nativeSetSelection(JNIEnv*, jclass, jlong handle, jint aLine, jint aCol, jint bLine, jint bCol) {
    handleToEngine(handle)->setSelection(aLine, aCol, bLine, bCol);
}

JNIEXPORT void JNICALL
Java_com_dlof_rinlang_RinNativeEditor_nativeCollapseSelection(JNIEnv*, jclass, jlong handle) {
    handleToEngine(handle)->collapseSelectionToCursor();
}

JNIEXPORT jintArray JNICALL
Java_com_dlof_rinlang_RinNativeEditor_nativeInsertText(JNIEnv* env, jclass, jlong handle, jstring text, jboolean smart) {
    EditorEngine* e = handleToEngine(handle);
    e->insertText(jstringToUtf8(env, text), smart == JNI_TRUE);
    return cursorArray(env, e->getCursor());
}

JNIEXPORT jintArray JNICALL
Java_com_dlof_rinlang_RinNativeEditor_nativeDeleteBackward(JNIEnv* env, jclass, jlong handle) {
    EditorEngine* e = handleToEngine(handle);
    e->deleteBackward();
    return cursorArray(env, e->getCursor());
}

JNIEXPORT jintArray JNICALL
Java_com_dlof_rinlang_RinNativeEditor_nativeDeleteForward(JNIEnv* env, jclass, jlong handle) {
    EditorEngine* e = handleToEngine(handle);
    e->deleteForward();
    return cursorArray(env, e->getCursor());
}

JNIEXPORT jintArray JNICALL
Java_com_dlof_rinlang_RinNativeEditor_nativeReplaceRange(JNIEnv* env, jclass, jlong handle, jint sl, jint sc, jint el, jint ec, jstring text) {
    EditorEngine* e = handleToEngine(handle);
    e->replaceRange(sl, sc, el, ec, jstringToUtf8(env, text));
    return cursorArray(env, e->getCursor());
}

JNIEXPORT jintArray JNICALL
Java_com_dlof_rinlang_RinNativeEditor_nativeUndo(JNIEnv* env, jclass, jlong handle) {
    EditorEngine* e = handleToEngine(handle);
    if (!e->undo()) return nullptr;
    return cursorArray(env, e->getCursor());
}

JNIEXPORT jintArray JNICALL
Java_com_dlof_rinlang_RinNativeEditor_nativeRedo(JNIEnv* env, jclass, jlong handle) {
    EditorEngine* e = handleToEngine(handle);
    if (!e->redo()) return nullptr;
    return cursorArray(env, e->getCursor());
}

JNIEXPORT jintArray JNICALL
Java_com_dlof_rinlang_RinNativeEditor_nativeDuplicateLine(JNIEnv* env, jclass, jlong handle) {
    EditorEngine* e = handleToEngine(handle);
    e->duplicateCurrentLine();
    return cursorArray(env, e->getCursor());
}

JNIEXPORT jintArray JNICALL
Java_com_dlof_rinlang_RinNativeEditor_nativeDeleteLine(JNIEnv* env, jclass, jlong handle) {
    EditorEngine* e = handleToEngine(handle);
    e->deleteCurrentLine();
    return cursorArray(env, e->getCursor());
}

JNIEXPORT jintArray JNICALL
Java_com_dlof_rinlang_RinNativeEditor_nativeMoveLineUp(JNIEnv* env, jclass, jlong handle) {
    EditorEngine* e = handleToEngine(handle);
    e->moveLineUp();
    return cursorArray(env, e->getCursor());
}

JNIEXPORT jintArray JNICALL
Java_com_dlof_rinlang_RinNativeEditor_nativeMoveLineDown(JNIEnv* env, jclass, jlong handle) {
    EditorEngine* e = handleToEngine(handle);
    e->moveLineDown();
    return cursorArray(env, e->getCursor());
}

JNIEXPORT jintArray JNICALL
Java_com_dlof_rinlang_RinNativeEditor_nativeToggleComment(JNIEnv* env, jclass, jlong handle) {
    EditorEngine* e = handleToEngine(handle);
    e->toggleLineComment();
    return cursorArray(env, e->getCursor());
}

JNIEXPORT jintArray JNICALL
Java_com_dlof_rinlang_RinNativeEditor_nativeIndentSelection(JNIEnv* env, jclass, jlong handle) {
    EditorEngine* e = handleToEngine(handle);
    e->indentSelection();
    return cursorArray(env, e->getCursor());
}

JNIEXPORT jintArray JNICALL
Java_com_dlof_rinlang_RinNativeEditor_nativeUnindentSelection(JNIEnv* env, jclass, jlong handle) {
    EditorEngine* e = handleToEngine(handle);
    e->unindentSelection();
    return cursorArray(env, e->getCursor());
}

JNIEXPORT jint JNICALL
Java_com_dlof_rinlang_RinNativeEditor_nativeCheckBracketBalance(JNIEnv*, jclass, jlong handle) {
    return handleToEngine(handle)->checkBracketBalance();
}

// امتدادات التلوين مسطّحة: [line, startCol, endCol, kind] * N
JNIEXPORT jintArray JNICALL
Java_com_dlof_rinlang_RinNativeEditor_nativeGetHighlightSpansFlat(JNIEnv* env, jclass, jlong handle) {
    std::vector<HighlightSpan> spans = handleToEngine(handle)->computeHighlights();
    std::vector<jint> flat;
    flat.reserve(spans.size() * 4);
    for (const auto& s : spans) {
        flat.push_back(s.line);
        flat.push_back(s.startCol);
        flat.push_back(s.endCol);
        flat.push_back(static_cast<jint>(s.kind));
    }
    return toIntArray(env, flat);
}

// نتائج البحث مسطّحة: [line, startCol, endCol] * N
JNIEXPORT jintArray JNICALL
Java_com_dlof_rinlang_RinNativeEditor_nativeFindAllFlat(JNIEnv* env, jclass, jlong handle, jstring query, jboolean caseSensitive) {
    std::vector<Match> matches = handleToEngine(handle)->findAll(jstringToUtf8(env, query), caseSensitive == JNI_TRUE);
    std::vector<jint> flat;
    flat.reserve(matches.size() * 3);
    for (const auto& m : matches) {
        flat.push_back(m.line);
        flat.push_back(m.startCol);
        flat.push_back(m.endCol);
    }
    return toIntArray(env, flat);
}

// اقتراحات إكمال تلقائي (كلمات محجوزة + معرِّفات المستند) تبدأ بـ prefix.
JNIEXPORT jobjectArray JNICALL
Java_com_dlof_rinlang_RinNativeEditor_nativeGetSuggestions(JNIEnv* env, jclass, jlong handle, jstring prefix, jint maxResults) {
    std::vector<std::string> sugg = handleToEngine(handle)->collectSuggestions(jstringToUtf8(env, prefix), maxResults);
    jclass stringClass = env->FindClass("java/lang/String");
    jobjectArray arr = env->NewObjectArray((jsize)sugg.size(), stringClass, nullptr);
    for (size_t i = 0; i < sugg.size(); ++i) {
        jstring s = utf8ToJstring(env, sugg[i]);
        env->SetObjectArrayElement(arr, (jsize)i, s);
        env->DeleteLocalRef(s);
    }
    return arr;
}

JNIEXPORT jintArray JNICALL
Java_com_dlof_rinlang_RinNativeEditor_nativeLineStartPosition(JNIEnv* env, jclass, jlong handle, jint oneBasedLine) {
    return cursorArray(env, handleToEngine(handle)->lineStartPosition(oneBasedLine));
}

// تشخيص أخطاء حي: مصفوفة نصوص، عنصر واحد لكل تشخيص بالصيغة
// "line:startCol:endCol:severity:code:message" (message قد يحوي ':' فيُقسَّم بحدّ أقصى 6 أجزاء
// حتى لا تُقطَع الرسالة نفسها). استدعاء JNI واحد فقط (بدل مصفوفتين منفصلتين) يعني أن
// computeDiagnostics() — التي تُشغِّل Lexer + Parser كاملين على المستند — تُستدعى مرة واحدة فقط
// لكل إعادة حساب، لا مرتين، وهذا مهم لأن الجانب الكوتلن يعيد حسابها عند كل تغيّر نصّي (كل ضغطة مفتاح).
JNIEXPORT jobjectArray JNICALL
Java_com_dlof_rinlang_RinNativeEditor_nativeGetDiagnosticsPacked(JNIEnv* env, jclass, jlong handle) {
    std::vector<EditorDiagnostic> diags = handleToEngine(handle)->computeDiagnostics();
    jclass stringClass = env->FindClass("java/lang/String");
    jobjectArray arr = env->NewObjectArray((jsize)diags.size(), stringClass, nullptr);
    for (size_t i = 0; i < diags.size(); ++i) {
        const auto& d = diags[i];
        std::string packed = std::to_string(d.line) + ":" + std::to_string(d.startCol) + ":" +
                              std::to_string(d.endCol) + ":" + std::to_string(static_cast<int>(d.severity)) +
                              ":" + d.code + ":" + d.message;
        jstring s = utf8ToJstring(env, packed);
        env->SetObjectArrayElement(arr, (jsize)i, s);
        env->DeleteLocalRef(s);
    }
    return arr;
}

// --- أدوات احترافية (rin_editor_tools.h) ------------------------------------------------

// نطاقات الطيّ مسطّحة: [startLine, endLine] * N (صفرية، لا أعمدة فلا تحويل بايت/حرف).
JNIEXPORT jintArray JNICALL
Java_com_dlof_rinlang_RinNativeEditor_nativeGetFoldRangesFlat(JNIEnv* env, jclass, jlong handle) {
    auto ranges = handleToEngine(handle)->computeFoldRanges();
    std::vector<jint> flat;
    flat.reserve(ranges.size() * 2);
    for (const auto& r : ranges) { flat.push_back(r.first); flat.push_back(r.second); }
    return toIntArray(env, flat);
}

// هيكل الملف: "line:col:kind:name" لكل عنصر (name قد يحوي ':' فيُقسَّم بحدّ أقصى 4 أجزاء).
JNIEXPORT jobjectArray JNICALL
Java_com_dlof_rinlang_RinNativeEditor_nativeGetOutlinePacked(JNIEnv* env, jclass, jlong handle) {
    auto items = handleToEngine(handle)->computeOutline();
    jclass stringClass = env->FindClass("java/lang/String");
    jobjectArray arr = env->NewObjectArray((jsize)items.size(), stringClass, nullptr);
    for (size_t i = 0; i < items.size(); ++i) {
        std::string packed = std::to_string(items[i].line) + ":" + std::to_string(items[i].col) + ":" +
                             std::to_string(items[i].kind) + ":" + items[i].name;
        jstring s = utf8ToJstring(env, packed);
        env->SetObjectArrayElement(arr, (jsize)i, s);
        env->DeleteLocalRef(s);
    }
    return arr;
}

JNIEXPORT jboolean JNICALL
Java_com_dlof_rinlang_RinNativeEditor_nativeReindentDocument(JNIEnv*, jclass, jlong handle, jint width) {
    return handleToEngine(handle)->reindentDocument(width) ? JNI_TRUE : JNI_FALSE;
}

// يوسّع قالباً عند المؤشر؛ يُرجع null إن لم تكن الكلمة قالباً، وإلا موضع المؤشر الجديد.
JNIEXPORT jintArray JNICALL
Java_com_dlof_rinlang_RinNativeEditor_nativeExpandSnippet(JNIEnv* env, jclass, jlong handle) {
    EditorEngine* e = handleToEngine(handle);
    if (!e->expandSnippetAtCursor()) return nullptr;
    return cursorArray(env, e->getCursor());
}

JNIEXPORT jboolean JNICALL
Java_com_dlof_rinlang_RinNativeEditor_nativeIsSnippetTrigger(JNIEnv* env, jclass, jstring word) {
    return EditorEngine::isSnippetTrigger(jstringToUtf8(env, word)) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_com_dlof_rinlang_RinNativeEditor_nativeSnippetTitle(JNIEnv* env, jclass, jstring word) {
    return utf8ToJstring(env, EditorEngine::snippetTitle(jstringToUtf8(env, word)));
}

// --- IntelliSense (rin_intellisense.h) ---------------------------------------------------
// الحزم النصية تستخدم الفاصل U+001F بين الحقول (لا يظهر في أي نص مولَّد) و U+001E بين عناصر القوائم.

namespace {
const char kFS = '\x1F';
const char kRS = '\x1E';

jobjectArray toStringArray(JNIEnv* env, const std::vector<std::string>& v) {
    jclass stringClass = env->FindClass("java/lang/String");
    jobjectArray arr = env->NewObjectArray((jsize)v.size(), stringClass, nullptr);
    for (size_t i = 0; i < v.size(); ++i) {
        jstring js = utf8ToJstring(env, v[i]);
        env->SetObjectArrayElement(arr, (jsize)i, js);
        env->DeleteLocalRef(js);
    }
    env->DeleteLocalRef(stringClass);
    return arr;
}
} // namespace

// العنصر 0: "startCol\x1FendCol\x1Ftrigger"؛ ثم عنصر لكل اقتراح:
// "kind\x1Flabel\x1FinsertText\x1Fdetail\x1Fdoc\x1FcursorBack\x1Fflags\x1FmatchPos(,)"
JNIEXPORT jobjectArray JNICALL
Java_com_dlof_rinlang_RinNativeEditor_nativeComplete(JNIEnv* env, jclass, jlong handle, jboolean explicitInvoke, jint maxResults) {
    EditorEngine* e = handleToEngine(handle);
    if (e == nullptr) return nullptr;
    rinedit::intel::CompletionResult r = e->complete(explicitInvoke == JNI_TRUE, maxResults);
    std::vector<std::string> out;
    out.reserve(r.items.size() + 1);
    out.push_back(std::to_string(r.startCol) + kFS + std::to_string(r.endCol) + kFS + std::to_string((int)r.trigger));
    for (const auto& it : r.items) {
        std::string pos;
        for (size_t i = 0; i < it.matchPos.size(); ++i) { if (i) pos += ','; pos += std::to_string(it.matchPos[i]); }
        std::string row = std::to_string((int)it.kind);
        row += kFS; row += it.label;
        row += kFS; row += it.insertText;
        row += kFS; row += it.detail;
        row += kFS; row += it.doc;
        row += kFS; row += std::to_string(it.cursorBack);
        row += kFS; row += std::to_string(it.snippet ? 1 : 0);
        row += kFS; row += pos;
        out.push_back(std::move(row));
    }
    return toStringArray(env, out);
}

// "kind\x1Fname\x1Fret\x1Fdoc\x1Factive\x1Fparam1\x1Eparam2..." أو null
JNIEXPORT jstring JNICALL
Java_com_dlof_rinlang_RinNativeEditor_nativeSignatureHelp(JNIEnv* env, jclass, jlong handle) {
    EditorEngine* e = handleToEngine(handle);
    if (e == nullptr) return nullptr;
    rinedit::intel::SignatureHelp sh = e->signatureHelp();
    if (!sh.valid) return nullptr;
    std::string params;
    for (size_t i = 0; i < sh.params.size(); ++i) { if (i) params += kRS; params += sh.params[i]; }
    std::string row = std::to_string(sh.kind);
    row += kFS; row += sh.name;
    row += kFS; row += sh.ret;
    row += kFS; row += sh.doc;
    row += kFS; row += std::to_string(sh.active);
    row += kFS; row += params;
    return utf8ToJstring(env, row);
}

// "name\x1FkindName\x1Fsignature\x1Fdoc\x1FdefLine\x1FdefCol" أو null
JNIEXPORT jstring JNICALL
Java_com_dlof_rinlang_RinNativeEditor_nativeDescribe(JNIEnv* env, jclass, jlong handle, jint line, jint col) {
    EditorEngine* e = handleToEngine(handle);
    if (e == nullptr) return nullptr;
    rinedit::intel::SymbolInfo si = e->describeAt(line, col);
    if (!si.valid) return nullptr;
    std::string row = si.name;
    row += kFS; row += si.kindName;
    row += kFS; row += si.signature;
    row += kFS; row += si.doc;
    row += kFS; row += std::to_string(si.defLine);
    row += kFS; row += std::to_string(si.defCol);
    return utf8ToJstring(env, row);
}

JNIEXPORT void JNICALL
Java_com_dlof_rinlang_RinNativeEditor_nativeRecordCompletionUse(JNIEnv* env, jclass, jstring label) {
    EditorEngine::recordCompletionUse(jstringToUtf8(env, label));
}


} // extern "C"
