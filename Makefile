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
# make integration's pytest-xdist workers: JOBS when it is set on the command line or in the
# environment, otherwise 1, so the build default above never makes a scenario run parallel.
INTEGRATION_JOBS = $(if $(filter file,$(origin JOBS)),1,$(JOBS))
# Extra ctest arguments, e.g. CTEST_ARGS='-R JsonUtils'.
CTEST_ARGS ?=
# Python used for the integration harness; point it at a virtual environment's python.
PYTHON ?= python3
# Scenario group for make integration, e.g. GROUP=boot; empty runs every scenario.
GROUP ?=
# Groups make integration leaves out, separated by spaces or commas, e.g. SKIP_GROUPS="poison summon".
SKIP_GROUPS ?=
# CONFIRM=yes starts make integration without asking, however long its estimate.
CONFIRM ?=
# The folder of downloaded CI results make integration-shard-durations reads, e.g. RESULTS=build/ci-results.
RESULTS ?=
# make world-catalogue prints only this test-world zone, e.g. ZONE=13; empty prints every zone.
ZONE ?=
# make world-catalogue prints only this table: zones, rooms, mobs, objects or scripts.
CATEGORY ?=
# The branch make integration-changed measures changes from, e.g. BASE=origin/master. Only a
# command-line BASE counts: an exported BASE is too generic a name to trust. $(value ...) keeps
# make from expanding anything inside it.
ifeq ($(origin BASE),command line)
INTEGRATION_CHANGED_BASE := $(value BASE)
else
INTEGRATION_CHANGED_BASE := origin/release-frodo-next
endif

.DEFAULT_GOAL := help
.PHONY: help configure setup build test run smoke-account integration-unit integration world-catalogue format clean
.PHONY: integration-build integration-changed integration-plan integration-shard-durations

help:
	@printf "Available targets:\n"
	@printf "  make configure         Configure the CMake build in %s\n" "$(BUILD_DIR)"
	@printf "  make setup             Create runtime directories and bootstrap files\n"
	@printf "  make build             Build the ageland server binary\n"
	@printf "  make test              Run the C++ unit tests\n"
	@printf "  make smoke-account     Build the game/proxy and run the account smoke flow\n"
	@printf "  make integration-unit  Run the integration harness's own unit tests (no server)\n"
	@printf "  make integration       Show the planned scenario groups, ask, then run them (GROUP=<name>\n"
	@printf "                         for one group, SKIP_GROUPS=<names> to leave groups out,\n"
	@printf "                         JOBS=<n> for n parallel workers, CONFIRM=yes to skip the question)\n"
	@printf "  make integration-plan  Only show that plan\n"
	@printf "  make world-catalogue   Print the integration test world's zones, rooms, mobs, objects\n"
	@printf "                         and scripts (ZONE=<n>, CATEGORY=<table> to narrow it)\n"
	@printf "  make integration-build Build the server, harness server and rotstool the scenarios run\n"
	@printf "  make integration-changed  Run the scenario groups this checkout's changes reach\n"
	@printf "                         (BASE=<branch>, default origin/release-frodo-next)\n"
	@printf "  make integration-shard-durations  Refresh the times CI's shards are balanced by\n"
	@printf "                         from downloaded CI results (RESULTS=<folder>)\n"
	@printf "  make format            clang-format the WHOLE tree -- see the warning above the target;\n"
	@printf "                         format only your changed files instead\n"
	@printf "  make run               Build and start the server in the foreground\n"
	@printf "  make clean             Clean the configured CMake build tree\n"
	@printf "\nVariables: BUILD_DIR (%s), JOBS (%s), CMAKE_CONFIGURE_ARGS, CTEST_ARGS, PYTHON, GROUP, SKIP_GROUPS, CONFIRM, ZONE, CATEGORY, BASE, RESULTS\n" "$(BUILD_DIR)" "$(JOBS)"
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

# Shows the planned groups and asks about them (rots_harness/run_plan.py), then runs pytest, which
# writes each test's result and time to build/integration/results.xml and lists the slowest tests.
# GROUP, SKIP_GROUPS and CONFIRM reach Python through the environment, so the shell never reads a
# GROUP expression as command text. Every value is passed as --name=value, so one starting with -
# is not read as an option.
integration integration-plan: export ROTS_IT_RUN_GROUP := $(GROUP)
integration integration-plan: export ROTS_IT_RUN_SKIP_GROUPS := $(SKIP_GROUPS)
integration integration-plan: export ROTS_IT_RUN_CONFIRM := $(CONFIRM)
integration:
	PYTHONPATH="tests/integration$${PYTHONPATH:+:$$PYTHONPATH}" $(PYTHON) -m rots_harness.run_plan --python="$(PYTHON)" --jobs="$(INTEGRATION_JOBS)" --group="$$ROTS_IT_RUN_GROUP" --skip-groups="$$ROTS_IT_RUN_SKIP_GROUPS" --confirm="$$ROTS_IT_RUN_CONFIRM"

integration-plan:
	PYTHONPATH="tests/integration$${PYTHONPATH:+:$$PYTHONPATH}" $(PYTHON) -m rots_harness.run_plan --python="$(PYTHON)" --jobs="$(INTEGRATION_JOBS)" --group="$$ROTS_IT_RUN_GROUP" --skip-groups="$$ROTS_IT_RUN_SKIP_GROUPS" --plan-only

# PYTHONPATH rather than cd, so a relative PYTHON such as .venv/bin/python still resolves.
world-catalogue:
	PYTHONPATH="tests/integration$${PYTHONPATH:+:$$PYTHONPATH}" $(PYTHON) -m rots_harness.world_catalogue$(if $(ZONE), --zone $(ZONE))$(if $(CATEGORY), --category $(CATEGORY))

# The programs the scenarios run, built as CI builds them; make build builds only the server.
integration-build: $(CMAKE_CACHE)
	+$(CMAKE) --build $(BUILD_DIR) --target ageland ageland_harness rotstool --parallel $(JOBS)

# Prints why each group was chosen, then runs make integration with them. The base reaches
# Python through the environment, so the shell never reads it as command text.
integration-changed: export INTEGRATION_CHANGED_BASE := $(INTEGRATION_CHANGED_BASE)
integration-changed:
	PYTHONPATH="tests/integration$${PYTHONPATH:+:$$PYTHONPATH}" $(PYTHON) -m rots_harness.changed_groups --base="$$INTEGRATION_CHANGED_BASE" --python "$(PYTHON)" --make "$(MAKE)"

# Rewrites tests/integration/shard_durations.json from the results.xml files under RESULTS
# (rots_harness/shard_durations.py). The folder reaches Python through the environment, so the
# shell never reads it as command text.
integration-shard-durations: export ROTS_IT_SHARD_RESULTS := $(RESULTS)
integration-shard-durations:
	@test -n "$$ROTS_IT_SHARD_RESULTS" || { echo "make integration-shard-durations needs RESULTS=<folder of downloaded CI results>" >&2; exit 2; }
	PYTHONPATH="tests/integration$${PYTHONPATH:+:$$PYTHONPATH}" $(PYTHON) -m rots_harness.shard_durations -- "$$ROTS_IT_SHARD_RESULTS"

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
