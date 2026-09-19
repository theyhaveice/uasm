#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>
#endif

#include "uasm/codegen.h"
#include "uasm/disassemble.h"
#include "uasm/extensions.h"
#include "uasm/glob.h"
#include "uasm/interpreter.h"
#include "uasm/jit.h"
#include "uasm/linker.h"
#include "uasm/object.h"
#include "uasm/opcode_info.h"
#include "uasm/parser.h"
#include "uasm/serializer.h"

namespace {

void printUsage() {
    std::cerr << "usage:\n"
              << "  uasm compile <files.uasm...> -o <out.uo>\n"
              << "  uasm run <file.uo> [-j|--jit [n]] [-- <type>:<value> ...]\n"
              << "      e.g. uasm run file.uo -- i32:10 i32:32\n"
              << "      e.g. uasm run file.uo -j 4 -- i32:10 i32:32\n"
              << "  uasm dump [-f|--format uasm|json|yaml] <file.uo>\n"
              << "  uasm build <file.uo> --<target> -o <output>\n"
              << "      targets: --macos-arm64 (others not yet implemented)\n"
              << "\n"
              << "extensions (compile):\n"
              << "  --enable-<ext>    enable one instruction-set extension\n"
              << "  --disable-<ext>   disable one extension\n"
              << "  --enable-all      enable every extension\n"
              << "  --print-extensions  list extensions and their opcode counts\n";
}

int printExtensions() {
    std::cout << "extension    opcodes  default\n";
    for (unsigned i = 0; i < static_cast<unsigned>(uasm::Extension::ExtensionCount); ++i) {
        uasm::Extension::Value e = static_cast<uasm::Extension::Value>(i);
        std::string name = uasm::extensionName(e);
        std::cout << name;
        for (size_t pad = name.size(); pad < 13; ++pad) std::cout << ' ';
        unsigned n = uasm::extensionOpcodeCount(e);
        std::cout << n;
        std::string ns;
        {
            unsigned v = n;
            do { ns.insert(ns.begin(), static_cast<char>('0' + (v % 10))); v /= 10; } while (v > 0);
        }
        for (size_t pad = ns.size(); pad < 9; ++pad) std::cout << ' ';
        std::cout << (e == uasm::Extension::Core ? "on" : "off") << "\n";
    }
    return 0;
}

bool parseExtensionFlag(const std::string& arg, uasm::ExtensionSet& set, std::string& error) {
    const std::string enablePrefix = "--enable-";
    const std::string disablePrefix = "--disable-";
    if (arg == "--enable-all") {
        set.enableAll();
        return true;
    }
    if (arg.size() > enablePrefix.size() && arg.compare(0, enablePrefix.size(), enablePrefix) == 0) {
        std::string name = arg.substr(enablePrefix.size());
        uasm::Extension::Value e;
        if (!uasm::extensionFromName(name, e)) {
            error = "unknown extension '" + name + "'";
            return false;
        }
        set.enable(e);
        return true;
    }
    if (arg.size() > disablePrefix.size() && arg.compare(0, disablePrefix.size(), disablePrefix) == 0) {
        std::string name = arg.substr(disablePrefix.size());
        uasm::Extension::Value e;
        if (!uasm::extensionFromName(name, e)) {
            error = "unknown extension '" + name + "'";
            return false;
        }
        set.disable(e);
        return true;
    }
    error.clear();
    return false;
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
    uasm::ExtensionSet enabled;

    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "-o") {
            if (i + 1 >= args.size()) {
                std::cerr << "error: -o requires an argument\n";
                return 1;
            }
            output = args[++i];
        } else if (args[i] == "--print-extensions") {
            return printExtensions();
        } else {
            std::string error;
            if (parseExtensionFlag(args[i], enabled, error)) continue;
            if (!error.empty()) {
                std::cerr << "error: " << error << "\n";
                return 1;
            }
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
            objects.push_back(uasm::parseModule(source, inputFiles[i], enabled));
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
    bool jit = false;
    int jitThreads = 1;

    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (!afterSeparator && arg == "--") {
            afterSeparator = true;
        } else if (afterSeparator) {
            argSpecs.push_back(arg);
        } else if (!afterSeparator && (arg == "-j" || arg == "--jit")) {
            jit = true;
            if (i + 1 < args.size()) {
                char* end = 0;
                long n = std::strtol(args[i + 1].c_str(), &end, 10);
                if (end != args[i + 1].c_str() && *end == '\0' && n > 0) {
                    jitThreads = static_cast<int>(n);
                    ++i;
                }
            }
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
        uasm::Value result = jit ? uasm::runJit(program, programArgs, jitThreads) : uasm::run(program, programArgs);
        if (result.type == uasm::Type::I32) return result.bits.i32;
        return 0;
    } catch (const uasm::RuntimeError& e) {
        std::cerr << "runtime error: " << e.message << "\n";
        return 1;
    } catch (const uasm::CodegenError& e) {
        std::cerr << "jit error: " << e.what() << "\n";
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

bool parseTargetFlag(const std::string& flag, uasm::CodegenTarget& target) {
    if (flag == "--macos-arm64") {
        target = uasm::CodegenTarget(uasm::TargetArch::Arm64, uasm::TargetOs::MacOS);
        return true;
    }
    if (flag == "--macos-x86_64") { target = uasm::CodegenTarget(uasm::TargetArch::X86_64, uasm::TargetOs::MacOS); return true; }
    if (flag == "--windows-x86") { target = uasm::CodegenTarget(uasm::TargetArch::X86, uasm::TargetOs::Windows); return true; }
    if (flag == "--windows-x86_64") { target = uasm::CodegenTarget(uasm::TargetArch::X86_64, uasm::TargetOs::Windows); return true; }
    if (flag == "--windows-arm32") { target = uasm::CodegenTarget(uasm::TargetArch::Arm32, uasm::TargetOs::Windows); return true; }
    if (flag == "--windows-arm64") { target = uasm::CodegenTarget(uasm::TargetArch::Arm64, uasm::TargetOs::Windows); return true; }
    if (flag == "--linux-x86") { target = uasm::CodegenTarget(uasm::TargetArch::X86, uasm::TargetOs::Linux); return true; }
    if (flag == "--linux-x86_64") { target = uasm::CodegenTarget(uasm::TargetArch::X86_64, uasm::TargetOs::Linux); return true; }
    if (flag == "--linux-arm32") { target = uasm::CodegenTarget(uasm::TargetArch::Arm32, uasm::TargetOs::Linux); return true; }
    if (flag == "--linux-arm64") { target = uasm::CodegenTarget(uasm::TargetArch::Arm64, uasm::TargetOs::Linux); return true; }
    return false;
}

int runBuild(const std::vector<std::string>& args) {
    std::string input;
    std::string output;
    bool haveTarget = false;
    uasm::CodegenTarget target;

    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "-o") {
            if (i + 1 >= args.size()) {
                std::cerr << "error: -o requires an argument\n";
                return 1;
            }
            output = args[++i];
        } else if (args[i].size() > 2 && args[i][0] == '-' && args[i][1] == '-') {
            if (haveTarget) {
                std::cerr << "error: only one target flag may be given\n";
                return 1;
            }
            if (!parseTargetFlag(args[i], target)) {
                std::cerr << "error: unknown target '" << args[i] << "'\n";
                return 1;
            }
            haveTarget = true;
        } else if (input.empty()) {
            input = args[i];
        } else {
            printUsage();
            return 1;
        }
    }

    if (input.empty() || output.empty() || !haveTarget) {
        printUsage();
        return 1;
    }

    if (!uasm::isCodegenSupported(target)) {
        std::cerr << "error: no native codegen backend for target '" << uasm::targetName(target)
                   << "' yet (v0.4 only supports macos-arm64)\n";
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
        uasm::CompiledCode code = uasm::compileProgram(program, target);
        std::vector<uint8_t> exe = uasm::wrapExecutable(target, code);

        std::ofstream out(output.c_str(), std::ios::binary);
        if (!out) {
            std::cerr << "error: could not open '" << output << "' for writing\n";
            return 1;
        }
        out.write(reinterpret_cast<const char*>(&exe[0]), static_cast<std::streamsize>(exe.size()));
        out.close();

#ifndef _WIN32
        if (uasm::executableNeedsExecBit(target.os)) {
            ::chmod(output.c_str(), 0755);
        }
#endif
    } catch (const uasm::CodegenError& e) {
        std::cerr << "build error: " << e.what() << "\n";
        return 1;
    }

    std::cout << "built " << uasm::targetName(target) << " executable -> " << output << "\n";
    if (target.os == uasm::TargetOs::MacOS && target.arch == uasm::TargetArch::Arm64) {
        std::cerr << "note: unsigned arm64 macOS binaries generally will not run on Apple Silicon\n"
                   << "hardware without at least an ad-hoc code signature; this build does not\n"
                   << "sign the output (see spec/ISA.md).\n";
    }
    return 0;
}

}

int main(int argc, char** argv) {
    uasm::setProcessArgs(argc, argv);

    if (argc < 2) {
        printUsage();
        return 1;
    }

    std::string command = argv[1];
    std::vector<std::string> rest(argv + 2, argv + argc);

    if (command == "--print-extensions" || command == "--list-extensions") return printExtensions();
    if (command == "compile") return runCompile(rest);
    if (command == "run") return runRun(rest);
    if (command == "dump") return runDump(rest);
    if (command == "build") return runBuild(rest);

    std::cerr << "error: unknown command '" << command << "'\n";
    printUsage();
    return 1;
}
