BUILD_DIR := build
SRC_DIR := src
CMAKE := cmake
CMAKE_CONFIGURE_ARGS ?= -DCMAKE_CXX_COMPILER=g++
CMAKE_CACHE := $(BUILD_DIR)/CMakeCache.txt

.PHONY: help configure setup build test run smoke-account format clean

help:
	@printf "Available targets:\n"
	@printf "  make configure  Configure the CMake build in %s\n" "$(BUILD_DIR)"
	@printf "  make setup      Create runtime directories and bootstrap files\n"
	@printf "  make build      Build the ageland server binary\n"
	@printf "  make test       Run the C++ unit tests\n"
	@printf "  make smoke-account  Build the game/proxy and run the account smoke flow\n"
	@printf "  make format     Run clang-format via the CMake target\n"
	@printf "  make run        Build and start the server in the background\n"
	@printf "  make clean      Clean the configured CMake build tree\n"

$(CMAKE_CACHE):
	$(CMAKE) -S $(SRC_DIR) -B $(BUILD_DIR) $(CMAKE_CONFIGURE_ARGS)

configure: $(CMAKE_CACHE)

setup: $(CMAKE_CACHE)
	+$(CMAKE) --build $(BUILD_DIR) --target setup

build: $(CMAKE_CACHE)
	+$(CMAKE) --build $(BUILD_DIR) --target ageland

test: $(CMAKE_CACHE)
	+$(CMAKE) --build $(BUILD_DIR) --target ageland ageland_tests
	ctest --test-dir $(BUILD_DIR) --output-on-failure

run: build
	./bin/ageland -p 3791 &

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
