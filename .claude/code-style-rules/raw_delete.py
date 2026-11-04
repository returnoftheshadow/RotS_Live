"""Turns off the raw delete rule; game objects are never wrapped in smart pointers and are freed only on the extraction path."""

# Stable ID; the registry lists it as the reason the base rule is off.
RULE_ID = "project/raw-delete"
# C++ sources and headers.
LANGUAGES = ("cpp", "cpp_header")
# Base rules that do not run in this project.
DISABLES = ("code-style-cpp-11/raw-delete",)
