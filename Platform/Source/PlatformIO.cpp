#include "stdafx.h"
#include "PlatformIO.h"

namespace Platform
{

bool ReadFileContent(LPCTSTR filename, std::vector<char>& data)
{
    DWORD error = NO_ERROR;
    HANDLE hFile = CreateFile(
        filename,
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    error = GetLastError();
    if (hFile != INVALID_HANDLE_VALUE)
    {
        // Only work with files less than 2Gb. We don't need larger
        DWORD size = GetFileSize(hFile, nullptr);
        error = GetLastError();
        if (error == NO_ERROR)
        {
            data.resize(size);

            DWORD readBytes = 0;
            ReadFile(hFile, data.data(), size, &readBytes, nullptr);
            error = GetLastError();
            if (readBytes != size)
            {
                OutputDebugString(_T("File "));
                OutputDebugString(filename);
                OutputDebugString(_T(" wrong number of bytes is read.\n"));
                if (error == NO_ERROR)
                {
                    error = ERROR_READ_FAULT; // We need to mirror error somehow, as we only expect the given number of bytes
                }
            }
        }

        CloseHandle(hFile);
        hFile = INVALID_HANDLE_VALUE;
    }

    switch (error)
    {
        case NO_ERROR:
            // Do nothing, all is OK
            break;

        case ERROR_FILE_NOT_FOUND:
            OutputDebugString(_T("File "));
            OutputDebugString(filename);
            OutputDebugString(_T(" is not found.\n"));
            break;

        case ERROR_ACCESS_DENIED:
            OutputDebugString(_T("Access is denied for file "));
            OutputDebugString(filename);
            OutputDebugString(_T(".\n"));
            break;

        default:
            OutputDebugString(_T("File I/O error while working with file "));
            OutputDebugString(filename);
            OutputDebugString(_T(".\n"));
            break;
    }

    return error == NO_ERROR;
}

std::vector<std::tstring> ScanFiles(LPCTSTR folder, LPCTSTR mask)
{
    std::vector<std::tstring> res;

    TCHAR curDir[MAX_PATH + 1];
    GetCurrentDirectory(MAX_PATH, curDir);

    BOOL dirRes = SetCurrentDirectory(folder);
    assert(dirRes == TRUE);

    WIN32_FIND_DATA findData = {};
    HANDLE fileHandle = FindFirstFile(mask, &findData);
    if (fileHandle != INVALID_HANDLE_VALUE)
    {
        do
        {
            res.push_back(std::tstring(folder) +_T('/') + findData.cFileName);
        } while (FindNextFile(fileHandle, &findData));
    }

    dirRes = SetCurrentDirectory(curDir);
    assert(dirRes == TRUE);

    return res;
}

std::vector<std::tstring> ScanDirectories(LPCTSTR folder, LPCTSTR fileToFind)
{
    std::vector<std::tstring> res;

    TCHAR curDir[MAX_PATH + 1];
    GetCurrentDirectory(MAX_PATH, curDir);

    BOOL dirRes = SetCurrentDirectory(folder);
    assert(dirRes == TRUE);

    WIN32_FIND_DATA findData = {};
    HANDLE fileHandle = FindFirstFile(_T("*.*"), &findData);
    if (fileHandle != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                std::tstring folderName = findData.cFileName;
                if (folderName != _T(".") && folderName != _T(".."))
                {
                    std::tstring filename = folderName + _T("/") + fileToFind;

                    HANDLE hFile = CreateFile(
                        filename.c_str(),
                        GENERIC_READ,
                        FILE_SHARE_READ,
                        nullptr,
                        OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL,
                        nullptr
                    );
                    if (hFile != INVALID_HANDLE_VALUE)
                    {
                        res.push_back(std::tstring(folder) + _T("/") + filename);

                        CloseHandle(hFile);
                    }
                }
            }
        } while (FindNextFile(fileHandle, &findData));
    }

    dirRes = SetCurrentDirectory(curDir);
    assert(dirRes == TRUE);

    return res;
}

} // Platform
