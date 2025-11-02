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


#include <string_view>
#include <unordered_map>
#include <windows.h>
#include <d2d1_1.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <winrt/Windows.Graphics.Display.h>


const enum WND_TYPE { WND_TYPE_MAIN, WND_TYPE_COMP_DENOISE, WND_TYPE_WDB };

typedef struct window_ctx
{
    int32_t default_cx;
    int32_t default_cy;
    int32_t default_x;
    int32_t default_y;
    HDC mem_dc;
    HWND hwnd;
    WND_TYPE type;
    winrt::com_ptr<IDXGISwapChain1> swap_chain;
    winrt::com_ptr<ID2D1DeviceContext> d2d_context;
    winrt::com_ptr<ID2D1Bitmap1> target_bitmap;
    winrt::com_ptr<ID2D1Bitmap1> source_bitmap;
    winrt::com_ptr<ID3D11Texture2D> source_texture;
    winrt::com_ptr<IDXGISurface1> source_surface;
} window_ctx_t;

class window_manager
{
private:
    HWND hwnd_main = nullptr;
    uint32_t ui_update_timer = 0;
    std::unordered_map<HWND, window_ctx_t> wctx_map;
    int32_t cur_main_width = 0;
    int32_t cur_main_height = 0;
    int32_t default_main_height = 0;
    int32_t default_main_width = 0;
    winrt::com_ptr<ID2D1Factory1> d2d_factory;
    winrt::com_ptr<ID2D1Device> d2d_device;
    winrt::com_ptr<ID3D11Device> d3d_device = nullptr;
    winrt::com_ptr<IDXGIDevice> dxgi_device;
    winrt::com_ptr<IDXGIAdapter> adapter;
    winrt::com_ptr<IDXGIFactory2> dxgi_factory;
    D2D1_BITMAP_PROPERTIES1 target_bitmap_props = {
        {DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE},
        96.0f, 96.0f,
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        nullptr
    };
    D2D1_BITMAP_PROPERTIES1 source_bitmap_props = {
        {DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE},
        96.0f, 96.0f,
        D2D1_BITMAP_OPTIONS_NONE,
        nullptr
    };

public:
    window_manager();
    static constexpr std::string_view MAINWINDOW_CLASSNAME = "VBCABLE0Voicemeeter0MainWindow0";
    static constexpr std::wstring_view MAINWINDOW_CLASSNAME_UNICODE = L"VBCABLE0Voicemeeter0MainWindow0";
    static constexpr std::string_view COMPDENOISE_CLASSNAME_ANSI = "C_VB2CTL_Free_00\xA9VBurel";
    static constexpr std::wstring_view COMPDENOISE_CLASSNAME_UNICODE = L"C_VB2CTL_Free_00©VBurel";
    static constexpr std::string_view WDB_CLASSNAME_ANSI = "C_VB2CTL_Free_00_wdb\xA9VBurel";
    static constexpr std::wstring_view WDB_CLASSNAME_UNICODE = L"C_VB2CTL_Free_00_wdb©VBurel";
    HWND get_hwnd_main() const;
    void set_hwnd_main(HWND);
    window_ctx_t& get_wctx(HWND hwnd);
    bool init_window(HWND hwnd, WND_TYPE type, const CREATESTRUCTA* cs);
    void destroy_window(HWND);
    void render(HWND hwnd);
    void set_cur_main_wnd_size(int w, int h);
    void get_cur_main_wnd_size(int& w, int& h) const;
    void set_default_main_wnd_size(int w, int h);
    void get_default_main_wnd_size(int& w, int& h) const;
    void resize_d2d(HWND hwnd, const D2D1_SIZE_U& pixelSize);
    bool is_in_map(HWND hwnd);
    void scale_coords(HWND hwnd, POINT& pt);
    void scale_coords_inverse(HWND hwnd, POINT& pt);
    void scale_to_main_wnd(int& x, int& y, int& cx, int& cy);
    void resize_child_windows();
};
