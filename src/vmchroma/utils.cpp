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
#include <windowsx.h>
#include <winnt.h>
#include <psapi.h>
#include <string>
#include <optional>
#include <vector>
#include <shlwapi.h>
#include <filesystem>
#include <shlobj.h>
#include "utils.hpp"

#include <fstream>
#include <sstream>

#include "spdlog/spdlog.h"
#include "spdlog/sinks/rotating_file_sink.h"

#include <winrt/base.h>

#include "detours.h"
#include "winapi_hook_defs.hpp"

namespace utils
{
/**
* Displays a messagebox with OK button
* @param msg The message to be displayed
*/
void mbox(const std::wstring& msg)
{
    MessageBox(nullptr, msg.c_str(), nullptr, MB_ICONWARNING | MB_OK);
}

/**
 * Create a messagebox with the error message and then terminate with code 1
 * @param msg The message to be displayed
 */
void mbox_error(const std::wstring& msg)
{
    mbox(L"error: " + msg);
    exit(1);
}

/**
 * Converts a UTF-8 encoded std::string to std::wstring to be used for WinAPI
 * @param str Narrow string for conversion
 * @return Converted wide string
 */
std::optional<std::wstring> str_to_wstr(const std::string& str)
{
    const int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), nullptr, 0);
    if (size == 0)
    {
        SPDLOG_ERROR("failed to convert string to wstring");
        return std::nullopt;
    }

    std::wstring res(size, 0);
    if (MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), res.data(), size) == 0)
    {
        SPDLOG_ERROR("failed to convert string to wstring");
        return std::nullopt;
    }

    return res;
}

std::wstring str_to_wstr_or_default(const std::string& str, const std::wstring& def)
{
    const auto converted = str_to_wstr(str);
    return converted ? *converted : def;
}

/**
 * Converts a std::wstring to UTF-8 encoded std::string
 * @param wstr Wide string for conversion
 * @return Converted narrow string
 */
std::optional<std::string> wstr_to_str(const std::wstring& wstr)
{
    const int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size == 0)
    {
        SPDLOG_ERROR("failed to convert wstring to string");
        return std::nullopt;
    }

    std::string res(size - 1, 0);
    if (WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, res.data(), size, nullptr, nullptr) == 0)
    {
        SPDLOG_ERROR("failed to convert wstring to string");
        return std::nullopt;
    }

    return res;
}

std::string wstr_to_str_or_default(const std::wstring& wstr, const std::string& def)
{
    const auto converted = wstr_to_str(wstr);
    return converted ? *converted : def;
}

/**
 * Convert COLORREF (BBGGRR) to RGB hex string (#RRGGBB)
 * See https://learn.microsoft.com/en-us/windows/win32/gdi/colorref
 * @param color The color in COLORREF format
 * @return The color as hex string
 */
std::string colorref_to_hex(const COLORREF color)
{
    std::stringstream ss;
    ss << '#' << std::uppercase << std::hex
        << std::setw(2) << std::setfill('0') << static_cast<int>(GetRValue(color))
        << std::setw(2) << std::setfill('0') << static_cast<int>(GetGValue(color))
        << std::setw(2) << std::setfill('0') << static_cast<int>(GetBValue(color));

    return ss.str();
}

/**
 * Convert RGB hex string (#RRGGBB) to COLORREF (BBGGRR)
 * See https://learn.microsoft.com/en-us/windows/win32/gdi/colorref
 * @param hex The color as hex string
 * @return The color in COLORREF format
 */
std::optional<COLORREF> hex_to_colorref(const std::string& hex)
{
    if (hex.empty())
    {
        SPDLOG_ERROR("empty hex value passed");
        return std::nullopt;
    }

    std::string clean_hex = (hex[0] == '#') ? hex.substr(1) : hex;

    if (clean_hex.length() != 6)
    {
        SPDLOG_ERROR("invalid value passed: {}", hex);
        return std::nullopt;
    }

    unsigned long value = 0;

    try
    {
        value = std::stoul(clean_hex, nullptr, 16);
    }
    catch (...)
    {
        SPDLOG_ERROR("invalid hex value passed: {}", clean_hex);
        return std::nullopt;
    }

    const uint8_t r = (value >> 16) & 0xFF;
    const uint8_t g = (value >> 8) & 0xFF;
    const uint8_t b = value & 0xFF;

    return RGB(r, g, b);
}

/**
 * @brief Scans the main module's memory for all occurrences of a given signature.
 * @param sig A struct containing the byte pattern and a mask ('?' for wildcards).
 * @return A std::vector<uint8_t*> containing the address of every match. The vector will be empty if no matches are found or if an error occurs.
 */
std::vector<uint8_t*> find_signatures(const signature_t& sig)
{
    std::vector<uint8_t*> occurrences;

    const auto handle = GetModuleHandle(nullptr);
    MODULEINFO mod_info;

    if (!handle)
    {
        SPDLOG_ERROR("failed to get module handle");
        return occurrences;
    }

    if (!GetModuleInformation(GetCurrentProcess(), handle, &mod_info, sizeof(mod_info)))
    {
        SPDLOG_ERROR("failed to get module information");
        return occurrences;
    }

    const auto start = static_cast<uint8_t*>(mod_info.lpBaseOfDll);
    const size_t end = mod_info.SizeOfImage;
    const size_t pattern_size = sig.pattern.size();
    const uint8_t* pattern = sig.pattern.data();
    const char* mask = sig.mask.data();

    for (size_t i = 0; i <= end - pattern_size; i++)
    {
        bool found = true;

        for (size_t j = 0; j < pattern_size; j++)
        {
            if (mask[j] != '?' && pattern[j] != start[i + j])
            {
                found = false;
                break;
            }
        }

        if (found)
            occurrences.push_back(start + i);
    }

    return occurrences;
}

/**
 * Loads the bitmap file from the specified path
 * @param path Path to bitmap
 * @param target Target buffer
 * @return True on success
 */
bool load_bitmap(const std::wstring& path, std::vector<uint8_t>& target)
{
    std::ifstream f(path.c_str(), std::ios::binary | std::ios::ate);

    if (!f.is_open())
    {
        SPDLOG_ERROR("failed to open file {}", *wstr_to_str(path));
        return false;
    }

    const auto size = f.tellg();
    f.seekg(0, std::ios::beg);
    target.assign(size, '\0');

    if (!f.read(reinterpret_cast<char*>(target.data()), size))
    {
        SPDLOG_ERROR("failed to read file {}", *wstr_to_str(path));
        return false;
    }

    return true;
}

/**
 * Gets the path to the Voicemeeter user directory
 * @return Path to VM directory
 */
std::optional<std::wstring> get_userprofile_path()
{
    PWSTR buffer = nullptr;

    const auto res = SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &buffer);

    if (res != S_OK)
    {
        SPDLOG_ERROR("SHGetKnownFolderPath failed: {}", res);
        return std::nullopt;
    }

    const std::wstring userprofile_path = buffer;
    std::wstring result = std::filesystem::path(userprofile_path) / L"Voicemeeter";
    CoTaskMemFree(buffer);

    return result;
}

/**
 * Initializes logging library to log to a file
 */
void setup_logging()
{
    const auto userprofile_path_wstr = get_userprofile_path();

    if (!userprofile_path_wstr)
        mbox_error(L"setup_logging: failed to get user profile path");

    auto userprofile_path = wstr_to_str(*userprofile_path_wstr);

    if (!userprofile_path)
        mbox_error(L"setup_logging: string conversion error");

    const std::string log_file_path = (std::filesystem::path(*userprofile_path) / "themes" / "vmchroma_log.txt").string();

    try
    {
        const auto logger = spdlog::rotating_logger_mt("vmchroma_logger", log_file_path, 1048576 * 5, 1);
        spdlog::set_default_logger(logger);
        spdlog::set_pattern("[%d.%m.%Y %H:%M:%S] [%l] %s %!:%# %v");
        spdlog::set_level(spdlog::level::err);
        spdlog::flush_on(spdlog::level::err);
    }
    catch (const spdlog::spdlog_ex& ex)
    {
        mbox_error(L"logger setup error:");
    }
}

std::optional<uint8_t*> find_code_cave(uint8_t* base_handle, const size_t size)
{
    const auto dos_header = reinterpret_cast<PIMAGE_DOS_HEADER>(base_handle);
    const auto nt_headers = reinterpret_cast<PIMAGE_NT_HEADERS>(base_handle + dos_header->e_lfanew);
    const auto section_header = IMAGE_FIRST_SECTION(nt_headers);
    uint8_t* ptr_text_end = nullptr;

    for (int k = 0; k < nt_headers->FileHeader.NumberOfSections; ++k)
    {
        if (!strncmp(reinterpret_cast<char*>(section_header[k].Name), ".text", 5))
        {
            const auto text_start = base_handle + section_header[k].VirtualAddress;
            ptr_text_end = text_start + section_header[k].Misc.VirtualSize;
        }
    }

    if (!ptr_text_end)
    {
        SPDLOG_ERROR("failed to find .text section end");
        return std::nullopt;
    }

    for (int i = 0; i < size; ++i)
    {
        if (ptr_text_end[i] != 0)
        {
            SPDLOG_ERROR("not enough free bytes at the end of .text section");
            return std::nullopt;
        }
    }

    return ptr_text_end;
}

std::optional<std::wstring> get_path_for_pid(DWORD pid)
{
    const auto proc = o_OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);

    if (!proc)
    {
        SPDLOG_ERROR("error OpenProcess for pid {}", pid);
        return std::nullopt;
    }

    std::wstring proc_name(MAX_PATH, '\0');

    DWORD bufferSize = MAX_PATH;
    if (!QueryFullProcessImageName(proc, 0, proc_name.data(), &bufferSize))
    {
        SPDLOG_ERROR("error QueryFullProcessImageName for pid {}", pid);
        return std::nullopt;
    }

    CloseHandle(proc);

    proc_name.resize(bufferSize);
    return proc_name;
}

std::optional<std::wstring> get_exe_image_name_for_pid(DWORD pid)
{
    const auto proc_name = get_path_for_pid(pid);

    if (!proc_name)
        return std::nullopt;

    const auto file_name = PathFindFileName(proc_name->c_str());

    return std::wstring(file_name);
}

std::optional<std::wstring> get_exe_product_name_for_pid(DWORD pid)
{
    const auto proc_name = get_path_for_pid(pid);

    if (!proc_name)
        return std::nullopt;

    DWORD dummy;
    const auto version_info_size = GetFileVersionInfoSize(proc_name->c_str(), &dummy);

    if (version_info_size == 0)
    {
        SPDLOG_ERROR("GetFileVersionInfoSize returned 0");
        return std::nullopt;
    }

    std::vector<char> version_info(version_info_size);

    // call the hooked function on purpose, so it can return a custom name, if set
    if (!GetFileVersionInfoW(proc_name->c_str(), 0, version_info_size, version_info.data()))
    {
        SPDLOG_ERROR("GetFileVersionInfo failed");
        return std::nullopt;
    }

    struct LANGANDCODEPAGE
    {
        WORD wLanguage;
        WORD wCodePage;
    } * translations;

    UINT translation_len = 0;
    if (!o_VerQueryValueW(version_info.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<LPVOID*>(&translations), &translation_len))
    {
        SPDLOG_ERROR("VerQueryValue failed");
        return std::nullopt;
    }

    if (translation_len == 0)
    {
        return std::nullopt;
    }

    std::vector<wchar_t> query_path(256);
    swprintf_s(query_path.data(), query_path.size(), L"\\StringFileInfo\\%04x%04x\\ProductName", translations[0].wLanguage, translations[0].wCodePage);

    wchar_t* product_name = nullptr;
    UINT product_name_len = 0;

    // call the hooked function on purpose, so it can return a custom name, if set
    if (VerQueryValueW(version_info.data(), query_path.data(), reinterpret_cast<LPVOID*>(&product_name), &product_name_len))
        return std::wstring(product_name, product_name_len);

    return std::nullopt;
}

/**
 * Patches the mulss/fmul instructions to change the mouse wheel scroll dB value multiplier
 * @param ptr_scroll_value Pointer to the scroll step value
 * @return True if patches successfully
 */
bool apply_scroll_patch64(float* ptr_scroll_value)
{
    const auto base_handle = reinterpret_cast<uint8_t*>(GetModuleHandle(nullptr));

    if (!base_handle)
    {
        SPDLOG_ERROR("failed to get base module handle");
        return false;
    }

    uint8_t shellcode_multiply[] = {
        0x51, // push rcx
        0x48, 0xB9, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // mov rcx, scroll_value
        0xF3, 0x0F, 0x10, 0x31, // movss xmm6, [rcx]
        0xF3, 0x0F, 0x59, 0xC6, // mulss xmm0, xmm6
        0x59, // pop rcx
        0xC3 // ret
    };

    memcpy_s(&shellcode_multiply[3], 8, &ptr_scroll_value, 8);

    const auto ptr_text_end_opt = find_code_cave(base_handle, sizeof(shellcode_multiply));

    if (!ptr_text_end_opt)
    {
        SPDLOG_ERROR("failed to get address of .text section");
        return false;
    }

    const auto ptr_text_end = *ptr_text_end_opt;

    DWORD dummy;

    if (!VirtualProtect(ptr_text_end, sizeof(shellcode_multiply), PAGE_EXECUTE_READWRITE, &dummy))
    {
        SPDLOG_ERROR("VirtualProtect failed");
        return false;
    }

    memcpy_s(ptr_text_end, sizeof(shellcode_multiply), shellcode_multiply, sizeof(shellcode_multiply));

    if (!VirtualProtect(ptr_text_end, sizeof(shellcode_multiply), PAGE_EXECUTE_READ, &dummy))
    {
        SPDLOG_ERROR("VirtualProtect failed");
        return false;
    }

    const signature_t sig_mulss1 = {{0xF3, 0x0F, 0x59, 0x05, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x28, 0xF2, 0xF3, 0x0F, 0x5C, 0xF0, 0x0F, 0x2F, 0xCE}, {"xxxx????xxxxxxxxxx"}};
    const signature_t sig_mulss2 = {{0xF3, 0x0F, 0x59, 0x05, 0x00, 0x00, 0x00, 0x00, 0xF3, 0x0F, 0x10, 0x94, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x28, 0xF2}, {"xxxx????xxxx?????xxx"}};

    const auto mulss1_occurrences = find_signatures(sig_mulss1);
    const auto mulss2_occurrences = find_signatures(sig_mulss2);

    std::vector<uint8_t*> mulss_merged;

    mulss_merged.insert(mulss_merged.end(), mulss1_occurrences.begin(), mulss1_occurrences.end());
    mulss_merged.insert(mulss_merged.end(), mulss2_occurrences.begin(), mulss2_occurrences.end());

    if (mulss_merged.size() != 2)
    {
        SPDLOG_ERROR("failed to find instructions to patch");
        return false;
    }

    const auto mulss1 = mulss_merged[0];
    const auto mulss2 = mulss_merged[1];

    uint8_t shellcode_call[] = {
        0xE8, 0x00, 0x00, 0x00, 0x00, // call <shellcode_multiply>
        0x90, 0x90, 0x90 // nop; nop; nop
    };

    if (!VirtualProtect(mulss1, sizeof(shellcode_call), PAGE_EXECUTE_READWRITE, &dummy) || !VirtualProtect(mulss2, sizeof(shellcode_call), PAGE_EXECUTE_READWRITE, &dummy))
    {
        SPDLOG_ERROR("VirtualProtect failed");
        return false;
    }

    auto offset = ptr_text_end - (mulss1 + 5);
    memcpy_s(&shellcode_call[1], 4, &offset, 4);
    memcpy_s(mulss1, sizeof(shellcode_call), &shellcode_call, sizeof(shellcode_call));

    offset = ptr_text_end - (mulss2 + 5);
    memcpy_s(&shellcode_call[1], 4, &offset, 4);
    memcpy_s(mulss2, sizeof(shellcode_call), &shellcode_call, sizeof(shellcode_call));

    if (!VirtualProtect(mulss1, sizeof(shellcode_call), PAGE_EXECUTE_READ, &dummy) || !VirtualProtect(mulss2, sizeof(shellcode_call), PAGE_EXECUTE_READ, &dummy))
    {
        SPDLOG_ERROR("VirtualProtect failed");
        return false;
    }

    FlushInstructionCache(base_handle, nullptr, 0);

    return true;
}

bool apply_scroll_patch32(float* ptr_scroll_value)
{
    const auto base_handle = reinterpret_cast<uint8_t*>(GetModuleHandle(nullptr));

    if (!base_handle)
    {
        SPDLOG_ERROR("failed to get base module handle");
        return false;
    }

    uint8_t shellcode_multiply[] = {
        0x50, // push eax
        0xB8, 0x00, 0x00, 0x00, 0x00, // mov eax, scroll_value
        0xD8, 0x08, // fmul qword ptr [eax]
        0x58, // pop eax
        0xC3 // ret
    };

    memcpy_s(&shellcode_multiply[2], 4, &ptr_scroll_value, 4);

    const auto ptr_text_end_opt = find_code_cave(base_handle, sizeof(shellcode_multiply));

    if (!ptr_text_end_opt)
    {
        SPDLOG_ERROR("failed to get address of .text section");
        return false;
    }

    const auto ptr_text_end = *ptr_text_end_opt;

    DWORD dummy;

    if (!VirtualProtect(ptr_text_end, sizeof(shellcode_multiply), PAGE_EXECUTE_READWRITE, &dummy))
    {
        SPDLOG_ERROR("VirtualProtect failed");
        return false;
    }

    memcpy_s(ptr_text_end, sizeof(shellcode_multiply), shellcode_multiply, sizeof(shellcode_multiply));

    if (!VirtualProtect(ptr_text_end, sizeof(shellcode_multiply), PAGE_EXECUTE_READ, &dummy))
    {
        SPDLOG_ERROR("VirtualProtect failed");
        return false;
    }

    const signature_t sig_fmul1 = {{0xD9, 0x0, 0x0, 0x0, 0xDB, 0x45, 0x00, 0xDC, 0x0D}, "x???xx?xx"};
    const signature_t sig_fmul2 = {{0xD9, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xDB, 0x45, 0x0, 0xDC, 0x0D}, "x??????xx?xx"};

    const auto fmul1_occurrences = find_signatures(sig_fmul1);
    const auto fmul2_occurrences = find_signatures(sig_fmul2);

    std::vector<uint8_t*> fmul_merged;

    fmul_merged.insert(fmul_merged.end(), fmul1_occurrences.begin(), fmul1_occurrences.end());
    fmul_merged.insert(fmul_merged.end(), fmul2_occurrences.begin(), fmul2_occurrences.end());

    if (fmul_merged.size() != 2)
    {
        SPDLOG_ERROR("failed to find instructions to patch");
        return false;
    }

    const auto fmul1 = fmul_merged[0] + 7;
    const auto fmul2 = fmul_merged[1] + 10;

    uint8_t shellcode_call[] = {
        0xE8, 0x00, 0x00, 0x00, 0x00, // call <shellcode_multiply>
        0x90 // nop
    };

    if (!VirtualProtect(fmul1, sizeof(shellcode_call), PAGE_EXECUTE_READWRITE, &dummy) || !VirtualProtect(fmul2, sizeof(shellcode_call), PAGE_EXECUTE_READWRITE, &dummy))
    {
        SPDLOG_ERROR("VirtualProtect failed");
        return false;
    }

    auto offset = ptr_text_end - (fmul1 + 5);
    memcpy_s(&shellcode_call[1], 4, &offset, 4);
    memcpy_s(fmul1, sizeof(shellcode_call), &shellcode_call, sizeof(shellcode_call));

    offset = ptr_text_end - (fmul2 + 5);
    memcpy_s(&shellcode_call[1], 4, &offset, 4);
    memcpy_s(fmul2, sizeof(shellcode_call), &shellcode_call, sizeof(shellcode_call));

    if (!VirtualProtect(fmul1, sizeof(shellcode_call), PAGE_EXECUTE_READ, &dummy) || !VirtualProtect(fmul2, sizeof(shellcode_call), PAGE_EXECUTE_READ, &dummy))
    {
        SPDLOG_ERROR("VirtualProtect failed");
        return false;
    }

    FlushInstructionCache(base_handle, nullptr, 0);

    return true;
}

/**
 * Uses Detours to patch a single function for late hooking
 * @param o_fn The original function
 * @param hk_fn The hook function
 * @return True if hooking was successful
 */
bool hook_single_fn(PVOID* o_fn, PVOID hk_fn)
{
    if (DetourTransactionBegin() != NO_ERROR)
    {
        SPDLOG_ERROR("DetourTransactionBegin failed");
        return false;
    }

    if (DetourUpdateThread(GetCurrentThread()) != NO_ERROR)
    {
        SPDLOG_ERROR("DetourUpdateThread failed");
        return false;
    }

    if (DetourAttach(o_fn, hk_fn) != NO_ERROR)
    {
        SPDLOG_ERROR("DetourAttach failed");
        return false;
    }

    if (DetourTransactionCommit() != NO_ERROR)
    {
        SPDLOG_ERROR("DetourTransactionCommit failed");
        return false;
    }

    return true;
}
}
