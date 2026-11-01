#pragma once
#include "rin_ast.h"
#include <set>
#include <string>
#include <vector>

namespace rin {

// Capabilities used by Make Unit policy enforcement.
std::set<std::string> makeCapabilities(const std::vector<StmtPtr>& body);
std::vector<std::string> makeDefaultAllows(const std::string& kind);
void validateMakeUnit(const MakeStmt& make);

// RCS-1.0 §3.13 Security (Phase 0) — نفس محرّك enforcePolicy الذي يستخدمه validateMakeUnit،
// لكن لأي @container عادية استخدمت policy_block (use/need/allow/deny/strict) بلا أي قوائم
// افتراضية مبنية على kind (تلك حصراً لـ Make Unit). لا يُستدعى إطلاقاً إن كانت
// ContainerStmt::hasPolicy == false (الحالة الافتراضية لأي حاوية قديمة).
void validateContainerPolicy(const ContainerStmt& c);

// RCS-1.0 §3.13 Security (Phase 1) — نفس المبدأ تماماً لكن على @Containers.Group: القدرات
// المفحوصة تُجمَع (عبر makeCapabilities، التي تتفرّع أصلاً داخل ContainerGroupStmt::body بشكل
// متكرر -- انظر collect() في rin_make.cpp) من كامل شجرة المجموعة، أي كل الحاويات/المجموعات
// الفرعية المتداخلة بداخلها أيضاً، لا حاويات المجموعة المباشرة فقط. بلا أي قوائم افتراضية
// (كـ validateContainerPolicy تماماً)، ولا يُستدعى إطلاقاً إن كانت
// ContainerGroupStmt::hasPolicy == false.
void validateContainerGroupPolicy(const ContainerGroupStmt& g);

} // namespace rin
