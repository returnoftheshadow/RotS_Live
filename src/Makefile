# Plain-make build of the ageland server, for hosts without CMake and for
# scripts/rots-docker.sh. The CMake build driven by the root Makefile is the primary one.
# SOURCES must list the same files as ROTS_SERVER_SOURCES in CMakeLists.txt; the order is
# the link order.

MAKEFLAGS += --no-builtin-rules
.SUFFIXES:
.DELETE_ON_ERROR:

BIN_DIR ?= ../bin
AGELAND := $(BIN_DIR)/ageland

# The live server is 32-bit: its binary player and object files depend on the i386 layout.
# These flags define the build; CXXFLAGS and LDFLAGS stay free for callers to extend.
ROTS_CXXFLAGS := -std=c++17 -m32 -w -fstrict-aliasing -funsigned-char
# -rdynamic exports the server's symbols so backtrace_symbols() can name crash frames.
ROTS_LDFLAGS := -m32 -rdynamic
CXXFLAGS ?= -g
LDFLAGS ?= -g
LDLIBS += -lcrypt
# Each compile writes a .d file listing the headers (and #included .cpp fragments) it read.
DEPFLAGS = -MMD -MP

SOURCES := \
    act_comm.cpp \
    act_info.cpp \
    act_move.cpp \
    act_obj1.cpp \
    act_obj2.cpp \
    act_offe.cpp \
    act_othe.cpp \
    act_soci.cpp \
    act_wiz.cpp \
    ban.cpp \
    battle_mage_handler.cpp \
    big_brother.cpp \
    boards.cpp \
    char_utils.cpp \
    char_utils_combat.cpp \
    clerics.cpp \
    clock.cpp \
    color.cpp \
    combat_manager.cpp \
    comm.cpp \
    config.cpp \
    consts.cpp \
    crashsave_schedule.cpp \
    db.cpp \
    delayed_command_interpreter.cpp \
    fight.cpp \
    graph.cpp \
    handler.cpp \
    interpre.cpp \
    environment_utils.cpp \
    account_management.cpp \
    account_cache.cpp \
    account_ppc.cpp \
    account_index.cpp \
    account_errors.cpp \
    roster_cache.cpp \
    character_json.cpp \
    exploits_json.cpp \
    json_utils.cpp \
    objects_json.cpp \
    limits.cpp \
    mail.cpp \
    mystic.cpp \
    mage.cpp \
    mobact.cpp \
    modify.cpp \
    mudlle.cpp \
    mudlle2.cpp \
    mob_csv_extract.cpp \
    mob_options.cpp \
    mob_progs/shopkeeper.cpp \
    game_boot_options.cpp \
    obj2html.cpp \
    object_utils.cpp \
    objsave.cpp \
    olog_hai.cpp \
    pkill.cpp \
    player_file_finalize.cpp \
    profs.cpp \
    protocol.cpp \
    ranger.cpp \
    save_benchmark.cpp \
    savebench.cpp \
    script.cpp \
    shapemdl.cpp \
    shapemob.cpp \
    shapeobj.cpp \
    shaperom.cpp \
    shapezon.cpp \
    shapescript.cpp \
    shop.cpp \
    signals.cpp \
    skill_timer.cpp \
    spec_ass.cpp \
    spec_pro.cpp \
    spell_pa.cpp \
    utility.cpp \
    wait_functions.cpp \
    weapon_master_handler.cpp \
    wild_fighting_handler.cpp \
    weather.cpp \
    zone.cpp

OBJFILES := $(SOURCES:.cpp=.o)
DEPFILES := $(OBJFILES:.o=.d)

RUNTIME_BUCKETS := A-E F-J K-O P-T U-Z ZZZ
RUNTIME_STORES := players accounts account_characters plrobjs exploits
RUNTIME_DIRS := $(BIN_DIR) ../log ../lib/misc ../mobs \
    $(foreach store,$(RUNTIME_STORES),$(addprefix ../lib/$(store)/,$(RUNTIME_BUCKETS)))

.DEFAULT_GOAL := all
.PHONY: all ageland clean format test setup run

all: ageland

ageland: $(AGELAND)

# The previous binary is renamed, not overwritten, because a running server still maps it.
$(AGELAND): $(OBJFILES)
	@mkdir -p $(@D)
	@if [ -f $@ ]; then mv -f $@ $@~; fi
	$(CXX) $(ROTS_LDFLAGS) $(LDFLAGS) -o $@ $(OBJFILES) $(LDLIBS)

%.o: %.cpp
	$(CXX) $(ROTS_CXXFLAGS) $(CPPFLAGS) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

-include $(DEPFILES)

clean:
	$(RM) $(OBJFILES) $(DEPFILES)

# See the warning above the root Makefile's format target before running this.
format:
	clang-format -i -style=WebKit *.cpp *.h

# The unit tests are built by CMake only.
test:
	$(MAKE) -C .. test

setup:
	@mkdir -p $(RUNTIME_DIRS)
	@touch ../log/lastdeath ../lib/last_cmds ../lib/misc/pklist

run: $(AGELAND) setup
	cd .. && $(abspath $(AGELAND)) -p 3791
