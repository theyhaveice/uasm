#pragma once

#include <string>
#include <vector>

namespace uasm {

bool platformDirectoryExists(const std::string& path);
void platformListFilesIn(const std::string& dir, std::vector<std::string>& out);
void platformListFilesRecursive(const std::string& root, std::vector<std::string>& out);

}
