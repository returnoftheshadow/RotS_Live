"""Turns off the trailing decltype rule; its fix deduces the return type, and this project writes auto only where the type is obvious from the line or cannot be written."""

# Stable ID; the registry lists it as the reason the base rule is off.
RULE_ID = "project/trailing-decltype"
# C++ sources and headers.
LANGUAGES = ("cpp", "cpp_header")
# Base rules that do not run in this project.
DISABLES = ("code-style-cpp-14/trailing-decltype",)
