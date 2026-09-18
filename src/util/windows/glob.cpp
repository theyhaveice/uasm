#include "../glob_platform.h"

#if __cplusplus < 201703L && defined(_WIN32) && !defined(__MINGW32__) && !defined(__MINGW64__)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace uasm {

bool platformDirectoryExists(const std::string& path) {
    DWORD attrs = ::GetFileAttributesA(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

void platformListFilesIn(const std::string& dir, std::vector<std::string>& out) {
    WIN32_FIND_DATAA data;
    std::string pattern = dir + "/*";
    HANDLE h = ::FindFirstFileA(pattern.c_str(), &data);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::string name = data.cFileName;
        if (name == "." || name == "..") continue;
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            out.push_back(dir + "/" + name);
        }
    } while (::FindNextFileA(h, &data));
    ::FindClose(h);
}

void platformListFilesRecursive(const std::string& root, std::vector<std::string>& out) {
    WIN32_FIND_DATAA data;
    std::string pattern = root + "/*";
    HANDLE h = ::FindFirstFileA(pattern.c_str(), &data);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::string name = data.cFileName;
        if (name == "." || name == "..") continue;
        std::string full = root + "/" + name;
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            platformListFilesRecursive(full, out);
        } else {
            out.push_back(full);
        }
    } while (::FindNextFileA(h, &data));
    ::FindClose(h);
}

}

#endif
