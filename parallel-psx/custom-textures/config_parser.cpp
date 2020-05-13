#include "config_parser.h"
#include <iostream>
#include <fstream>
#include <regex>

int ignore_arg_to_number(std::string arg) {
    if (arg == "*") {
        return -1;
    } else {
        return atoi(arg.c_str());
    }
}

std::vector<PSX::RectMatch> parse_config_file(const char *path) {
    std::vector<PSX::RectMatch> result;
    // https://stackoverflow.com/questions/7868936/read-file-line-by-line-using-ifstream-in-c
    std::string line;
    std::ifstream in(path);
    std::regex ignore_command("^\\s*ignore\\s+(\\d+|\\*)\\s*,\\s*(\\d+|\\*)\\s*,\\s*(\\d+|\\*)\\s*,\\s*(\\d+|\\*)\\s*(?:#.*)?$");
    while (std::getline(in, line)) {
        std::smatch sm;
        if (std::regex_match(line, sm, ignore_command)) {
            result.push_back(PSX::RectMatch(
                ignore_arg_to_number(sm[1]),
                ignore_arg_to_number(sm[2]),
                ignore_arg_to_number(sm[3]),
                ignore_arg_to_number(sm[4])
            ));
        }
    }
    return result;
}