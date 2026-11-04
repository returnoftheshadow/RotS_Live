"""Turns off the C allocation rule; game objects are never wrapped in smart pointers, and their text fields keep the legacy allocation pair."""

# Stable ID; the registry lists it as the reason the base rule is off.
RULE_ID = "project/c-allocation"
# C++ sources and headers.
LANGUAGES = ("cpp", "cpp_header")
# Base rules that do not run in this project.
DISABLES = ("code-style-cpp-11/c-allocation",)
