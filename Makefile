MAKEFLAGS += --no-builtin-rules
.SUFFIXES:

BUILD_DIR ?= build
SRC_DIR := src
CMAKE ?= cmake
CTEST ?= ctest
CMAKE_CONFIGURE_ARGS ?= -DCMAKE_CXX_COMPILER=g++
CMAKE_CACHE := $(BUILD_DIR)/CMakeCache.txt
# Parallel build jobs; rotsmud's two shared cores want JOBS=1.
JOBS ?= $(shell getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)
# Extra ctest arguments, e.g. CTEST_ARGS='-R JsonUtils'.
CTEST_ARGS ?=
# Python used for the integration harness; point it at a virtual environment's python.
PYTHON ?= python3
# Scenario group for make integration, e.g. GROUP=boot; empty runs every scenario.
GROUP ?=

.DEFAULT_GOAL := help
.PHONY: help configure setup build test run smoke-account integration-unit integration format clean

help:
	@printf "Available targets:\n"
	@printf "  make configure         Configure the CMake build in %s\n" "$(BUILD_DIR)"
	@printf "  make setup             Create runtime directories and bootstrap files\n"
	@printf "  make build             Build the ageland server binary\n"
	@printf "  make test              Run the C++ unit tests\n"
	@printf "  make smoke-account     Build the game/proxy and run the account smoke flow\n"
	@printf "  make integration-unit  Run the integration harness's own unit tests (no server)\n"
	@printf "  make integration       Run the integration scenarios (GROUP=<name> for one group)\n"
	@printf "  make format            clang-format the WHOLE tree -- see the warning above the target;\n"
	@printf "                         format only your changed files instead\n"
	@printf "  make run               Build and start the server in the foreground\n"
	@printf "  make clean             Clean the configured CMake build tree\n"
	@printf "\nVariables: BUILD_DIR (%s), JOBS (%s), CMAKE_CONFIGURE_ARGS, CTEST_ARGS, PYTHON, GROUP\n" "$(BUILD_DIR)" "$(JOBS)"
	@printf "Changing CMAKE_CONFIGURE_ARGS needs a fresh BUILD_DIR; an existing tree keeps its cache.\n"

$(CMAKE_CACHE):
	$(CMAKE) -S $(SRC_DIR) -B $(BUILD_DIR) $(CMAKE_CONFIGURE_ARGS)

configure: $(CMAKE_CACHE)

setup: $(CMAKE_CACHE)
	+$(CMAKE) --build $(BUILD_DIR) --target setup

build: $(CMAKE_CACHE)
	+$(CMAKE) --build $(BUILD_DIR) --target ageland --parallel $(JOBS)

# ctest runs from inside the tree because --test-dir needs CMake 3.20 and the README supports 3.18.
test: $(CMAKE_CACHE)
	+$(CMAKE) --build $(BUILD_DIR) --target ageland ageland_tests --parallel $(JOBS)
	cd $(BUILD_DIR) && $(CTEST) --output-on-failure $(CTEST_ARGS)

run: build
	./bin/ageland -p 3791

smoke-account: setup build
	cargo build -p proxy
	python3 tools/account_smoke.py

integration-unit:
	$(PYTHON) -m pytest tests/integration/unit -q

integration:
	$(PYTHON) -m pytest tests/integration/scenarios -q -m "scenario$(if $(GROUP), and ($(GROUP)))"

# DO NOT RUN THIS on a change you are about to commit. It is clang-format over the WHOLE
# tree: ~1200 lines nobody touched get rewritten (db.cpp and act_wiz.cpp worst), and the
# fragment #includes in account_management.cpp get reordered into something that does not
# compile. Format only what you changed:
#     cd src && clang-format -i -style=WebKit <files you changed>
# (-style=WebKit is passed on the command line here too, which overrides the repo-root
# .clang-format, so that file is effectively unused -- match WebKit.)
format: $(CMAKE_CACHE)
	+$(CMAKE) --build $(BUILD_DIR) --target format

clean:
	@if [ ! -f "$(CMAKE_CACHE)" ]; then \
		printf "No configured CMake build tree found in %s\n" "$(BUILD_DIR)"; \
	else \
		$(CMAKE) --build $(BUILD_DIR) --target clean; \
	fi
