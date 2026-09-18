#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#include "uasm/disassemble.h"
#include "uasm/glob.h"
#include "uasm/interpreter.h"
#include "uasm/linker.h"
#include "uasm/object.h"
#include "uasm/parser.h"
#include "uasm/serializer.h"

namespace {

void printUsage() {
    std::cerr << "usage:\n"
              << "  uasm compile <files.uasm...> -o <out.uo>\n"
              << "  uasm run <file.uo> [-- <type>:<value> ...]\n"
              << "      e.g. uasm run file.uo -- i32:10 i32:32\n"
              << "  uasm dump [-f|--format uasm|json|yaml] <file.uo>\n";
}

std::string readFile(const std::string& path) {
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) throw std::runtime_error("could not open '" + path + "'");
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

int runCompile(const std::vector<std::string>& args) {
    std::vector<std::string> inputPatterns;
    std::string output;

    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "-o") {
            if (i + 1 >= args.size()) {
                std::cerr << "error: -o requires an argument\n";
                return 1;
            }
            output = args[++i];
        } else {
            inputPatterns.push_back(args[i]);
        }
    }

    if (inputPatterns.empty() || output.empty()) {
        printUsage();
        return 1;
    }

    std::vector<std::string> inputFiles;
    try {
        for (size_t i = 0; i < inputPatterns.size(); ++i) {
            std::vector<std::string> matches = uasm::expandGlob(inputPatterns[i]);
            for (size_t j = 0; j < matches.size(); ++j) inputFiles.push_back(matches[j]);
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    std::sort(inputFiles.begin(), inputFiles.end());
    inputFiles.erase(std::unique(inputFiles.begin(), inputFiles.end()), inputFiles.end());

    std::vector<uasm::Object> objects;
    try {
        for (size_t i = 0; i < inputFiles.size(); ++i) {
            std::string source = readFile(inputFiles[i]);
            objects.push_back(uasm::parseModule(source, inputFiles[i]));
        }
    } catch (const uasm::ParseError& e) {
        std::cerr << "error: " << e.message << " (line " << e.line << ")\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }

    uasm::Program program;
    try {
        program = uasm::link(objects);
    } catch (const uasm::LinkError& e) {
        std::cerr << "link error: " << e.message << "\n";
        return 1;
    }

    try {
        uasm::writeUo(program, output);
    } catch (const uasm::SerializeError& e) {
        std::cerr << "error: " << e.message << "\n";
        return 1;
    }

    std::cout << "compiled " << inputFiles.size() << " file(s) -> " << output << "\n";
    return 0;
}

int runRun(const std::vector<std::string>& args) {
    std::string input;
    std::vector<std::string> argSpecs;
    bool afterSeparator = false;

    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (!afterSeparator && arg == "--") {
            afterSeparator = true;
        } else if (afterSeparator) {
            argSpecs.push_back(arg);
        } else if (input.empty()) {
            input = arg;
        } else {
            printUsage();
            return 1;
        }
    }

    if (input.empty()) {
        printUsage();
        return 1;
    }

    std::vector<uasm::Value> programArgs;
    try {
        for (size_t i = 0; i < argSpecs.size(); ++i) programArgs.push_back(uasm::parseTypedValue(argSpecs[i]));
    } catch (const uasm::InvalidTypedValue& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }

    uasm::Program program;
    try {
        program = uasm::readUo(input);
    } catch (const uasm::SerializeError& e) {
        std::cerr << "error: " << e.message << "\n";
        return 1;
    }

    try {
        uasm::Value result = uasm::run(program, programArgs);
        if (result.type == uasm::Type::I32) return result.bits.i32;
        return 0;
    } catch (const uasm::RuntimeError& e) {
        std::cerr << "runtime error: " << e.message << "\n";
        return 1;
    }
}

int runDump(const std::vector<std::string>& args) {
    std::string formatName = "uasm";
    std::string input;

    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "-f" || args[i] == "--format") {
            if (i + 1 >= args.size()) {
                std::cerr << "error: " << args[i] << " requires an argument\n";
                return 1;
            }
            formatName = args[++i];
        } else if (input.empty()) {
            input = args[i];
        } else {
            printUsage();
            return 1;
        }
    }

    if (input.empty()) {
        printUsage();
        return 1;
    }

    uasm::DumpFormat::Value format = uasm::DumpFormat::Uasm;
    try {
        format = uasm::dumpFormatFromName(formatName);
    } catch (const uasm::UnknownDumpFormat& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }

    uasm::Program program;
    try {
        program = uasm::readUo(input);
    } catch (const uasm::SerializeError& e) {
        std::cerr << "error: " << e.message << "\n";
        return 1;
    }

    std::cout << uasm::dump(program, format);
    return 0;
}

}

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage();
        return 1;
    }

    std::string command = argv[1];
    std::vector<std::string> rest(argv + 2, argv + argc);

    if (command == "compile") return runCompile(rest);
    if (command == "run") return runRun(rest);
    if (command == "dump") return runDump(rest);

    std::cerr << "error: unknown command '" << command << "'\n";
    printUsage();
    return 1;
}
