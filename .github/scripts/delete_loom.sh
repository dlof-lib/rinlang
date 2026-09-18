#!/usr/bin/env bash
# يحذف ملفات/مجلدات Loom القديمة. الاستخدام: delete_loom.sh [apply|dry]
# آمن للتكرار (idempotent): يتجاوز أي مسار غير موجود.
set -euo pipefail
MODE="${1:-apply}"

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
  "tools/mirror-loom-preview"
  "tools/rin_loom_desktop.cpp"
  "tools/rin_loom_run"
  "tools/rin_loom_run.cpp"
)
# ملفات الاختبار (تتكرر في tests/tools و tools)
for d in tests/tools tools; do
  for n in chatbot_loom_persistent_interp chatbot_loom_ui_demo loom_actions loom_banner \
           loom_button loom_elements_real_output loom_events_effects loom_export \
           loom_group_object loom_icons loom_live_runtime loom_missing_components \
           loom_overlay loom_sizing loom_tokens; do
    OLD_PATHS+=("$d/test_$n.cpp")
  done
done

removed=0
for p in "${OLD_PATHS[@]}"; do
  if [ -e "$p" ]; then
    if [ "$MODE" = "apply" ]; then rm -rf -- "$p"; echo "deleted: $p"
    else echo "would delete: $p"; fi
    removed=$((removed+1))
  fi
done
echo "المجموع: $removed مساراً ($MODE)."
