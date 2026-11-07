#pragma once
#include <string>
#include <string_view>

namespace clipub {

/// Selects the UI language: "es", "en" or "auto" (from LANG/LC_ALL).
void setLanguage(const std::string& lang);
std::string currentLanguage();
/// Translated UI string; returns the key itself when missing.
const char* tr(std::string_view key);

}  // namespace clipub
