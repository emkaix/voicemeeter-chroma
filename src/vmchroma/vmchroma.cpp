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
#include <detours.h>
#include <string>
#include <optional>
#include <vector>
#include <shlwapi.h>
#include <filesystem>
#include <shlobj.h>
#include <wingdi.h>
#include <d2d1_1.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <psapi.h>

#include "utils.hpp"
#include "winapi_hook_defs.hpp"
#include "window_manager.hpp"
#include "config_manager.hpp"
#include "spdlog/fmt/bundled/ranges.h"

//******************//
//      WINAPI      //
//******************//

HANDLE (WINAPI *o_CreateMutexA)(LPSECURITY_ATTRIBUTES lpMutexAttributes, BOOL bInitialOwner, LPCSTR lpName) = CreateMutexA;
HFONT (WINAPI *o_CreateFontIndirectA)(const LOGFONTA* lplf) = CreateFontIndirectA;
BOOL (WINAPI *o_AppendMenuA)(HMENU hMenu, UINT uFlags, UINT_PTR uIDNewItem, LPCSTR lpNewItem) = AppendMenuA;
HPEN (WINAPI *o_CreatePen)(int iStyle, int cWidth, COLORREF color) = CreatePen;
HBRUSH (WINAPI *o_CreateBrushIndirect)(const LOGBRUSH* plbrush) = CreateBrushIndirect;
COLORREF (WINAPI *o_SetTextColor)(HDC hdc, COLORREF color) = SetTextColor;
ATOM (WINAPI *o_RegisterClassA)(const WNDCLASSA* lpWndClass) = RegisterClassA;
BOOL (WINAPI *o_Rectangle)(HDC hdc, int left, int top, int right, int bottom) = Rectangle;
HBITMAP (WINAPI *o_CreateDIBSection)(HDC hdc, const BITMAPINFO* pbmi, UINT usage, void** ppvBits, HANDLE hSection, DWORD offset) = CreateDIBSection;
HDC (WINAPI *o_BeginPaint)(HWND hWnd, LPPAINTSTRUCT lpPaint) = BeginPaint;
UINT_PTR (WINAPI *o_SetTimer)(HWND hWnd, UINT_PTR nIDEvent, UINT uElapse, TIMERPROC lpTimerFunc) = SetTimer;
HDC (WINAPI *o_GetDC)(HWND hWnd) = GetDC;
int (WINAPI *o_ReleaseDC)(HWND hWnd, HDC hDC) = ReleaseDC;
BOOL (WINAPI *o_SetWindowPos)(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags) = SetWindowPos;
BOOL (WINAPI *o_TrackPopupMenu)(HMENU hMenu, UINT uFlags, int x, int y, int nReserved, HWND hWnd, const RECT* prcRect) = TrackPopupMenu;
BOOL (WINAPI *o_GetClientRect)(HWND hWnd, LPRECT lpRect) = GetClientRect;
HWND (WINAPI *o_CreateWindowExA)(DWORD dwExStyle, LPCSTR lpClassName, LPCSTR lpWindowName, DWORD dwStyle, int X, int Y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam) = CreateWindowExA;
INT_PTR (WINAPI *o_DialogBoxIndirectParamA)(HINSTANCE hInstance, LPCDLGTEMPLATEA hDialogTemplate, HWND hWndParent, DLGPROC lpDialogFunc, LPARAM dwInitParam) = DialogBoxIndirectParamA;
HRESULT (WINAPI *o_CoCreateInstance)(REFCLSID rclsid, LPUNKNOWN pUnkOuter, DWORD dwClsContext, REFIID riid, LPVOID* ppv) = CoCreateInstance;
int (WINAPI *o_InternalGetWindowText)(HWND hWnd, LPWSTR pString, int cchMaxCount) = InternalGetWindowText;
BOOL (WINAPI *o_GetFileVersionInfoW)(LPCWSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData) = GetFileVersionInfoW;
BOOL (WINAPI *o_VerQueryValueW)(LPCVOID pBlock, LPCWSTR lpSubBlock, LPVOID* lplpBuffer, PUINT puLen) = VerQueryValueW;
HANDLE (WINAPI *o_OpenProcess)(DWORD dwDesiredAccess, BOOL bInheritHandle, DWORD dwProcessId) = OpenProcess;

//******************//
//       COM        //
//******************//

HRESULT (STDMETHODCALLTYPE *o_GetSessionEnumerator)(IAudioSessionManager2* this_ptr, IAudioSessionEnumerator** SessionEnum) = nullptr;
HRESULT (STDMETHODCALLTYPE *o_GetSession)(IAudioSessionEnumerator* this_ptr, int SessionCount, IAudioSessionControl** Session) = nullptr;
HRESULT (STDMETHODCALLTYPE *o_GetProcessId)(IAudioSessionControl2* this_ptr, DWORD* pRetVal) = nullptr;
HRESULT (STDMETHODCALLTYPE *o_IsSystemSoundsSession)(IAudioSessionControl2* this_ptr) = nullptr;

//******************//
//      GLOBALS     //
//******************//

std::unique_ptr<window_manager> wm;
std::unique_ptr<config_manager> cm;

static bool init_entered = false;
static float scroll_value = 3.0f;
static WNDPROC o_WndProc_main = nullptr;
static o_WndProc_chldwnd_t o_WndProc_comp = nullptr;
static o_WndProc_chldwnd_t o_WndProc_denoiser = nullptr;
static o_WndProc_chldwnd_t o_WndProc_wdb = nullptr;
static HMENU tray_menu = nullptr;
static std::wstring file_version_buffer;

bool apply_hooks();

//*****************************//
//      HOOKED FUNCTIONS       //
//*****************************//

/**
 * We hook this function to initialize the theme, because it gets called early in WinMain and is exported
 * The theme config is loaded from "C:\Users\<User>\Documents\Voicemeeter"
 * See https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createmutexa
 */
HANDLE WINAPI hk_CreateMutexA(LPSECURITY_ATTRIBUTES lpMutexAttributes, BOOL bInitialOwner, LPCSTR lpName)
{
    if (!init_entered)
    {
        init_entered = true;

        utils::setup_logging();

        wm = std::make_unique<window_manager>();
        cm = std::make_unique<config_manager>();

        if (!cm->load_config())
        {
            SPDLOG_ERROR("failed to load config");
            utils::mbox_error(L"failed to load config, check error log for more details");
            return o_CreateMutexA(lpMutexAttributes, bInitialOwner, lpName);
        }

        if (!cm->init_theme())
        {
            SPDLOG_ERROR("failed to init theme");
            utils::mbox_error(L"failed to init theme, check error log for more details");
            return o_CreateMutexA(lpMutexAttributes, bInitialOwner, lpName);
        }

        if (!apply_hooks())
        {
            SPDLOG_ERROR("hooking failed");
            return o_CreateMutexA(lpMutexAttributes, bInitialOwner, lpName);
        }
    }

    return o_CreateMutexA(lpMutexAttributes, bInitialOwner, lpName);
}

/**
 * Creates a font object
 * We hook this function to change the font size and quality
 * See https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-createfontindirecta
 */
HFONT WINAPI hk_CreateFontIndirectA(const LOGFONTA* lplf)
{
    std::unordered_map<long, long> font_height_map = {
        {20, 18}, // input custom label
        {16, 15} // master section fader
    };

    LOGFONTA modified_log_font = *lplf;
    const long new_size = font_height_map[lplf->lfHeight];
    modified_log_font.lfHeight = new_size != 0 ? new_size : lplf->lfHeight;
    modified_log_font.lfQuality = *cm->get_font_quality();

    return o_CreateFontIndirectA(&modified_log_font);
}

/**
 * Adds a menu item to a menu
 * We hook this function to add custom menu items to the main menu
 * See https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-appendmenua
 */
BOOL WINAPI hk_AppendMenuA(HMENU hMenu, UINT uFlags, UINT_PTR uIDNewItem, LPCSTR lpNewItem)
{
    if (uIDNewItem == 0x1F9u)
    {
        o_AppendMenuA(hMenu, uFlags, uIDNewItem, lpNewItem);

        return o_AppendMenuA(hMenu, uFlags, 0x1337, VMCHROMA_VERSION);
    }

    // get tray menu handle
    if (lpNewItem != nullptr && strcmp(lpNewItem, "Exit Menu") == 0)
        tray_menu = hMenu;

    return o_AppendMenuA(hMenu, uFlags, uIDNewItem, lpNewItem);
}

/**
 * GDI function used to draw lines
 * We hook this function to change the color of UI elements made up of lines
 * Color values are parsed from the colors.yaml
 * See https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-createpen
 */
HPEN WINAPI hk_CreatePen(int iStyle, int cWidth, COLORREF color)
{
    if (const auto new_col_opt = cm->cfg_get_color(utils::colorref_to_hex(color), CATEGORY_SHAPES))
    {
        if (const auto new_col = utils::hex_to_colorref(*new_col_opt))
            color = *new_col;
    }

    return o_CreatePen(iStyle, cWidth, color);
}

/**
 * GDI function used to draw forms like filled rectangles
 * We hook this function to change the color of UI elements made up of such forms
 * Color values are parsed from the colors.yaml
 * See https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-createbrushindirect
 */
HBRUSH WINAPI hk_CreateBrushIndirect(LOGBRUSH* plbrush)
{
    if (const auto new_col_opt = cm->cfg_get_color(utils::colorref_to_hex(plbrush->lbColor), CATEGORY_SHAPES))
    {
        if (const auto new_col = utils::hex_to_colorref(*new_col_opt))
            plbrush->lbColor = *new_col;
    }

    return o_CreateBrushIndirect(plbrush);
}

/**
 * GDI function used to set the color of text
 * We hook this function to change text color
 * Color values are parsed from the colors.yaml
 * See https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-settextcolor
 */
COLORREF WINAPI hk_SetTextColor(HDC hdc, COLORREF color)
{
    if (const auto new_col_opt = cm->cfg_get_color(utils::colorref_to_hex(color), CATEGORY_TEXT))
    {
        if (const auto new_col = utils::hex_to_colorref(*new_col_opt))
            color = *new_col;
    }

    return o_SetTextColor(hdc, color);
}

/**
 * Sets the time interval for WM_TIMER messages, used to dynamically update UI elements without user interaction
 * Interval value is parsed from the vmchroma.yaml
 * See https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-settimer
 */
UINT_PTR WINAPI hk_SetTimer(HWND hWnd, UINT_PTR nIDEvent, UINT uElapse, TIMERPROC lpTimerFunc)
{
    if (nIDEvent == 12346)
    {
        if (const auto interval = cm->get_ui_update_interval())
            return o_SetTimer(hWnd, nIDEvent, *interval, lpTimerFunc);
    }

    return o_SetTimer(hWnd, nIDEvent, uElapse, lpTimerFunc);
}

/**
 * We hook this function to disable drawing specific rectangles that are supposed to mask the background
 * See https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-rectangle
 */
BOOL WINAPI hk_Rectangle(HDC hdc, int left, int top, int right, int bottom)
{
    if (cm->get_current_flavor_id() == FLAVOR_POTATO)
    {
        if ((left == 1469 && top == 15) || // box inside menu button
            (left == 1221 && top == 581) || // bus fader box
            (left == 1159 && top == 581) || // bus fader box
            (left == 1345 && top == 581) || // bus fader box
            (left == 1283 && top == 581)) // bus fader box
            return true;
    }

    if (cm->get_current_flavor_id() == FLAVOR_BANANA)
    {
        if ((left == 848 && top == 15) || // box inside menu button
            (left == 789 && top == 432) || // bus fader box
            (left == 727 && top == 432) || // bus fader box
            (left == 913 && top == 432) || // bus fader box
            (left == 851 && top == 432)) // bus fader box
            return true;
    }

    return o_Rectangle(hdc, left, top, right, bottom);
}

/**
 * Provides a pointer to a buffer for the loaded bitmaps
 * We hook this function in order to write our own background bitmaps to the buffer
 * See https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-createdibsection
 */
HBITMAP WINAPI hk_CreateDIBSection(HDC hdc, BITMAPINFO* pbmi, UINT usage, void** ppvBits, HANDLE hSection, DWORD offset)
{
    void* ppvBits_new = nullptr;
    const uint8_t* bm_data = nullptr;

    if (pbmi->bmiHeader.biWidth == cm->get_active_flavor().bitmap_width_main)
        bm_data = cm->get_bm_data_main().data();
    else if (pbmi->bmiHeader.biWidth == cm->get_active_flavor().bitmap_width_settings)
        bm_data = cm->get_bm_data_settings().data();
    else if (pbmi->bmiHeader.biWidth == cm->get_active_flavor().bitmap_width_cassette)
        bm_data = cm->get_bm_data_cassette().data();

    if (bm_data != nullptr)
    {
        const auto bm_offset = reinterpret_cast<const BITMAPFILEHEADER*>(bm_data)->bfOffBits;
        const auto bm_handle = o_CreateDIBSection(hdc, pbmi, usage, &ppvBits_new, hSection, offset);

        memcpy(ppvBits_new, &bm_data[bm_offset], pbmi->bmiHeader.biSizeImage);

        return bm_handle;
    }

    return o_CreateDIBSection(hdc, pbmi, usage, ppvBits, hSection, offset);
}

/**
 * Is called on WM_PAINT messages
 * We hook this function to replace the window DC with our D2D memory DC
 * See https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-beginpaint
 */
HDC WINAPI hk_BeginPaint(HWND hWnd, LPPAINTSTRUCT lpPaint)
{
    if (wm->is_in_map(hWnd))
    {
        o_BeginPaint(hWnd, lpPaint);

        const auto& wctx = wm->get_wctx(hWnd);

        return wctx.mem_dc;
    }

    return o_BeginPaint(hWnd, lpPaint);
}

/**
 * Get a device context for the specified hwnd
 * We hook this function to replace the window DC with our D2D memory DC
 * See https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getdc
 */
HDC WINAPI hk_GetDC(HWND hWnd)
{
    if (wm->is_in_map(hWnd))
    {
        const auto& wctx = wm->get_wctx(hWnd);

        return wctx.mem_dc;
    }

    return o_GetDC(hWnd);
}

/**
 * Releases the device context for the specified hwnd
 * We hook this function to disable releasing our D2D memory DC
 * See https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-releasedc
 */
int WINAPI hk_ReleaseDC(HWND hWnd, HDC hdc)
{
    if (wm->is_in_map(hWnd))
        return 1;

    return o_ReleaseDC(hWnd, hdc);
}

/**
 * Retrieves the coordinates of a window's client area
 * We hook this function to fake the current window size to the application
 * See https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getclientrect
 */
BOOL WINAPI hk_GetClientRect(HWND hWnd, LPRECT lpRect)
{
    // subwindows should think they have their default size
    auto class_name = std::wstring(256, '\0');
    const int len = GetClassNameW(hWnd, class_name.data(), 256);
    class_name.resize(len);

    const auto parent_hwnd = GetAncestor(hWnd, GA_PARENT);

    if (!parent_hwnd)
    {
        SPDLOG_ERROR("Error finding parent window");
        return o_GetClientRect(hWnd, lpRect);
    }
    
    auto parent_class_name = std::wstring(256, '\0');
    const int parent_len = GetClassNameW(parent_hwnd, parent_class_name.data(), 256);
    parent_class_name.resize(parent_len);

    if (parent_class_name == window_manager::MAINWINDOW_CLASSNAME_UNICODE)
    {
        if (class_name == window_manager::WDB_CLASSNAME_UNICODE)
        {
            lpRect->left = 0;
            lpRect->top = 0;
            lpRect->right = 100;
            lpRect->bottom = 386;
            return TRUE;
        }

        if (class_name == window_manager::COMPDENOISE_CLASSNAME_UNICODE)
        {
            lpRect->left = 0;
            lpRect->top = 0;
            lpRect->right = 153;
            lpRect->bottom = 413;
            return TRUE;
        }
    }

    return o_GetClientRect(hWnd, lpRect);
}

/**
 * Changes the position of the window
 * We hook this function to disable the default drag behaviour since it's not compatible with the resizing feature and we have our own
 * See https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowpos
 */
BOOL WINAPI hk_SetWindowPos(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags)
{
    if (hWnd == wm->get_hwnd_main() && GetAncestor(hWnd, GA_ROOT))
        return TRUE;

    return o_SetWindowPos(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags);
}

/**
 * Displays several popup menus
 * We hook this function to display the menus at the correct location when window is resized
 * See https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-trackpopupmenu
 */
BOOL WINAPI hk_TrackPopupMenu(HMENU hMenu, UINT uFlags, int x, int y, int nReserved, HWND hWnd, const RECT* prcRect)
{
    POINT pt = {x, y};

    if (hMenu != tray_menu && hWnd == wm->get_hwnd_main() && GetAncestor(hWnd, GA_ROOT))
    {
        ScreenToClient(hWnd, &pt);

        wm->scale_coords_inverse(hWnd, pt);

        ClientToScreen(hWnd, &pt);
    }

    return o_TrackPopupMenu(hMenu, uFlags, pt.x, pt.y, nReserved, hWnd, prcRect);
}

/**
 * Wndproc function of the main window
 * We hook this function to handle the resizing and render logic
 */
LRESULT ARCH_CALL hk_WndProc_main(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_COMMAND && LOWORD(wParam) == 0x1337)
        ShellExecuteW(nullptr, L"open", L"https://github.com/emkaix/voicemeeter-chroma", nullptr, nullptr, SW_SHOW);

    if (msg == WM_TIMER && wParam == 12346)
    {
        const auto ret = o_WndProc_main(hwnd, msg, wParam, lParam);
        wm->render(hwnd);
        return ret;
    }

    if (msg == WM_DISPLAYCHANGE)
    {
        const auto& wctx = wm->get_wctx(hwnd);

        SendMessageW(hwnd, WM_ERASEBKGND, reinterpret_cast<WPARAM>(wctx.mem_dc), lParam);
        SendMessageW(hwnd, WM_PAINT, 0, 0);
        return 0;
    }

    if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK || msg == WM_LBUTTONUP || msg == WM_RBUTTONDOWN || msg == WM_RBUTTONDBLCLK || msg == WM_RBUTTONUP)
    {
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        wm->scale_coords(hwnd, pt);

        const auto ret = o_WndProc_main(hwnd, msg, wParam, MAKELPARAM(pt.x, pt.y));

        if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK || msg == WM_LBUTTONUP)
            wm->render(hwnd);

        return ret;
    }

    if (msg == WM_MOUSEWHEEL)
    {
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};

        ScreenToClient(hwnd, &pt);

        wm->scale_coords(hwnd, pt);

        ClientToScreen(hwnd, &pt);

        const auto shift_val = cm->get_fader_shift_scroll_step();
        const auto normal_val = cm->get_fader_scroll_step();

        if (shift_val && normal_val)
            scroll_value = GetAsyncKeyState(VK_SHIFT) & 0x8000 ? *shift_val : *normal_val;

        const auto ret = o_WndProc_main(hwnd, msg, wParam, MAKELPARAM(pt.x, pt.y));

        wm->render(hwnd);

        return ret;
    }

    if (msg == WM_MOUSEMOVE)
    {
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};

        wm->scale_coords(hwnd, pt);

        const auto ret = o_WndProc_main(hwnd, msg, wParam, MAKELPARAM(pt.x, pt.y));

        // keep db meters from being visually stuck
        if (wParam & MK_LBUTTON)
            SendMessageA(hwnd, WM_TIMER, 12346, 0);

        return ret;
    }


    if (msg == WM_CREATE)
    {
        const auto cs = reinterpret_cast<const CREATESTRUCTA*>(lParam);

        wm->init_window(hwnd, WND_TYPE_MAIN, cs);

        wm->set_hwnd_main(hwnd);

        wm->set_default_main_wnd_size(cs->cx, cs->cy);

        uint32_t w, h;
        LRESULT ret;

        auto restore_size_opt = cm->get_restore_size();

        bool restore_size = true;

        if (restore_size_opt)
            restore_size = *restore_size_opt;

        if (restore_size && cm->reg_get_wnd_size(w, h))
        {
            wm->set_cur_main_wnd_size(w, h);

            ret = o_WndProc_main(hwnd, msg, wParam, lParam);

            o_SetWindowPos(hwnd, nullptr, cs->x, cs->y, w, h, SWP_NOREDRAW);

            wm->resize_d2d(hwnd, D2D1::SizeU(w, h));
        }
        else
        {
            wm->set_cur_main_wnd_size(cs->cx, cs->cy);

            ret = o_WndProc_main(hwnd, msg, wParam, lParam);
        }

        // patch mouse scroll instructions after integrity checks
#if defined(_WIN64)
        if (!utils::apply_scroll_patch64(&scroll_value))
#else
        if (!utils::apply_scroll_patch32(&scroll_value))
#endif
        {
            SPDLOG_ERROR("unable to apply scroll patch");
            return ret;
        }

        return ret;
    }

    if (msg == WM_NCHITTEST)
    {
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hwnd, &pt);

        RECT rc;
        o_GetClientRect(hwnd, &rc);

        constexpr int area_size = 10;

        if (pt.x > rc.right - area_size && pt.y > rc.bottom - area_size)
            return HTBOTTOMRIGHT;

        wm->scale_coords(hwnd, pt);

        const auto& af = cm->get_active_flavor();

        if (pt.x > af.htclient_x1 && pt.x < af.htclient_x2 && pt.y < 40)
            return HTCAPTION;

        return HTCLIENT;
    }

    if (msg == WM_SIZING)
    {
        const auto& wctx = wm->get_wctx(hwnd);

        if (wParam != WMSZ_BOTTOMRIGHT)
            return 0;

        const auto rect = reinterpret_cast<RECT*>(lParam);

        int new_width = rect->right - rect->left;
        new_width = max(wctx.default_cx / 2, min(wctx.default_cx, new_width));

        int new_height = MulDiv(new_width, wctx.default_cy, wctx.default_cx);
        new_height = max(wctx.default_cy / 2, min(wctx.default_cy, new_height));

        rect->right = rect->left + new_width;
        rect->bottom = rect->top + new_height;

        wm->set_cur_main_wnd_size(new_width, new_height);

        wm->resize_child_windows();

        SendMessageA(hwnd, WM_TIMER, 12346, 0);

        return 1;
    }

    if (msg == WM_SIZE)
    {
        const D2D1_SIZE_U size = {LOWORD(lParam), HIWORD(lParam)};

        wm->resize_d2d(hwnd, size);

        wm->set_cur_main_wnd_size(LOWORD(lParam), HIWORD(lParam));

        return o_WndProc_main(hwnd, msg, wParam, lParam);
    }

    if (msg == WM_PAINT)
    {
        const auto ret = o_WndProc_main(hwnd, msg, wParam, lParam);

        SendMessageA(hwnd, WM_TIMER, 12346, 0);

        wm->render(hwnd);

        return ret;
    }

    if (msg == WM_ERASEBKGND)
    {
        const auto& wctx = wm->get_wctx(hwnd);

        o_WndProc_main(hwnd, msg, reinterpret_cast<WPARAM>(wctx.mem_dc), lParam);

        return 1;
    }

    if (msg == WM_DESTROY)
    {
        RECT rc;
        o_GetClientRect(hwnd, &rc);

        const auto& wctx = wm->get_wctx(hwnd);

        if (rc.right > 0 && rc.right <= wctx.default_cx && rc.bottom > 0 && rc.bottom <= wctx.default_cy)
            cm->reg_save_wnd_size(rc.right, rc.bottom);

        wm->destroy_window(hwnd);
    }

    return o_WndProc_main(hwnd, msg, wParam, lParam);
}

/**
 * Wndproc function of the compressor / gate child window for potato
 * We hook this function to handle the resizing and render logic
 */
LRESULT WNDPROC_SUB_CALL hk_WndProc_comp(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, uint64_t a5)
{
    if (msg == WM_CREATE)
    {
        const auto cs = reinterpret_cast<CREATESTRUCTA*>(lParam);
        wm->init_window(hwnd, WND_TYPE_COMP_DENOISE, cs);

        o_SetTimer(hwnd, 12346, 15, nullptr);

        wm->scale_to_main_wnd(cs->x, cs->y, cs->cx, cs->cy);

        MoveWindow(hwnd, cs->x, cs->y, cs->cx, cs->cy, false);

        wm->resize_d2d(hwnd, D2D1::SizeU(cs->cx, cs->cy));

        return o_WndProc_comp(hwnd, msg, wParam, lParam, a5);
    }

    if (msg == WM_PAINT)
    {
        const auto ret = o_WndProc_comp(hwnd, msg, wParam, lParam, a5);

        wm->render(hwnd);

        return ret;
    }

    if (msg == WM_TIMER && wParam == 12346)
    {
        wm->render(hwnd);

        return 0;
    }

    if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK || msg == WM_LBUTTONUP || msg == WM_RBUTTONDOWN || msg == WM_RBUTTONDBLCLK || msg == WM_RBUTTONUP)
    {
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        wm->scale_coords(hwnd, pt);

        const auto ret = o_WndProc_comp(hwnd, msg, wParam, MAKELPARAM(pt.x, pt.y), a5);

        if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK || msg == WM_LBUTTONUP)
            wm->render(hwnd);

        return ret;
    }

    if (msg == WM_MOUSEMOVE)
    {
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};

        wm->scale_coords(hwnd, pt);

        const auto ret = o_WndProc_comp(hwnd, msg, wParam, MAKELPARAM(pt.x, pt.y), a5);

        if (wParam & MK_LBUTTON)
            wm->render(hwnd);

        return ret;
    }

    if (msg == WM_DESTROY)
    {
        const auto ret = o_WndProc_comp(hwnd, msg, wParam, lParam, a5);

        wm->destroy_window(hwnd);

        return ret;
    }

    if (msg == WM_ERASEBKGND)
    {
        const auto& wctx = wm->get_wctx(hwnd);

        return o_WndProc_comp(hwnd, msg, reinterpret_cast<WPARAM>(wctx.mem_dc), lParam, a5);
    }

    return o_WndProc_comp(hwnd, msg, wParam, lParam, a5);
}

/**
 * Wndproc function of the denoiser child window for potato
 * We hook this function to handle the resizing and render logic
 */
LRESULT WNDPROC_SUB_CALL hk_WndProc_denoiser(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, uint64_t a5)
{
    if (msg == WM_CREATE)
    {
        const auto cs = reinterpret_cast<CREATESTRUCTA*>(lParam);
        wm->init_window(hwnd, WND_TYPE_COMP_DENOISE, cs);

        o_SetTimer(hwnd, 12346, 15, nullptr);

        wm->scale_to_main_wnd(cs->x, cs->y, cs->cx, cs->cy);

        MoveWindow(hwnd, cs->x, cs->y, cs->cx, cs->cy, false);

        wm->resize_d2d(hwnd, D2D1::SizeU(cs->cx, cs->cy));

        return o_WndProc_denoiser(hwnd, msg, wParam, lParam, a5);
    }

    if (msg == WM_TIMER && wParam == 12346)
    {
        wm->render(hwnd);

        return 0;
    }

    if (msg == WM_PAINT)
    {
        const auto ret = o_WndProc_denoiser(hwnd, msg, wParam, lParam, a5);

        wm->render(hwnd);

        return ret;
    }

    if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK || msg == WM_LBUTTONUP || msg == WM_RBUTTONDOWN || msg == WM_RBUTTONDBLCLK || msg == WM_RBUTTONUP)
    {
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        wm->scale_coords(hwnd, pt);

        const auto ret = o_WndProc_denoiser(hwnd, msg, wParam, MAKELPARAM(pt.x, pt.y), a5);

        if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK || msg == WM_LBUTTONUP)
            wm->render(hwnd);

        return ret;
    }

    if (msg == WM_MOUSEMOVE)
    {
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};

        wm->scale_coords(hwnd, pt);

        auto ret = o_WndProc_denoiser(hwnd, msg, wParam, MAKELPARAM(pt.x, pt.y), a5);

        if (wParam & MK_LBUTTON)
            wm->render(hwnd);

        return ret;
    }

    if (msg == WM_DESTROY)
    {
        const auto ret = o_WndProc_denoiser(hwnd, msg, wParam, lParam, a5);

        wm->destroy_window(hwnd);

        return ret;
    }

    if (msg == WM_ERASEBKGND)
    {
        const auto& wctx = wm->get_wctx(hwnd);

        return o_WndProc_denoiser(hwnd, msg, reinterpret_cast<WPARAM>(wctx.mem_dc), lParam, a5);
    }

    return o_WndProc_denoiser(hwnd, msg, wParam, lParam, a5);
}

/**
 * Wndproc function of the windows app volume child window for potato
 * We hook this function to handle the resizing and render logic
 */
LRESULT WNDPROC_SUB_CALL hk_WndProc_wdb(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, uint64_t a5)
{
    if (msg == WM_PAINT)
    {
        const auto ret = o_WndProc_wdb(hwnd, msg, wParam, lParam, a5);

        wm->render(hwnd);

        return ret;
    }

    if (msg == WM_CREATE)
    {
        const auto cs = reinterpret_cast<CREATESTRUCTA*>(lParam);

        wm->init_window(hwnd, WND_TYPE_WDB, cs);

        o_SetTimer(hwnd, 12346, 15, nullptr);

        wm->scale_to_main_wnd(cs->x, cs->y, cs->cx, cs->cy);

        // fix pixel gap for wdb
        cs->x -= 1;
        cs->y -= 1;
        cs->cx += 2;
        cs->cy += 2;

        MoveWindow(hwnd, cs->x, cs->y, cs->cx, cs->cy, false);

        wm->resize_d2d(hwnd, D2D1::SizeU(cs->cx, cs->cy));

        return o_WndProc_wdb(hwnd, msg, wParam, lParam, a5);
    }

    if (msg == WM_TIMER && wParam == 12346)
    {
        wm->render(hwnd);

        return 0;
    }

    if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK || msg == WM_LBUTTONUP || msg == WM_RBUTTONDOWN || msg == WM_RBUTTONDBLCLK || msg == WM_RBUTTONUP)
    {
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        wm->scale_coords(hwnd, pt);

        const auto ret = o_WndProc_wdb(hwnd, msg, wParam, MAKELPARAM(pt.x, pt.y), a5);

        if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK || msg == WM_LBUTTONUP)
            wm->render(hwnd);

        return ret;
    }

    if (msg == WM_MOUSEMOVE)
    {
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};

        wm->scale_coords(hwnd, pt);

        const auto ret = o_WndProc_wdb(hwnd, msg, wParam, MAKELPARAM(pt.x, pt.y), a5);

        if (wParam & MK_LBUTTON)
            wm->render(hwnd);

        return ret;
    }

    if (msg == WM_DESTROY)
    {
        const auto ret = o_WndProc_wdb(hwnd, msg, wParam, lParam, a5);

        wm->destroy_window(hwnd);

        return ret;
    }

    if (msg == WM_ERASEBKGND)
    {
        const auto& wctx = wm->get_wctx(hwnd);
        return o_WndProc_wdb(hwnd, msg, reinterpret_cast<WPARAM>(wctx.mem_dc), lParam, a5);
    }

    return o_WndProc_wdb(hwnd, msg, wParam, lParam, a5);
}

/**
 * We hook this function in order to get the address of WndProc from the lpWndClass pointer, so we can hook WndProc
 * See https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerclassa
 */
ATOM WINAPI hk_RegisterClassA(const WNDCLASSA* lpWndClass)
{
    if (lpWndClass->lpszClassName == window_manager::MAINWINDOW_CLASSNAME)
    {
        o_WndProc_main = lpWndClass->lpfnWndProc;

        if (!utils::hook_single_fn(&reinterpret_cast<PVOID&>(o_WndProc_main), reinterpret_cast<PVOID>(hk_WndProc_main)))
        {
            SPDLOG_ERROR("failed to hook main wndproc");
        }
    }

    return o_RegisterClassA(lpWndClass);
}

/**
 * Called when a new window is created
 * We hook this function to check for child window creation (potato) for late hooking, since the wndproc pointer is passed
 */
HWND WINAPI hk_CreateWindowExA(DWORD dwExStyle, LPCSTR lpClassName, LPCSTR lpWindowName, DWORD dwStyle, int X, int Y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam)
{
    if (lpParam == nullptr)
        return o_CreateWindowExA(dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);

    const auto lparam_info = static_cast<createwindowexa_lparam_t*>(lpParam);

    const std::string class_name = lpClassName;

    // denoiser window
    if (class_name == window_manager::COMPDENOISE_CLASSNAME_ANSI && o_WndProc_denoiser == nullptr && lparam_info->wnd_id >= 1200 && lparam_info->wnd_id <= 1204)
    {
        o_WndProc_denoiser = reinterpret_cast<o_WndProc_chldwnd_t>(lparam_info->wndproc);
        if (!utils::hook_single_fn(&reinterpret_cast<PVOID&>(o_WndProc_denoiser), reinterpret_cast<PVOID>(hk_WndProc_denoiser)))
        {
            SPDLOG_ERROR("failed to hook denoiser wndproc");
        }
    }

    // compressor window
    if (class_name == window_manager::COMPDENOISE_CLASSNAME_ANSI && o_WndProc_comp == nullptr && lparam_info->wnd_id >= 1100 && lparam_info->wnd_id <= 1104)
    {
        o_WndProc_comp = reinterpret_cast<o_WndProc_chldwnd_t>(lparam_info->wndproc);
        if (!utils::hook_single_fn(&reinterpret_cast<PVOID&>(o_WndProc_comp), reinterpret_cast<PVOID>(hk_WndProc_comp)))
        {
            SPDLOG_ERROR("failed to hook compressor wndproc");
        }
    }

    // wdb window
    if (class_name == window_manager::WDB_CLASSNAME_ANSI && o_WndProc_wdb == nullptr && lparam_info->wnd_id >= 1000 && lparam_info->wnd_id <= 1002)
    {
        o_WndProc_wdb = reinterpret_cast<o_WndProc_chldwnd_t>(lparam_info->wndproc);
        if (!utils::hook_single_fn(&reinterpret_cast<PVOID&>(o_WndProc_wdb), reinterpret_cast<PVOID>(hk_WndProc_wdb)))
        {
            SPDLOG_ERROR("failed to hook wdb wndproc");
        }
    }

    return o_CreateWindowExA(dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
}

/**
 * Displays the small "edit" boxes when a fader or label is right clicked
 * We hook this function so that the boxes are displayed at the correct location when window is resized
 * See https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-dialogboxindirectparama
 */
INT_PTR WINAPI hk_DialogBoxIndirectParamA(HINSTANCE hInstance, LPCDLGTEMPLATEA hDialogTemplate, HWND hWndParent, DLGPROC lpDialogFunc, LPARAM dwInitParam)
{
    if (dwInitParam == 0)
        return o_DialogBoxIndirectParamA(hInstance, hDialogTemplate, hWndParent, lpDialogFunc, dwInitParam);

    const auto lparam = reinterpret_cast<dialogbox_initparam_t*>(dwInitParam);

    // 2016 is some magic value for all the "edit" dialogs
    if (hWndParent == wm->get_hwnd_main() && lparam->unk2 == 2016)
    {
        POINT pt = {lparam->x, lparam->y};
        ScreenToClient(hWndParent, &pt);

        wm->scale_coords_inverse(hWndParent, pt);

        ClientToScreen(hWndParent, &pt);

        lparam->x = pt.x;
        lparam->y = pt.y;
    }

    return o_DialogBoxIndirectParamA(hInstance, hDialogTemplate, hWndParent, lpDialogFunc, dwInitParam);
}

BOOL WINAPI hk_VerQueryValueW(LPCVOID pBlock, LPCWSTR lpSubBlock, LPVOID* lplpBuffer, PUINT puLen)
{
    if (file_version_buffer.empty() || wcsncmp(lpSubBlock, L"\\StringFileInfo", 15) != 0)
        return o_VerQueryValueW(pBlock, lpSubBlock, lplpBuffer, puLen);

    // alias should be applied
    *lplpBuffer = file_version_buffer.data();
    *puLen = file_version_buffer.length();

    return TRUE;
}

BOOL WINAPI hk_GetFileVersionInfoW(LPCWSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData)
{
    file_version_buffer.clear();

    const auto alias_map = cm->get_app_aliases();

    if (!alias_map)
        return o_GetFileVersionInfoW(lptstrFilename, dwHandle, dwLen, lpData);

    const auto file_name = std::wstring(PathFindFileName(lptstrFilename));

    for (const auto& [k, v] : *alias_map)
    {
        const auto k_wstr = utils::str_to_wstr(k);
        const auto v_wstr = utils::str_to_wstr(v);

        if (!k_wstr || !v_wstr)
        {
            SPDLOG_ERROR("failed to convert string to wstring: {}, {}", k, v);
            return o_GetFileVersionInfoW(lptstrFilename, dwHandle, dwLen, lpData);
        }

        if (lstrcmpiW(k_wstr->c_str(), file_name.c_str()) != 0)
            continue;

        // alias should be applied in the next call to VerQueryValueW
        file_version_buffer = *v_wstr;
    }

    return o_GetFileVersionInfoW(lptstrFilename, dwHandle, dwLen, lpData);
}

int WINAPI hk_InternalGetWindowText(HWND hWnd, LPWSTR pString, int cchMaxCount)
{
    if (const auto use_app_name = cm->get_always_use_appname())
    {
        if (!*use_app_name)
            return o_InternalGetWindowText(hWnd, pString, cchMaxCount);

        DWORD pid;
        if (!GetWindowThreadProcessId(hWnd, &pid))
        {
            SPDLOG_ERROR("GetWindowThreadProcessId failed");
            return o_InternalGetWindowText(hWnd, pString, cchMaxCount);
        }

        const auto app_name = utils::get_exe_product_name_for_pid(pid);

        if (!app_name)
        {
            SPDLOG_ERROR("failed to get app name for pid {}", pid);
            return o_InternalGetWindowText(hWnd, pString, cchMaxCount);
        }

        wcsncpy_s(pString, cchMaxCount, app_name->c_str(), app_name->length());
        return static_cast<int>(app_name->length());
    }

    return o_InternalGetWindowText(hWnd, pString, cchMaxCount);
}

HRESULT STDMETHODCALLTYPE hk_IsSystemSoundsSession(IAudioSessionControl2* this_ptr)
{
    return o_IsSystemSoundsSession(this_ptr);
}

HRESULT STDMETHODCALLTYPE hk_GetProcessId(IAudioSessionControl2* this_ptr, DWORD* pRetVal)
{
    const auto hr = o_GetProcessId(this_ptr, pRetVal);

    if (hr != S_OK)
        return hr;

    const auto app_name = utils::get_exe_image_name_for_pid(*pRetVal);

    if (!app_name)
    {
        SPDLOG_ERROR("failed to get app name for pid {}", *pRetVal);
        return S_OK;
    }

    const auto blacklist_opt = cm->get_app_blacklist();

    if (!blacklist_opt)
        return S_OK;

    const auto& blacklist = *blacklist_opt;

    // app is blacklisted
    for (const auto& s : blacklist)
    {
        const auto wstr = utils::str_to_wstr(s);

        if (!wstr)
        {
            SPDLOG_ERROR("failed to convert to wstr");
            return S_OK;
        }

        if (lstrcmpiW(wstr->c_str(), app_name->c_str()) == 0)
        {
            *pRetVal = 0;
            return S_FALSE;
        }
    }

    return S_OK;
}

HRESULT STDMETHODCALLTYPE hk_GetSession(IAudioSessionEnumerator* this_ptr, int SessionCount, IAudioSessionControl** Session)
{
    if (o_GetProcessId || o_IsSystemSoundsSession)
        return o_GetSession(this_ptr, SessionCount, Session);

    winrt::com_ptr<IAudioSessionControl> session_control;
    winrt::com_ptr<IAudioSessionControl2> session_control2;
    try
    {
        winrt::check_hresult(o_GetSession(this_ptr, SessionCount, session_control.put()));
        winrt::check_hresult(session_control->QueryInterface(__uuidof(IAudioSessionControl2), session_control2.put_void()));
    }
    catch (const winrt::hresult_error& ex)
    {
        SPDLOG_ERROR("failed to create COM interface: {}, {}", static_cast<uint32_t>(ex.code()), winrt::to_string(ex.message()));
    }

    // IAudioSessionControl2 vtable layout:
    // -- IUnknown
    // 0: QueryInterface
    // 1: AddRef
    // 2: Release
    // -- IAudioSessionControl
    // 3: GetState
    // 4: GetDisplayName
    // 5: SetDisplayName
    // 6: GetIconPath
    // 7: SetIconPath
    // 8: GetGroupingParam
    // 9: SetGroupingParam
    // 10: RegisterAudioSessionNotification
    // 11: UnregisterAudioSessionNotification
    // -- IAudioSessionControl2
    // 12: GetSessionIdentifier
    // 13: GetSessionInstanceIdentifier
    // 14: GetProcessId
    // 15: IsSystemSoundsSession

    void** session2_control_vtable = *reinterpret_cast<void***>(session_control2.get());

    // GetProcessId has index 14
    o_GetProcessId = reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(IAudioSessionControl2* this_ptr, DWORD* pRetVal)>(session2_control_vtable[14]);
    utils::hook_single_fn(&reinterpret_cast<PVOID&>(o_GetProcessId), reinterpret_cast<PVOID>(hk_GetProcessId));

    // IsSystemSoundsSession has index 15
    o_IsSystemSoundsSession = reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(IAudioSessionControl2* this_ptr)>(session2_control_vtable[15]);
    utils::hook_single_fn(&reinterpret_cast<PVOID&>(o_IsSystemSoundsSession), reinterpret_cast<PVOID>(hk_IsSystemSoundsSession));

    return o_GetSession(this_ptr, SessionCount, Session);
}

HRESULT STDMETHODCALLTYPE hk_GetSessionEnumerator(IAudioSessionManager2* this_ptr, IAudioSessionEnumerator** SessionEnum)
{
    if (o_GetSession)
        return o_GetSessionEnumerator(this_ptr, SessionEnum);

    winrt::com_ptr<IAudioSessionEnumerator> session_enumerator;
    try
    {
        winrt::check_hresult(o_GetSessionEnumerator(this_ptr, session_enumerator.put()));
    }
    catch (const winrt::hresult_error& ex)
    {
        SPDLOG_ERROR("failed to create COM interface: {}, {}", static_cast<uint32_t>(ex.code()), winrt::to_string(ex.message()));
    }

    // GetSession has index 4 in vtable:
    // -- IUnknown
    // 0: QueryInterface
    // 1: AddRef
    // 2: Release
    // -- IAudioSessionEnumerator
    // 3: GetCount
    // 4: GetSession

    void** session_enumerator_vtable = *reinterpret_cast<void***>(session_enumerator.get());

    o_GetSession = reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(IAudioSessionEnumerator* this_ptr, int SessionCount, IAudioSessionControl** Session)>(session_enumerator_vtable[4]);
    utils::hook_single_fn(&reinterpret_cast<PVOID&>(o_GetSession), reinterpret_cast<PVOID>(hk_GetSession));

    return o_GetSessionEnumerator(this_ptr, SessionEnum);
}

HRESULT WINAPI hk_CoCreateInstance(REFCLSID rclsid, LPUNKNOWN pUnkOuter, DWORD dwClsContext, REFIID riid, LPVOID* ppv)
{
    if (!IsEqualCLSID(rclsid, __uuidof(MMDeviceEnumerator)) || !IsEqualIID(riid, __uuidof(IMMDeviceEnumerator)) || o_GetSessionEnumerator)
        return o_CoCreateInstance(rclsid, pUnkOuter, dwClsContext, riid, ppv);

    winrt::com_ptr<IMMDeviceEnumerator> device_enumerator;
    winrt::com_ptr<IMMDevice> device;
    winrt::com_ptr<IAudioSessionManager2> session_manager;
    winrt::com_ptr<IAudioSessionEnumerator> session_enumerator;

    try
    {
        winrt::check_hresult(o_CoCreateInstance(rclsid, pUnkOuter, dwClsContext, riid, device_enumerator.put_void()));
        winrt::check_hresult(device_enumerator->GetDefaultAudioEndpoint(eRender, eConsole, device.put()));
        winrt::check_hresult(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, session_manager.put_void()));
    }
    catch (const winrt::hresult_error& ex)
    {
        SPDLOG_ERROR("failed to create COM interface: {}, {}", static_cast<uint32_t>(ex.code()), winrt::to_string(ex.message()));
        return ex.code();
    }

    // IAudioSessionManager2 vtable layout
    //
    // -- IUnknown
    // 0: QueryInterface
    // 1: AddRef
    // 2: Release
    // -- IAudioSessionManager
    // 3: GetAudioSessionControl
    // 4: GetSimpleAudioVolume
    // -- IAudioSessionManager2
    // 5: GetSessionEnumerator
    // 6: RegisterSessionNotification

    void** session_manager_vtable = *reinterpret_cast<void***>(session_manager.get());

    // GetSessionEnumerator has index 5 in vtable
    o_GetSessionEnumerator = reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(IAudioSessionManager2* this_ptr, IAudioSessionEnumerator** SessionEnum)>(session_manager_vtable[5]);
    utils::hook_single_fn(&reinterpret_cast<PVOID&>(o_GetSessionEnumerator), reinterpret_cast<PVOID>(hk_GetSessionEnumerator));

    return o_CoCreateInstance(rclsid, pUnkOuter, dwClsContext, riid, ppv);
}

HANDLE WINAPI hk_OpenProcess(DWORD dwDesiredAccess, BOOL bInheritHandle, DWORD dwProcessId)
{
    // On Windows 10/11, we can strip access to only PROCESS_QUERY_LIMITED_INFORMATION 
    if (dwDesiredAccess == (SYNCHRONIZE | PROCESS_VM_READ | PROCESS_QUERY_INFORMATION))
        dwDesiredAccess = PROCESS_QUERY_LIMITED_INFORMATION;

    return o_OpenProcess(dwDesiredAccess, bInheritHandle, dwProcessId);
}

//*****************************//
//        DETOURS SETUP        //
//*****************************//

static std::vector<std::pair<PVOID*, PVOID>> hooks_base = {
    {&reinterpret_cast<PVOID&>(o_AppendMenuA), hk_AppendMenuA},
    {&reinterpret_cast<PVOID&>(o_RegisterClassA), hk_RegisterClassA},
    {&reinterpret_cast<PVOID&>(o_Rectangle), hk_Rectangle},
    {&reinterpret_cast<PVOID&>(o_BeginPaint), hk_BeginPaint},
    {&reinterpret_cast<PVOID&>(o_SetTimer), hk_SetTimer},
    {&reinterpret_cast<PVOID&>(o_GetDC), hk_GetDC},
    {&reinterpret_cast<PVOID&>(o_ReleaseDC), hk_ReleaseDC},
    {&reinterpret_cast<PVOID&>(o_SetWindowPos), hk_SetWindowPos},
    {&reinterpret_cast<PVOID&>(o_CreateWindowExA), hk_CreateWindowExA},
    {&reinterpret_cast<PVOID&>(o_DialogBoxIndirectParamA), hk_DialogBoxIndirectParamA},
    {&reinterpret_cast<PVOID&>(o_TrackPopupMenu), hk_TrackPopupMenu},
    {&reinterpret_cast<PVOID&>(o_GetClientRect), hk_GetClientRect},
    {&reinterpret_cast<PVOID&>(o_CoCreateInstance), hk_CoCreateInstance},
    {&reinterpret_cast<PVOID&>(o_InternalGetWindowText), hk_InternalGetWindowText},
    {&reinterpret_cast<PVOID&>(o_GetFileVersionInfoW), hk_GetFileVersionInfoW},
    {&reinterpret_cast<PVOID&>(o_VerQueryValueW), hk_VerQueryValueW},
    {&reinterpret_cast<PVOID&>(o_OpenProcess), hk_OpenProcess},
};

static std::vector<std::pair<PVOID*, PVOID>> hooks_theme = {
    {&reinterpret_cast<PVOID&>(o_CreateFontIndirectA), hk_CreateFontIndirectA},
    {&reinterpret_cast<PVOID&>(o_CreatePen), hk_CreatePen},
    {&reinterpret_cast<PVOID&>(o_CreateBrushIndirect), hk_CreateBrushIndirect},
    {&reinterpret_cast<PVOID&>(o_SetTextColor), hk_SetTextColor},
    {&reinterpret_cast<PVOID&>(o_CreateDIBSection), hk_CreateDIBSection},
};

/**
 * Initializes Detours hooks
 * @return True if hooks are attached successfully, false otherwise
 */
bool apply_hooks()
{
    if (DetourTransactionBegin() != NO_ERROR)
        return false;

    if (DetourUpdateThread(GetCurrentThread()) != NO_ERROR)
        return false;

    for (const auto& [original, hook] : hooks_base)
    {
        if (*original != nullptr && DetourAttach(original, hook) != NO_ERROR)
        {
            SPDLOG_ERROR("unable to hook functions");
            return false;
        }
    }

    if (cm->get_theme_enabled())
    {
        for (const auto& [original, hook] : hooks_theme)
        {
            if (*original != nullptr && DetourAttach(original, hook) != NO_ERROR)
            {
                SPDLOG_ERROR("unable to hook functions");
                return false;
            }
        }
    }

    if (DetourTransactionCommit() != NO_ERROR)
        return false;

    return true;
}

/**
 * Detours needs a single exported function with ordinal 1
 */
void dummy_export()
{
}

/**
 * DLL entry point
 * Contains only code to initialize and clean up Detours
 */
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH)
    {
        utils::attach_console_debug();
        return utils::hook_single_fn(&reinterpret_cast<PVOID&>(o_CreateMutexA), reinterpret_cast<PVOID>(hk_CreateMutexA));
    }

    return TRUE;
}
