BUILD_DIR := build
SRC_DIR := src
CMAKE := cmake
CMAKE_CONFIGURE_ARGS ?= -DCMAKE_CXX_COMPILER=g++
# Empty or "address". Forwarded even when empty so the configure line keeps one shape and
# re-asserts the default when run by hand; the rule only runs on a fresh BUILD_DIR, so
# changing SANITIZE needs a new directory.
SANITIZE ?=
CMAKE_CACHE := $(BUILD_DIR)/CMakeCache.txt
PYTHON ?= python3

.PHONY: help configure setup build test run smoke-account format clean integration-unit integration

help:
	@printf "Available targets:\n"
	@printf "  make configure      Configure the CMake build in %s\n" "$(BUILD_DIR)"
	@printf "  make configure SANITIZE=address BUILD_DIR=build-asan  Configure an AddressSanitizer tree (fresh BUILD_DIR)\n"
	@printf "  make setup          Create runtime directories and bootstrap files\n"
	@printf "  make build          Build the ageland server binary\n"
	@printf "  make test           Run the C++ unit tests\n"
	@printf "  make smoke-account  Build the game/proxy and run the account smoke flow\n"
	@printf "  make format         Run clang-format via the CMake target\n"
	@printf "  make run            Build and start the server in the foreground\n"
	@printf "  make clean          Clean the configured CMake build tree\n"
	@printf "  make integration-unit  Run the harness unit tests (no server)\n"
	@printf "  make integration       Run the integration harness against a booted server\n"

$(CMAKE_CACHE):
	$(CMAKE) -S $(SRC_DIR) -B $(BUILD_DIR) $(CMAKE_CONFIGURE_ARGS) -DROTS_SANITIZE=$(SANITIZE)

configure: $(CMAKE_CACHE)

setup: $(CMAKE_CACHE)
	+$(CMAKE) --build $(BUILD_DIR) --target setup

build: $(CMAKE_CACHE)
	+$(CMAKE) --build $(BUILD_DIR) --target ageland -j16

test: $(CMAKE_CACHE)
	+$(CMAKE) --build $(BUILD_DIR) --target ageland ageland_tests -j16
	ctest --test-dir $(BUILD_DIR) --output-on-failure

run: build
	./bin/ageland -p 3791

smoke-account: setup build
	cargo build -p proxy
	python3 tools/account_smoke.py

format: $(CMAKE_CACHE)
	+$(CMAKE) --build $(BUILD_DIR) --target format

clean:
	@if [ ! -f "$(CMAKE_CACHE)" ]; then \
		printf "No configured CMake build tree found in %s\n" "$(BUILD_DIR)"; \
	else \
		$(CMAKE) --build $(BUILD_DIR) --target clean; \
	fi

integration-unit:
	$(PYTHON) -m pytest tests/integration/unit -q

integration:
	$(PYTHON) -m pytest tests/integration -q
