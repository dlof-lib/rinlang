#!/usr/bin/env bash
# delete_old_loom_files.sh
#
# يحذف الملفات/المجلدات القديمة بالاسم "Loom" بعد أن حلّت محلّها نظيراتها
# الجديدة تحت اسم "indsin" (Interface Design). شغّله من جذر المشروع
# (نفس المجلد الذي يحوي README.md) بعد فكّ ضغط أو دمج
# rinlang-indsin-changed-only.zip فوق مشروعك.
#
# الاستخدام:
#   bash delete_old_loom_files.sh          # يطبع ما سيُحذف فقط (وضع آمن dry-run)
#   bash delete_old_loom_files.sh --apply   # يحذف فعلياً

set -euo pipefail

APPLY=0
if [ "${1:-}" = "--apply" ]; then APPLY=1; fi

# مسارات نسبية إلى جذر المشروع (52 مساراً: ملفات ومجلدات)
OLD_PATHS=(
  "app/src/main/cpp/loom"
  "app/src/main/java/com/dlof/rinlang/LoomFabricView.kt"
  "app/src/main/java/com/dlof/rinlang/LoomPreviewActivity.kt"
  "app/src/main/java/com/dlof/rinlang/LoomPreviewManager.kt"
  "app/src/main/java/com/dlof/rinlang/LoomViewTracer.kt"
  "app/src/main/res/drawable/bg_loom_device_frame.xml"
  "app/src/main/res/drawable/bg_loom_error_banner.xml"
  "app/src/main/res/drawable/bg_loom_inspector_panel.xml"
  "app/src/main/res/layout/activity_loom_preview.xml"
  "examples/chatbot_loom_ui_demo.rin"
  "examples/container_loom_api_demo.rin"
  "examples/object_loom_preview_demo.rin"
  "examples/relyRIN_loom_container_demo.rin"
  "examples/samples/loom_banner_demo.rin"
  "examples/samples/loom_button_library_demo.rin"
  "examples/samples/loom_missing_components_demo.rin"
  "examples/samples/loom_showcase.rin"
  "examples/samples/loom_sizing_demo.rin"
  "examples/samples/loom_tokens_demo.rin"
  "pitok/tools/pitok_loom_e2e.cpp"
  "tests/tools/test_chatbot_loom_persistent_interp.cpp"
  "tests/tools/test_chatbot_loom_ui_demo.cpp"
  "tests/tools/test_loom_actions.cpp"
  "tests/tools/test_loom_banner.cpp"
  "tests/tools/test_loom_button.cpp"
  "tests/tools/test_loom_elements_real_output.cpp"
  "tests/tools/test_loom_export.cpp"
  "tests/tools/test_loom_icons.cpp"
  "tests/tools/test_loom_live_runtime.cpp"
  "tests/tools/test_loom_missing_components.cpp"
  "tests/tools/test_loom_overlay.cpp"
  "tests/tools/test_loom_sizing.cpp"
  "tests/tools/test_loom_tokens.cpp"
  "tools/mirror-loom-preview"
  "tools/rin_loom_desktop.cpp"
  "tools/rin_loom_run"
  "tools/rin_loom_run.cpp"
  "tools/test_chatbot_loom_persistent_interp.cpp"
  "tools/test_chatbot_loom_ui_demo.cpp"
  "tools/test_loom_actions.cpp"
  "tools/test_loom_banner.cpp"
  "tools/test_loom_button.cpp"
  "tools/test_loom_elements_real_output.cpp"
  "tools/test_loom_events_effects.cpp"
  "tools/test_loom_export.cpp"
  "tools/test_loom_group_object.cpp"
  "tools/test_loom_icons.cpp"
  "tools/test_loom_live_runtime.cpp"
  "tools/test_loom_missing_components.cpp"
  "tools/test_loom_overlay.cpp"
  "tools/test_loom_sizing.cpp"
  "tools/test_loom_tokens.cpp"
)

removed=0
missing=0
for p in "${OLD_PATHS[@]}"; do
  if [ -e "$p" ]; then
    if [ "$APPLY" -eq 1 ]; then
      rm -rf -- "$p"
      echo "deleted: $p"
    else
      echo "would delete: $p"
    fi
    removed=$((removed+1))
  else
    echo "skip (not found): $p"
    missing=$((missing+1))
  fi
done

echo ""
if [ "$APPLY" -eq 1 ]; then
  echo "تم حذف $removed مساراً ($missing غير موجود أصلاً)."
else
  echo "وضع تجريبي (dry-run): $removed مساراً سيُحذف، $missing غير موجود."
  echo "أعد التشغيل بالخيار --apply لتنفيذ الحذف فعلياً."
fi

