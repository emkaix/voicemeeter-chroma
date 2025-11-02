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

extern HANDLE (WINAPI *o_CreateMutexA)(LPSECURITY_ATTRIBUTES lpMutexAttributes, BOOL bInitialOwner, LPCSTR lpName);
extern HFONT (WINAPI *o_CreateFontIndirectA)(const LOGFONTA* lplf);
extern BOOL (WINAPI *o_AppendMenuA)(HMENU hMenu, UINT uFlags, UINT_PTR uIDNewItem, LPCSTR lpNewItem);
extern HPEN (WINAPI *o_CreatePen)(int iStyle, int cWidth, COLORREF color);
extern HBRUSH (WINAPI *o_CreateBrushIndirect)(const LOGBRUSH* plbrush);
extern COLORREF (WINAPI *o_SetTextColor)(HDC hdc, COLORREF color);
extern ATOM (WINAPI *o_RegisterClassA)(const WNDCLASSA* lpWndClass);
extern BOOL (WINAPI *o_Rectangle)(HDC hdc, int left, int top, int right, int bottom);
extern HBITMAP (WINAPI *o_CreateDIBSection)(HDC hdc, const BITMAPINFO* pbmi, UINT usage, void** ppvBits, HANDLE hSection, DWORD offset);
extern HDC (WINAPI *o_BeginPaint)(HWND hWnd, LPPAINTSTRUCT lpPaint);
extern UINT_PTR (WINAPI *o_SetTimer)(HWND hWnd, UINT_PTR nIDEvent, UINT uElapse, TIMERPROC lpTimerFunc);
extern HDC (WINAPI *o_GetDC)(HWND hWnd);
extern int (WINAPI *o_ReleaseDC)(HWND hWnd, HDC hDC);
extern BOOL (WINAPI *o_SetWindowPos)(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags);
extern BOOL (WINAPI *o_TrackPopupMenu)(HMENU hMenu, UINT uFlags, int x, int y, int nReserved, HWND hWnd, const RECT* prcRect);
extern BOOL (WINAPI *o_GetClientRect)(HWND hWnd, LPRECT lpRect);
extern HWND (WINAPI *o_CreateWindowExA)(DWORD dwExStyle, LPCSTR lpClassName, LPCSTR lpWindowName, DWORD dwStyle, int X, int Y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam);
extern INT_PTR (WINAPI *o_DialogBoxIndirectParamA)(HINSTANCE hInstance, LPCDLGTEMPLATEA hDialogTemplate, HWND hWndParent, DLGPROC lpDialogFunc, LPARAM dwInitParam);
extern HRESULT (WINAPI *o_CoCreateInstance)(REFCLSID rclsid, LPUNKNOWN pUnkOuter, DWORD dwClsContext, REFIID riid, LPVOID* ppv);
extern int (WINAPI *o_InternalGetWindowText)(HWND hWnd, LPWSTR pString, int cchMaxCount);
extern BOOL (WINAPI *o_GetFileVersionInfoW)(LPCWSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData);
extern BOOL (WINAPI *o_VerQueryValueW)(LPCVOID pBlock, LPCWSTR lpSubBlock, LPVOID* lplpBuffer, PUINT puLen);
extern HANDLE (WINAPI *o_OpenProcess)(DWORD dwDesiredAccess, BOOL bInheritHandle, DWORD dwProcessId);
