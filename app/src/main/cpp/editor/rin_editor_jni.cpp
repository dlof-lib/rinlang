// rin_editor_jni.cpp
// يعرض rinedit::EditorEngine إلى Kotlin عبر JNI. الجانب الكوتلن المطابق: RinNativeEditor.kt.
//
// كل نسخة من المحرر هي مؤشر C++ (rinedit::EditorEngine*) يُمرَّر إلى Kotlin كـ jlong (handle)؛
// Kotlin مسؤول عن استدعاء nativeDestroy() عند التخلّص من المحرر (مثلاً في View.onDetachedFromWindow).
#include <jni.h>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <mutex>
#include "rin_editor_engine.h"

using rinedit::EditorEngine;
using rinedit::HighlightSpan;
using rinedit::Match;
using rinedit::Position;

namespace {

std::string jstringToUtf8(JNIEnv* env, jstring s) {
    if (s == nullptr) return "";
    const char* chars = env->GetStringUTFChars(s, nullptr);
    std::string out(chars ? chars : "");
    env->ReleaseStringUTFChars(s, chars);
    return out;
}

jstring utf8ToJstring(JNIEnv* env, const std::string& s) {
    return env->NewStringUTF(s.c_str());
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

} // extern "C"
