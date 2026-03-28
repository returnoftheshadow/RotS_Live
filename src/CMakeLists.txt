cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED CMAKE_CXX_COMPILER)
    find_program(ROTS_GXX NAMES g++)
    if(ROTS_GXX)
        set(CMAKE_CXX_COMPILER "${ROTS_GXX}" CACHE FILEPATH "C++ compiler" FORCE)
    endif()
endif()

project(RotS LANGUAGES CXX)

include(CTest)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(BIN_DIR "${REPO_ROOT}/bin")
set(LOG_DIR "${REPO_ROOT}/log")
set(LIB_DIR "${REPO_ROOT}/lib")

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
    clock.cpp
    color.cpp
    combat_manager.cpp
    comm.cpp
    config.cpp
    consts.cpp
    db.cpp
    delayed_command_interpreter.cpp
    environment_utils.cpp
    fight.cpp
    graph.cpp
    handler.cpp
    interpre.cpp
    limits.cpp
    mage.cpp
    mail.cpp
    mob_csv_extract.cpp
    mobact.cpp
    modify.cpp
    mudlle.cpp
    mudlle2.cpp
    mystic.cpp
    obj2html.cpp
    object_utils.cpp
    objsave.cpp
    olog_hai.cpp
    pkill.cpp
    profs.cpp
    protocol.cpp
    ranger.cpp
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
    zone.cpp
)

set(ROTS_TEST_SOURCES
    tests/battle_mage_handler_tests.cpp
    tests/account_management_tests.cpp
    tests/CharPlayerDataBuilder.cpp
    tests/char_utils_tests.cpp
    tests/character_json_tests.cpp
    tests/damage_tests.cpp
    tests/db_loader_tests.cpp
    tests/exploits_json_tests.cpp
    tests/fight_proc_tests.cpp
    tests/mage_tests.cpp
    tests/ObjFlagDataBuilder.cpp
    tests/json_utils_tests.cpp
    tests/objects_json_tests.cpp
    tests/gtest_main.cpp
    tests/olog_hai_tests.cpp
    tests/obj_flag_data_tests.cpp
    tests/object_utils_tests.cpp
    tests/test_random_utils.cpp
    tests/test_random_utils_tests.cpp
    tests/weapon_master_handler_tests.cpp
)

add_executable(ageland ${ROTS_SERVER_SOURCES})
target_compile_options(ageland PRIVATE
    -m32
    -w
    -fstrict-aliasing
    -funsigned-char
    -g
    -rdynamic
)
target_link_options(ageland PRIVATE -m32 -g -rdynamic)
set_target_properties(ageland PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${BIN_DIR}"
)
target_link_libraries(ageland PRIVATE crypt)

find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(ageland_tests ${ROTS_SERVER_SOURCES} ${ROTS_TEST_SOURCES})
target_include_directories(ageland_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests"
)
target_compile_definitions(ageland_tests PRIVATE TESTING)
target_compile_options(ageland_tests PRIVATE
    -Wall
    -Wextra
)
target_link_libraries(ageland_tests PRIVATE GTest::gtest)
target_link_libraries(ageland_tests PRIVATE crypt)
target_link_options(ageland_tests PRIVATE
    -Wl,--wrap=_Z6numberv
    -Wl,--wrap=_Z6numberii
)
set_target_properties(ageland_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${BIN_DIR}"
    OUTPUT_NAME tests
)

if(BUILD_TESTING)
    gtest_discover_tests(ageland_tests
        DISCOVERY_MODE PRE_TEST
        WORKING_DIRECTORY "${REPO_ROOT}"
    )
endif()

add_custom_target(setup
    COMMAND "${CMAKE_COMMAND}" -E make_directory
        "${BIN_DIR}"
        "${LOG_DIR}"
        "${LIB_DIR}/misc"
        "${LIB_DIR}/players/A-E"
        "${LIB_DIR}/players/F-J"
        "${LIB_DIR}/players/K-O"
        "${LIB_DIR}/players/P-T"
        "${LIB_DIR}/players/U-Z"
        "${LIB_DIR}/players/ZZZ"
        "${LIB_DIR}/accounts/A-E"
        "${LIB_DIR}/accounts/F-J"
        "${LIB_DIR}/accounts/K-O"
        "${LIB_DIR}/accounts/P-T"
        "${LIB_DIR}/accounts/U-Z"
        "${LIB_DIR}/accounts/ZZZ"
        "${LIB_DIR}/account_characters/A-E"
        "${LIB_DIR}/account_characters/F-J"
        "${LIB_DIR}/account_characters/K-O"
        "${LIB_DIR}/account_characters/P-T"
        "${LIB_DIR}/account_characters/U-Z"
        "${LIB_DIR}/account_characters/ZZZ"
        "${LIB_DIR}/plrobjs/A-E"
        "${LIB_DIR}/plrobjs/F-J"
        "${LIB_DIR}/plrobjs/K-O"
        "${LIB_DIR}/plrobjs/P-T"
        "${LIB_DIR}/plrobjs/U-Z"
        "${LIB_DIR}/plrobjs/ZZZ"
        "${LIB_DIR}/exploits/A-E"
        "${LIB_DIR}/exploits/F-J"
        "${LIB_DIR}/exploits/K-O"
        "${LIB_DIR}/exploits/P-T"
        "${LIB_DIR}/exploits/U-Z"
        "${LIB_DIR}/exploits/ZZZ"
        "${REPO_ROOT}/mobs"
    COMMAND "${CMAKE_COMMAND}" -E touch
        "${LOG_DIR}/lastdeath"
        "${LIB_DIR}/last_cmds"
        "${LIB_DIR}/misc/pklist"
    COMMENT "Creating runtime directories and placeholder files"
)

find_program(CLANG_FORMAT_EXECUTABLE clang-format)
if(CLANG_FORMAT_EXECUTABLE)
    file(GLOB ROTS_FORMAT_FILES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    )
    add_custom_target(format
        COMMAND "${CLANG_FORMAT_EXECUTABLE}" -i -style=WebKit ${ROTS_FORMAT_FILES}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Formatting source files with clang-format"
    )
endif()

find_package(Python3 COMPONENTS Interpreter)
find_program(CARGO_EXECUTABLE cargo)
if(Python3_Interpreter_FOUND AND CARGO_EXECUTABLE)
    add_custom_target(smoke_account
        COMMAND "${CARGO_EXECUTABLE}" build -p proxy
        COMMAND "${Python3_EXECUTABLE}" "${REPO_ROOT}/tools/account_smoke.py"
        WORKING_DIRECTORY "${REPO_ROOT}"
        DEPENDS ageland setup
        COMMENT "Running the proxy-backed account smoke flow"
    )
endif()
