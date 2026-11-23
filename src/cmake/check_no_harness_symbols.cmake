# Fails when the symbol table of the program at BINARY, as NM -C lists it, has any line that
# names namespace harness, or has no line naming run_the_game: a listing without the server's
# entry point is empty or stripped and proves nothing.
# Run as: cmake -DNM=<nm> -DBINARY=<program> -P <this file>

if(NOT NM OR NOT BINARY)
    message(FATAL_ERROR "Pass -DNM=<nm program> and -DBINARY=<program to check>.")
endif()

# A function every build of the server holds.
set(REQUIRED_SYMBOL "run_the_game")

execute_process(
    COMMAND "${NM}" -C "${BINARY}"
    RESULT_VARIABLE nm_result
    OUTPUT_VARIABLE symbol_listing
    ERROR_VARIABLE nm_errors
)
if(NOT nm_result EQUAL 0)
    message(FATAL_ERROR "${NM} could not list the symbols of ${BINARY} (${nm_result}): ${nm_errors}")
endif()

string(FIND "${symbol_listing}" "${REQUIRED_SYMBOL}(" required_symbol_position)
if(required_symbol_position EQUAL -1)
    message(FATAL_ERROR
        "${BINARY} lists no ${REQUIRED_SYMBOL} symbol, so its listing cannot show what it holds. ${nm_errors}")
endif()

# Demangled names hold semicolons and square brackets, which CMake lists treat specially; replace
# them so each matching line counts as one list element.
string(REPLACE ";" "," symbol_listing "${symbol_listing}")
string(REPLACE "[" "(" symbol_listing "${symbol_listing}")
string(REPLACE "]" ")" symbol_listing "${symbol_listing}")
string(REGEX MATCHALL "[^\n]*harness::[^\n]*" harness_symbols "${symbol_listing}")
list(LENGTH harness_symbols harness_symbol_count)
if(harness_symbol_count GREATER 0)
    list(GET harness_symbols 0 first_harness_symbol)
    message(FATAL_ERROR
        "${BINARY} lists ${harness_symbol_count} harness:: symbols, the first: ${first_harness_symbol}")
endif()

message(STATUS "${BINARY} lists ${REQUIRED_SYMBOL} and no harness:: symbols.")
