/**
Copyright (C) 2025 Klaus Hahnenkamp

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include <windows.h>
#include <detours.h>
#include <fstream>

static std::wstring dll_path;

static BOOL CALLBACK cb(PVOID pContext, LPCSTR pszFile, LPCSTR* ppszOutFile)
{
    auto added_dll = static_cast<bool*>(pContext);

    if (!pszFile && !*added_dll)
    {
        const int size = WideCharToMultiByte(CP_UTF8, 0, dll_path.c_str(), -1, nullptr, 0, nullptr, nullptr);
        static std::string res(size - 1, 0);
        WideCharToMultiByte(CP_UTF8, 0, dll_path.c_str(), -1, res.data(), size, nullptr, nullptr);

        *ppszOutFile = res.c_str();
        *added_dll = true;
    }

    return TRUE;
}

int wmain(int argc, wchar_t* argv[])
{
    if (argv == nullptr)
    {
        MessageBoxW(nullptr, L"Failed to parse command line", L"Error", MB_OK);
        return 1;
    }

    if (argc != 4)
    {
        MessageBoxW(nullptr, L"wrong number of args", L"Error", MB_OK);
        return 1;
    }

    dll_path = argv[1];
    wprintf(L"adding %s", dll_path.c_str());

    const std::wstring exe_path_old = argv[2];
    const std::wstring exe_path_new = argv[3];

    const auto handle_exe_old = CreateFileW(exe_path_old.c_str(),
                                    GENERIC_READ,
                                    FILE_SHARE_READ,
                                    nullptr,
                                    OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL,
                                    nullptr);

    if (handle_exe_old == INVALID_HANDLE_VALUE)
    {
        wprintf(L"action failed: %s, error: %d\n", exe_path_old.c_str(), GetLastError());
        exit(1);
    }

    const auto binary = DetourBinaryOpen(handle_exe_old);
    CloseHandle(handle_exe_old);

    if (!binary)
    {
        wprintf(L"action failed: %d\n", GetLastError());
        exit(1);
    }

    bool added_dll = false;

    if (!DetourBinaryEditImports(binary, &added_dll, cb, nullptr, nullptr, nullptr))
    {
        wprintf(L"action failed: %d\n", GetLastError());
        DetourBinaryClose(binary);
        exit(1);
    }

    const auto handle_exe_new = CreateFileW(exe_path_new.c_str(),
                             GENERIC_READ | GENERIC_WRITE,
                             0,
                             nullptr,
                             CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                             nullptr);

    if (handle_exe_new == INVALID_HANDLE_VALUE)
    {
        wprintf(L"action failed: %s, error: %d\n", exe_path_new.c_str(), GetLastError());
        exit(1);
    }

    if (!DetourBinaryWrite(binary, handle_exe_new))
    {
        wprintf(L"action failed: %d\n", GetLastError());
        DetourBinaryClose(binary);
        CloseHandle(handle_exe_new);
        exit(1);
    }

    DetourBinaryClose(binary);
    CloseHandle(handle_exe_new);
    wprintf(L"success: %s\n", exe_path_new.c_str());

    return 0;
}
