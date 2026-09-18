#include "uasm/glob.h"

#include "glob_platform.h"

#include <algorithm>
#include <stdexcept>

namespace uasm {

namespace {

bool matchSegment(const std::string& pattern, const std::string& name) {
    size_t p = 0, n = 0;
    size_t starP = std::string::npos, starN = 0;
    while (n < name.size()) {
        if (p < pattern.size() && pattern[p] == '*') {
            starP = p++;
            starN = n;
        } else if (p < pattern.size() && pattern[p] == name[n]) {
            ++p;
            ++n;
        } else if (starP != std::string::npos) {
            p = starP + 1;
            n = ++starN;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

bool hasRecursiveWildcard(const std::string& pattern) { return pattern.find("**") != std::string::npos; }

bool endsWithUasm(const std::string& path) {
    const std::string suffix = ".uasm";
    return path.size() >= suffix.size() && path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string baseName(const std::string& path) {
    std::string::size_type slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

}

std::vector<std::string> expandGlob(const std::string& pattern) {
    if (pattern.find('*') == std::string::npos) {
        std::vector<std::string> single;
        single.push_back(pattern);
        return single;
    }

    std::vector<std::string> candidates;
    std::string namePattern;

    if (hasRecursiveWildcard(pattern)) {
        size_t starPos = pattern.find("**");
        std::string beforeStar = pattern.substr(0, starPos);
        size_t lastSlash = beforeStar.find_last_of('/');
        std::string root = (lastSlash == std::string::npos) ? "." : beforeStar.substr(0, lastSlash);
        std::string filePattern = pattern.substr(lastSlash == std::string::npos ? 0 : lastSlash + 1);

        for (size_t i = 0; i < filePattern.size();) {
            if (filePattern.compare(i, 2, "**") == 0) {
                namePattern += '*';
                i += 2;
            } else {
                namePattern += filePattern[i++];
            }
        }
        if (root.empty()) root = ".";
        if (!platformDirectoryExists(root)) {
            throw std::runtime_error("glob pattern '" + pattern + "' matches nothing (no such directory '" + root + "')");
        }
        platformListFilesRecursive(root, candidates);
    } else {
        size_t lastSlash = pattern.find_last_of('/');
        std::string dir = (lastSlash == std::string::npos) ? "." : pattern.substr(0, lastSlash);
        namePattern = pattern.substr(lastSlash == std::string::npos ? 0 : lastSlash + 1);
        if (dir.empty()) dir = "/";
        if (!platformDirectoryExists(dir)) {
            throw std::runtime_error("glob pattern '" + pattern + "' matches nothing (no such directory '" + dir + "')");
        }
        platformListFilesIn(dir, candidates);
    }

    std::vector<std::string> results;
    for (size_t i = 0; i < candidates.size(); ++i) {
        if (!endsWithUasm(candidates[i])) continue;
        if (matchSegment(namePattern, baseName(candidates[i]))) results.push_back(candidates[i]);
    }

    if (results.empty()) {
        throw std::runtime_error("glob pattern '" + pattern + "' matched no .uasm files");
    }
    std::sort(results.begin(), results.end());
    results.erase(std::unique(results.begin(), results.end()), results.end());
    return results;
}

}
