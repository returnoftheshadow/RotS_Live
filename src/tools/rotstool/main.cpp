#include "rotstool_command.h"

#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    std::vector<std::string> arguments;
    if (argc > 1) {
        arguments.reserve(static_cast<std::size_t>(argc - 1));
    }
    for (int argument_index = 1; argument_index < argc; ++argument_index) {
        arguments.emplace_back(argv[argument_index]);
    }
    return rotstool::run_rotstool(arguments, std::cout, std::cerr);
}
