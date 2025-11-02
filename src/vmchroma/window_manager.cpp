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

#include "window_manager.hpp"
#include "winapi_hook_defs.hpp"
#include "spdlog/spdlog.h"

using namespace winrt::Windows;
using namespace winrt::Windows::Graphics::Display;

/**
 * Initializes the Direct2D context
 */
window_manager::window_manager()
{
    try
    {
        winrt::check_hresult(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d_factory.put()));

        const UINT creation_flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_SINGLETHREADED;

        const D3D_FEATURE_LEVEL feature_levels[] = {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0,
        };

        winrt::check_hresult(D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            creation_flags,
            feature_levels,
            4,
            D3D11_SDK_VERSION,
            d3d_device.put(),
            nullptr,
            nullptr
        ));

        dxgi_device = d3d_device.as<IDXGIDevice>();
        winrt::check_hresult(dxgi_device->GetAdapter(adapter.put()));
        dxgi_factory.capture(adapter, &IDXGIAdapter::GetParent);
        winrt::check_hresult(d2d_factory->CreateDevice(dxgi_device.get(), d2d_device.put()));
    }
    catch (const winrt::hresult_error& ex)
    {
        SPDLOG_ERROR("failed to init directx context: {}, {}", static_cast<uint32_t>(ex.code()), winrt::to_string(ex.message()));
    }
}

HWND window_manager::get_hwnd_main() const
{
    return hwnd_main;
}

void window_manager::set_hwnd_main(HWND hwnd)
{
    hwnd_main = hwnd;
}

window_ctx_t& window_manager::get_wctx(HWND hwnd)
{
    return wctx_map[hwnd];
}

/**
 * Called in WM_CREATE, sets up the Direct2D rendering for that window
 * @param hwnd The hwnd of the window
 * @param type Main window or child window
 * @param cs CreateStruct pointer passed via WM_CREATE
 * @return True if init was successful
 */
bool window_manager::init_window(HWND hwnd, const WND_TYPE type, const CREATESTRUCTA* cs)
{
    window_ctx_t wctx = {};
    wctx.default_cx = cs->cx;
    wctx.default_cy = cs->cy;
    wctx.default_x = cs->x;
    wctx.default_y = cs->y;
    wctx.hwnd = hwnd;
    wctx.type = type;

    D3D11_TEXTURE2D_DESC tex_desc = {};
    tex_desc.Width = cs->cx;
    tex_desc.Height = cs->cy;
    tex_desc.MipLevels = 1;
    tex_desc.ArraySize = 1;
    tex_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    tex_desc.SampleDesc.Count = 1;
    tex_desc.Usage = D3D11_USAGE_DEFAULT;
    tex_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    tex_desc.MiscFlags = D3D11_RESOURCE_MISC_GDI_COMPATIBLE;
    tex_desc.CPUAccessFlags = 0;

    DXGI_SWAP_CHAIN_DESC1 swap_chain_desc = {};
    swap_chain_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swap_chain_desc.Width = cs->cx;
    swap_chain_desc.Height = cs->cy;
    swap_chain_desc.SampleDesc.Count = 1;
    swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap_chain_desc.BufferCount = 2;
    swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swap_chain_desc.Scaling = DXGI_SCALING_STRETCH;
    swap_chain_desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    try
    {
        winrt::check_hresult(d3d_device->CreateTexture2D(&tex_desc, nullptr, wctx.source_texture.put()));
        winrt::check_hresult(wctx.source_texture->QueryInterface(__uuidof(IDXGISurface1), wctx.source_surface.put_void()));
        winrt::check_hresult(d2d_device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, wctx.d2d_context.put()));

        winrt::check_hresult(wctx.d2d_context->CreateBitmapFromDxgiSurface(
            wctx.source_surface.get(),
            &source_bitmap_props,
            wctx.source_bitmap.put()
        ));

        winrt::check_hresult(wctx.source_surface->GetDC(FALSE, &wctx.mem_dc));

        winrt::check_hresult(dxgi_factory->CreateSwapChainForHwnd(
            d3d_device.get(),
            hwnd,
            &swap_chain_desc,
            nullptr,
            nullptr,
            wctx.swap_chain.put()
        ));

        winrt::check_hresult(dxgi_factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER));

        winrt::com_ptr<IDXGISurface1> swap_chain_surface;
        winrt::check_hresult(wctx.swap_chain->GetBuffer(0, __uuidof(IDXGISurface1), swap_chain_surface.put_void()));

        winrt::check_hresult(wctx.d2d_context->CreateBitmapFromDxgiSurface(
            swap_chain_surface.get(),
            &target_bitmap_props,
            wctx.target_bitmap.put()
        ));

        wctx.d2d_context->SetTarget(wctx.target_bitmap.get());
    }
    catch (const winrt::hresult_error& ex)
    {
        SPDLOG_ERROR("failed to initialize window: {}, {}", static_cast<uint32_t>(ex.code()), winrt::to_string(ex.message()));
        return false;
    }

    wctx_map[hwnd] = wctx;

    return true;
}

/**
 * Called in WM_DESTROY, releases the Direct2D context
 * @param hwnd The hwnd of the window
 */
void window_manager::destroy_window(HWND hwnd)
{
    const auto& wctx = wctx_map[hwnd];

    try
    {
        winrt::check_hresult((wctx.source_surface->ReleaseDC(nullptr)));
    }
    catch (const winrt::hresult_error& ex)
    {
        SPDLOG_ERROR("failed to destroy window: {}, {}", static_cast<uint32_t>(ex.code()), winrt::to_string(ex.message()));
    }

    DeleteDC(wctx.mem_dc);
    wctx_map.erase(hwnd);
}

/**
 * Draws the content of the memory DC to the window
 * @param hwnd The hwnd of the window
 */
void window_manager::render(HWND hwnd)
{
    auto& wctx = wctx_map[hwnd];

    try
    {
        GdiFlush();

        winrt::check_hresult(wctx.source_surface->ReleaseDC(nullptr));

        RECT rc;
        o_GetClientRect(wctx.hwnd, &rc);

        const float scaleX = rc.right / static_cast<float>(wctx.default_cx);
        const float scaleY = rc.bottom / static_cast<float>(wctx.default_cy);

        wctx.d2d_context->BeginDraw();

        wctx.d2d_context->SetTransform(D2D1::Matrix3x2F::Scale(scaleX, scaleY));

        wctx.d2d_context->DrawImage(
            wctx.source_bitmap.get(),
            D2D1::Point2F(0, 0),
            D2D1::RectF(0, 0, static_cast<float>(wctx.default_cx), static_cast<float>(wctx.default_cy)),
            D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC,
            D2D1_COMPOSITE_MODE_SOURCE_COPY
        );

        winrt::check_hresult(wctx.d2d_context->EndDraw());

        winrt::check_hresult(wctx.swap_chain->Present(1, 0));

        wctx.mem_dc = nullptr;
        wctx.source_bitmap = nullptr;

        winrt::check_hresult(wctx.d2d_context->CreateBitmapFromDxgiSurface(
            wctx.source_surface.get(),
            &source_bitmap_props,
            wctx.source_bitmap.put()
        ));

        winrt::check_hresult(wctx.source_surface->GetDC(FALSE, &wctx.mem_dc));
    }
    catch (const winrt::hresult_error& ex)
    {
        SPDLOG_ERROR("render error: {}, {}", static_cast<uint32_t>(ex.code()), winrt::to_string(ex.message()));
    }
}

void window_manager::set_cur_main_wnd_size(int w, int h)
{
    cur_main_width = w;
    cur_main_height = h;
}

void window_manager::get_cur_main_wnd_size(int& w, int& h) const
{
    w = cur_main_width;
    h = cur_main_height;
}

void window_manager::set_default_main_wnd_size(int w, int h)
{
    default_main_width = w;
    default_main_height = h;
}

void window_manager::get_default_main_wnd_size(int& w, int& h) const
{
    w = default_main_width;
    h = default_main_height;
}

/**
 * Called when window size changes, recreates the Direct2D context for the new window dimensions
 * @param hwnd The hwnd of the window
 * @param pixelSize The new dimensions of the window
 */
void window_manager::resize_d2d(HWND hwnd, const D2D1_SIZE_U& pixelSize)
{
    auto& wctx = wctx_map[hwnd];

    wctx.d2d_context->SetTarget(nullptr);
    wctx.target_bitmap = nullptr;

    try
    {
        winrt::check_hresult(wctx.swap_chain->ResizeBuffers(
            0, pixelSize.width, pixelSize.height, DXGI_FORMAT_B8G8R8A8_UNORM, 0
        ));

        winrt::com_ptr<IDXGISurface1> swap_chain_surface;
        winrt::check_hresult(wctx.swap_chain->GetBuffer(0, __uuidof(IDXGISurface1), swap_chain_surface.put_void()));

        winrt::check_hresult(wctx.d2d_context->CreateBitmapFromDxgiSurface(
            swap_chain_surface.get(),
            &target_bitmap_props,
            wctx.target_bitmap.put()
        ));
    }
    catch (const winrt::hresult_error& ex)
    {
        SPDLOG_ERROR("failed to resize window: {}, {}", static_cast<uint32_t>(ex.code()), winrt::to_string(ex.message()));
    }

    wctx.d2d_context->SetTarget(wctx.target_bitmap.get());
}

bool window_manager::is_in_map(HWND hwnd)
{
    return wctx_map.find(hwnd) != wctx_map.end();
}

void window_manager::scale_coords(HWND hwnd, POINT& pt)
{
    RECT cur_rc;
    o_GetClientRect(hwnd, &cur_rc);

    const auto& wctx = wctx_map[hwnd];

    pt.x = MulDiv(pt.x, wctx.default_cx, cur_rc.right);
    pt.y = MulDiv(pt.y, wctx.default_cy, cur_rc.bottom);
}

void window_manager::scale_coords_inverse(HWND hwnd, POINT& pt)
{
    RECT cur_rc;
    o_GetClientRect(hwnd, &cur_rc);

    const auto& wctx = wctx_map[hwnd];

    pt.x = MulDiv(pt.x, cur_rc.right, wctx.default_cx);
    pt.y = MulDiv(pt.y, cur_rc.bottom, wctx.default_cy);
}

void window_manager::scale_to_main_wnd(int& x, int& y, int& cx, int& cy)
{
    x = MulDiv(x, cur_main_width, default_main_width);
    y = MulDiv(y, cur_main_height, default_main_height);
    cx = MulDiv(cx, cur_main_width, default_main_width);
    cy = MulDiv(cy, cur_main_height, default_main_height);
}

void window_manager::resize_child_windows()
{
    for (const auto& [hwnd, wctx] : wctx_map)
    {
        if (hwnd == hwnd_main)
            continue;

        int x = wctx.default_x;
        int y = wctx.default_y;
        int cx = wctx.default_cx;
        int cy = wctx.default_cy;

        scale_to_main_wnd(x, y, cx, cy);

        if (wctx.type == WND_TYPE_WDB)
        {
            // fix pixel gap for wdb
            x -= 1;
            y -= 1;
            cx += 2;
            cy += 2;
        }

        MoveWindow(hwnd, x, y, cx, cy, false);
        resize_d2d(hwnd, D2D1::SizeU(cx, cy));
    }
}
