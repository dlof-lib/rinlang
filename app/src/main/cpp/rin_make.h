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

} // namespace rin
