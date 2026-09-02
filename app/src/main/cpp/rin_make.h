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

} // namespace rin
