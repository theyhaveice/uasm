#include "../glob_platform.h"

#if __cplusplus < 201703L && (!defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__))

#include <dirent.h>
#include <sys/stat.h>

namespace uasm {

bool platformDirectoryExists(const std::string& path) {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

void platformListFilesIn(const std::string& dir, std::vector<std::string>& out) {
    DIR* d = ::opendir(dir.c_str());
    if (!d) return;
    for (struct dirent* entry = ::readdir(d); entry != 0; entry = ::readdir(d)) {
        std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        std::string full = dir + "/" + name;
        struct stat st;
        if (::stat(full.c_str(), &st) == 0 && S_ISREG(st.st_mode)) out.push_back(full);
    }
    ::closedir(d);
}

void platformListFilesRecursive(const std::string& root, std::vector<std::string>& out) {
    DIR* d = ::opendir(root.c_str());
    if (!d) return;
    for (struct dirent* entry = ::readdir(d); entry != 0; entry = ::readdir(d)) {
        std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        std::string full = root + "/" + name;
        struct stat st;
        if (::stat(full.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            platformListFilesRecursive(full, out);
        } else if (S_ISREG(st.st_mode)) {
            out.push_back(full);
        }
    }
    ::closedir(d);
}

}

#endif
