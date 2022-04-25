#pragma once

#include <vector>

namespace Platform
{

PLATFORM_API bool ReadFileContent(LPCTSTR filename, std::vector<char>& data);
PLATFORM_API std::vector<std::tstring> ScanFiles(LPCTSTR folder, LPCTSTR mask);
PLATFORM_API std::vector<std::tstring> ScanDirectories(LPCTSTR folder, LPCTSTR fileToFind);

} // Platform
