#pragma once

#include <initializer_list>
#include <string>

namespace pinx::i18n {

// Load strings for the given language code ("en-US", "pt-BR", or "" = system/en-US).
// Call once at startup and again whenever the user changes language.
void Init(const std::string &lang_code);

// Return translated string for a dot-notation key ("browse.loading").
// Returns the key itself if not found.
const std::string &tr(const std::string &key);

// tr() with positional substitution: {0}, {1}, … replaced by args.
std::string trf(const std::string &key, std::initializer_list<std::string> args);

} // namespace pinx::i18n
