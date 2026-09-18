#include "../glob_platform.h"

#if __cplusplus >= 201703L

#include <filesystem>

namespace uasm {

bool platformDirectoryExists(const std::string& path) {
    return std::filesystem::exists(path) && std::filesystem::is_directory(path);
}

void platformListFilesIn(const std::string& dir, std::vector<std::string>& out) {
    for (std::filesystem::directory_iterator it(dir), end; it != end; ++it) {
        if (it->is_regular_file()) out.push_back(it->path().string());
    }
}

void platformListFilesRecursive(const std::string& root, std::vector<std::string>& out) {
    for (std::filesystem::recursive_directory_iterator it(root), end; it != end; ++it) {
        if (it->is_regular_file()) out.push_back(it->path().string());
    }
}

}

#endif
