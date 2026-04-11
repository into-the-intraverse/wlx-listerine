#pragma once

#include "scope.h"
#include <string>
#include <unordered_set>

struct LanguageContext {
    const std::unordered_set<std::string>* keywords = nullptr;
    const std::unordered_set<std::string>* type_keywords = nullptr;
};

class ScopeMapper {
public:
    static Scope map(const std::string& language, const std::string& node_type);
    static LanguageContext for_language(const std::string& language);
    static Scope map(const LanguageContext& ctx, const std::string& node_type);
};
