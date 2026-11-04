"""Turns off the Allman brace rule; this project's format targets apply clang-format's WebKit layout."""

# Stable ID; the registry lists it as the reason the base rule is off.
RULE_ID = "project/webkit-layout"
# C++ sources and headers.
LANGUAGES = ("cpp", "cpp_header")
# Base rules that do not run in this project.
DISABLES = ("code-style-cpp/allman-braces",)
