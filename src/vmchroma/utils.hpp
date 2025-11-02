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

#pragma once

#include <windows.h>
#include <string>
#include <optional>

#include <spdlog/spdlog.h>

#if defined(_WIN64)
#define ARCH_CALL __fastcall
#else
#define ARCH_CALL __stdcall
#endif

#if defined(_WIN64)
#define WNDPROC_SUB_CALL __fastcall
#else
#define WNDPROC_SUB_CALL __cdecl
#endif

enum flavor_id { FLAVOR_NONE, FLAVOR_DEFAULT, FLAVOR_BANANA, FLAVOR_POTATO };

enum color_category { CATEGORY_TEXT, CATEGORY_SHAPES };

typedef struct flavor_info
{
    std::string name;
    flavor_id id;
    uint32_t bitmap_width_main{};
    uint32_t bitmap_width_settings{};
    uint32_t bitmap_width_cassette{};
    uint32_t htclient_x1{};
    uint32_t htclient_x2{};
} flavor_info_t;

typedef struct createwindowexa_lparam
{
    HWND hwnd;
    int32_t x;
    int32_t y;
    int32_t wnd_id;
    void* unk2;
    void* wndproc;
} createwindowexa_lparam_t;

typedef struct dialogbox_initparam
{
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    int32_t unk1;
    int32_t unk2;
} dialogbox_initparam_t;

typedef struct signature
{
    std::vector<uint8_t> pattern;
    std::string mask;
} signature_t;

typedef LRESULT (WNDPROC_SUB_CALL *o_WndProc_chldwnd_t)(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam, uint64_t a5);

namespace utils
{
void mbox(const std::wstring& msg);
void mbox_error(const std::wstring& msg);
void attach_console_debug();
std::optional<std::wstring> str_to_wstr(const std::string& str);
std::wstring str_to_wstr_or_default(const std::string& str, const std::wstring& def = L"[conversion error]");
std::optional<std::string> wstr_to_str(const std::wstring& wstr);
std::string wstr_to_str_or_default(const std::wstring& wstr, const std::string& def = "[conversion error]");
std::string colorref_to_hex(COLORREF color);
std::optional<COLORREF> hex_to_colorref(const std::string& hex);
std::optional<uint8_t*> find_code_cave(uint8_t* base_handle, size_t size);
std::optional<std::wstring> get_exe_image_name_for_pid(DWORD pid);
std::optional<std::wstring> get_exe_product_name_for_pid(DWORD pid);
std::vector<uint8_t*> find_signatures(const signature_t& sig);
bool load_bitmap(const std::wstring& path, std::vector<uint8_t>& target);
std::optional<std::wstring> get_userprofile_path();
void setup_logging();
bool apply_scroll_patch64(float* ptr_scroll_value);
bool apply_scroll_patch32(float* ptr_scroll_value);
bool hook_single_fn(PVOID* o_fn, PVOID hk_fn);

/**
 * Allocate console to print debug messages
 */
inline void attach_console_debug()
{
#ifndef NDEBUG
    if (!AllocConsole())
        mbox_error(L"AllocConsole");

    FILE* fDummy;
    if (freopen_s(&fDummy, "CONOUT$", "w", stdout) != ERROR_SUCCESS)
        mbox_error(L"freopen_s");
#endif
}
}
