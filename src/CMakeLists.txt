# 3.18 is the oldest CMake in use (the i386 Debian bullseye image) and 3.28 the newest
# CI exercises; newer CMake applies policies only up to 3.28.
cmake_minimum_required(VERSION 3.18...3.28)

# The compiler is chosen by the caller (-DCMAKE_CXX_COMPILER=g++ in the root Makefile and
# README), never forced here, so a CXX or toolchain choice is not silently overridden.
project(RotS
    DESCRIPTION "Return of the Shadow MUD server"
    LANGUAGES CXX
)

include(CTest)

get_property(ROTS_IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT ROTS_IS_MULTI_CONFIG)
    if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
    endif()
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

# One project-wide standard rather than per-target compile features: this is an application
# with no downstream consumers, and it pins exactly -std=c++17 whatever the compiler's default.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ROTS_SUPPRESS_TEST_WARNINGS "Suppress compiler warnings when building the test target" ON)

include(CheckCXXSourceCompiles)
# A 64-bit host needs its 32-bit C++ toolchain and 32-bit libcrypt (g++-multilib and
# libcrypt-dev:i386, or the distribution's equivalents); without them the build would fail at
# the first compile or link with a less clear error.
set(CMAKE_REQUIRED_FLAGS -m32)
set(CMAKE_REQUIRED_LINK_OPTIONS -m32)
set(CMAKE_REQUIRED_LIBRARIES crypt)
check_cxx_source_compiles(
    "int main() { static_assert(sizeof(void*) == 4, \"32-bit\"); return 0; }"
    ROTS_HAS_32BIT_TOOLCHAIN
)
unset(CMAKE_REQUIRED_FLAGS)
unset(CMAKE_REQUIRED_LINK_OPTIONS)
unset(CMAKE_REQUIRED_LIBRARIES)
if(NOT ROTS_HAS_32BIT_TOOLCHAIN)
    # Forget the failed result so the next configure checks again.
    unset(ROTS_HAS_32BIT_TOOLCHAIN CACHE)
    message(FATAL_ERROR
        "RotS builds 32-bit (-m32), and this host cannot compile and link a 32-bit program "
        "against libcrypt. Install the 32-bit C++ toolchain and libcrypt, for example "
        "g++-multilib, libc6-dev-i386 and libcrypt-dev:i386, then configure a fresh build "
        "directory.")
endif()

# Settings that change what the compiled code does, not only how it is built. The live server
# is 32-bit with unsigned char, and its binary player and object files depend on that layout.
# Everything compiled from the server's sources links this target, so the tests exercise the
# code that ships. src/Makefile's ROTS_CXXFLAGS repeats these flags for the Makefile build;
# change both together.
add_library(rots_codegen INTERFACE)
target_compile_options(rots_codegen INTERFACE
    -m32
    -funsigned-char
    -fstrict-aliasing
)
target_link_options(rots_codegen INTERFACE -m32)

get_filename_component(ROTS_REPO_ROOT "${PROJECT_SOURCE_DIR}/.." ABSOLUTE)
# A second build tree (an ASan or scratch build) can point this elsewhere so it does not
# overwrite the primary tree's bin/ageland.
set(ROTS_BIN_DIR "${ROTS_REPO_ROOT}/bin" CACHE PATH "Output directory for the ageland and tests binaries")
set(ROTS_LOG_DIR "${ROTS_REPO_ROOT}/log")
set(ROTS_LIB_DIR "${ROTS_REPO_ROOT}/lib")

set(ROTS_SERVER_SOURCES
    act_comm.cpp
    act_info.cpp
    act_move.cpp
    act_obj1.cpp
    act_obj2.cpp
    act_offe.cpp
    act_othe.cpp
    act_soci.cpp
    act_wiz.cpp
    account_management.cpp
    account_cache.cpp
    account_ppc.cpp
    account_index.cpp
    account_errors.cpp
    roster_cache.cpp
    character_json.cpp
    exploits_json.cpp
    json_utils.cpp
    objects_json.cpp
    ban.cpp
    battle_mage_handler.cpp
    big_brother.cpp
    boards.cpp
    char_utils.cpp
    char_utils_combat.cpp
    clerics.cpp
    color.cpp
    combat_manager.cpp
    comm.cpp
    config.cpp
    consts.cpp
    crashsave_schedule.cpp
    damage_meters.cpp
    db.cpp
    delayed_command_interpreter.cpp
    environment_utils.cpp
    fight.cpp
    game_heartbeat.cpp
    game_time_text.cpp
    graph.cpp
    handler.cpp
    interpre.cpp
    lap_timer.cpp
    limits.cpp
    mage.cpp
    mail.cpp
    mob_csv_extract.cpp
    mob_options.cpp
    mob_progs/shopkeeper.cpp
    mob_progs/banker.cpp
    game_boot_options.cpp
    mobact.cpp
    modify.cpp
    mudlle.cpp
    mudlle2.cpp
    mystic.cpp
    number_text.cpp
    obj2html.cpp
    object_utils.cpp
    objsave.cpp
    olog_hai.cpp
    pkill.cpp
    player_file_finalize.cpp
    profs.cpp
    protocol.cpp
    ranger.cpp
    real_time_tick_schedule.cpp
    save_benchmark.cpp
    savebench.cpp
    script.cpp
    shapemdl.cpp
    shapemob.cpp
    shapeobj.cpp
    shaperom.cpp
    shapescript.cpp
    shapezon.cpp
    shop.cpp
    signals.cpp
    skill_timer.cpp
    spec_ass.cpp
    spec_pro.cpp
    spell_pa.cpp
    utility.cpp
    wait_functions.cpp
    weapon_master_handler.cpp
    weather.cpp
    wild_fighting_handler.cpp
    world_clock.cpp
    zone.cpp
)

set(ROTS_TEST_SOURCES
    tests/barter_vendor_tests.cpp
    tests/banker_tests.cpp
    tests/game_boot_options_tests.cpp
    tests/resistance_tests.cpp
    tests/resist_item_slot_tests.cpp
    tests/battle_mage_handler_tests.cpp
    tests/big_brother_tests.cpp
    tests/account_management_tests.cpp
    tests/account_cache_tests.cpp
    tests/account_index_tests.cpp
    tests/account_errors_tests.cpp
    tests/roster_cache_tests.cpp
    tests/account_ppc_tests.cpp
    tests/account_management_assets_header_compile.cpp
    tests/account_management_identity_header_compile.cpp
    tests/account_management_migration_header_compile.cpp
    tests/account_management_presentation_header_compile.cpp
    tests/account_management_storage_header_compile.cpp
    tests/account_management_types_header_compile.cpp
    tests/act_comm_tests.cpp
    tests/act_wiz_tests.cpp
    tests/CharPlayerDataBuilder.cpp
    tests/char_utils_tests.cpp
    tests/character_json_tests.cpp
    tests/color_tests.cpp
    tests/crashsave_schedule_tests.cpp
    tests/current_time_phase_tests.cpp
    tests/damage_meters_tests.cpp
    tests/damage_tests.cpp
    tests/db_loader_tests.cpp
    tests/exploit_record_tests.cpp
    tests/exploits_json_tests.cpp
    tests/interpre_account_menu_tests.cpp
    tests/intrusive_list_tests.cpp
    tests/room_lists_tests.cpp
    tests/fight_proc_tests.cpp
    tests/game_heartbeat_tests.cpp
    tests/game_time_text_tests.cpp
    tests/mage_tests.cpp
    tests/ObjFlagDataBuilder.cpp
    tests/json_utils_tests.cpp
    tests/kill_experience_tests.cpp
    tests/lap_timer_tests.cpp
    tests/limits_tests.cpp
    tests/room_light_tests.cpp
    tests/mob_options_tests.cpp
    tests/sprintbit_tests.cpp
    tests/script_flow_tests.cpp
    tests/script_command_tests.cpp
    tests/string_value_tests.cpp
    tests/argument_split_tests.cpp
    tests/zone_loader_tests.cpp
    tests/shape_section_tests.cpp
    tests/idle_follower_tests.cpp
    tests/room_contents_tests.cpp
    tests/negative_room_report_tests.cpp
    tests/process_output_tests.cpp
    tests/json_perf_tests.cpp
    tests/objects_json_tests.cpp
    tests/player_finalize_tests.cpp
    tests/protocol_tests.cpp
    tests/real_time_tick_schedule_tests.cpp
    tests/save_benchmark_tests.cpp
    tests/spell_pa_tests.cpp
    tests/startup_options_tests.cpp
    tests/gtest_main.cpp
    tests/olog_hai_tests.cpp
    tests/number_text_tests.cpp
    tests/obj_flag_data_tests.cpp
    tests/object_utils_tests.cpp
    tests/test_random_utils.cpp
    tests/test_random_utils_tests.cpp
    tests/weapon_master_handler_tests.cpp
    tests/world_clock_tests.cpp
    tests/load_scalp_tests.cpp
)

add_executable(ageland ${ROTS_SERVER_SOURCES})
target_compile_options(ageland PRIVATE
    -w
    -g
)
# -rdynamic exports the server's symbols so backtrace_symbols() can name crash frames.
target_link_options(ageland PRIVATE -g -rdynamic)
# A bare "crypt" lets the linker find the libcrypt that matches -m32; a find_library() path
# can resolve to the 64-bit library on a multilib host.
target_link_libraries(ageland PRIVATE rots_codegen crypt)
set_target_properties(ageland PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${ROTS_BIN_DIR}"
)

if(BUILD_TESTING)
    # Built from source with the server's settings, so GoogleTest and the tests agree on -m32 and
    # char signedness; a system GoogleTest on a 64-bit host is 64-bit. To build offline, set
    # FETCHCONTENT_SOURCE_DIR_GOOGLETEST to an unpacked copy of this release.
    include(FetchContent)
    FetchContent_Declare(googletest
        URL https://github.com/google/googletest/releases/download/v1.17.0/googletest-1.17.0.tar.gz
        URL_HASH SHA256=65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c
    )
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    target_link_libraries(gtest PRIVATE rots_codegen)
    target_link_libraries(gtest_main PRIVATE rots_codegen)
    # GoogleTest turns on its own warnings; its sources are not ours to fix, so keep test
    # output to warnings from this project.
    target_compile_options(gtest PRIVATE -w)
    target_compile_options(gtest_main PRIVATE -w)
    include(GoogleTest)

    add_executable(ageland_tests ${ROTS_SERVER_SOURCES} ${ROTS_TEST_SOURCES})
    target_include_directories(ageland_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests"
    )
    target_compile_definitions(ageland_tests PRIVATE TESTING)
    if(ROTS_SUPPRESS_TEST_WARNINGS)
        target_compile_options(ageland_tests PRIVATE -w)
    else()
        target_compile_options(ageland_tests PRIVATE
            -Wall
            -Wextra
        )
    endif()
    target_link_libraries(ageland_tests PRIVATE rots_codegen GTest::gtest crypt)
    # Routes both number() overloads through the seeded test RNG in tests/test_random_utils.cpp.
    target_link_options(ageland_tests PRIVATE
        -Wl,--wrap=_Z6numberv
        -Wl,--wrap=_Z6numberii
    )
    set_target_properties(ageland_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${ROTS_BIN_DIR}"
        OUTPUT_NAME tests
    )

    gtest_discover_tests(ageland_tests
        DISCOVERY_MODE PRE_TEST
        WORKING_DIRECTORY "${ROTS_REPO_ROOT}"
    )
endif()

set(ROTS_RUNTIME_DIRS
    "${ROTS_BIN_DIR}"
    "${ROTS_LOG_DIR}"
    "${ROTS_LIB_DIR}/misc"
    "${ROTS_REPO_ROOT}/mobs"
)
foreach(ROTS_STORE IN ITEMS players accounts account_characters plrobjs exploits)
    foreach(ROTS_BUCKET IN ITEMS A-E F-J K-O P-T U-Z ZZZ)
        list(APPEND ROTS_RUNTIME_DIRS "${ROTS_LIB_DIR}/${ROTS_STORE}/${ROTS_BUCKET}")
    endforeach()
endforeach()

add_custom_target(setup
    COMMAND "${CMAKE_COMMAND}" -E make_directory ${ROTS_RUNTIME_DIRS}
    COMMAND "${CMAKE_COMMAND}" -E touch
        "${ROTS_LOG_DIR}/lastdeath"
        "${ROTS_LIB_DIR}/last_cmds"
        "${ROTS_LIB_DIR}/misc/pklist"
    COMMENT "Creating runtime directories and placeholder files"
    VERBATIM
)

find_program(CLANG_FORMAT_EXECUTABLE clang-format)
if(CLANG_FORMAT_EXECUTABLE)
    file(GLOB ROTS_FORMAT_FILES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/intrusive/*.h"
    )
    add_custom_target(format
        COMMAND "${CLANG_FORMAT_EXECUTABLE}" -i -style=WebKit ${ROTS_FORMAT_FILES}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Formatting source files with clang-format"
        VERBATIM
    )
endif()

find_package(Python3 COMPONENTS Interpreter)
find_program(CARGO_EXECUTABLE cargo)
if(Python3_Interpreter_FOUND AND CARGO_EXECUTABLE)
    add_custom_target(smoke_account
        COMMAND "${CARGO_EXECUTABLE}" build -p proxy
        COMMAND "${Python3_EXECUTABLE}" "${ROTS_REPO_ROOT}/tools/account_smoke.py"
        WORKING_DIRECTORY "${ROTS_REPO_ROOT}"
        COMMENT "Running the proxy-backed account smoke flow"
        VERBATIM
    )
    add_dependencies(smoke_account ageland setup)
endif()
