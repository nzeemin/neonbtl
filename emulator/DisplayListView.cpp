/*  This file is part of NEONBTL.
NEONBTL is free software: you can redistribute it and/or modify it under the terms
of the GNU Lesser General Public License as published by the Free Software Foundation,
either version 3 of the License, or (at your option) any later version.
NEONBTL is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU Lesser General Public License for more details.
You should have received a copy of the GNU Lesser General Public License along with
NEONBTL. If not, see <http://www.gnu.org/licenses/>. */

// DisplayListView.cpp

#include "stdafx.h"
#include <CommCtrl.h>
#include "Main.h"
#include "Views.h"
#include "ToolWindow.h"
#include "Emulator.h"
#include "emubase/Emubase.h"

//////////////////////////////////////////////////////////////////////

HWND g_hwndDisplayList = (HWND)INVALID_HANDLE_VALUE;  // DisplayList view window handler
WNDPROC m_wndprocDisplayListToolWindow = NULL;  // Old window proc address of the ToolWindow

HWND m_hwndDisplayListViewer = (HWND)INVALID_HANDLE_VALUE;

HWND m_hwndDisplayListTreeView = (HWND)INVALID_HANDLE_VALUE;

void DiaplayList_FillTreeView();


//////////////////////////////////////////////////////////////////////

void DisplayListView_RegisterClass()
{
    WNDCLASSEX wcex;
    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = DisplayListViewViewerWndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = g_hInst;
    wcex.hIcon = NULL;
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = NULL;
    wcex.lpszClassName = CLASSNAME_DISPLAYLISTVIEW;
    wcex.hIconSm = NULL;

    RegisterClassEx(&wcex);
}

void DisplayListView_Create(int x, int y)
{
    int width = 480;
    int height = 600;

    g_hwndDisplayList = CreateWindowEx(
            WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
            CLASSNAME_OVERLAPPEDWINDOW, _T("Display List Viewer"),
            WS_POPUPWINDOW | WS_CAPTION | WS_VISIBLE,
            x, y, width, height,
            NULL, NULL, g_hInst, NULL);

    // ToolWindow subclassing
    m_wndprocDisplayListToolWindow = (WNDPROC)LongToPtr(SetWindowLongPtr(
            g_hwndDisplayList, GWLP_WNDPROC, PtrToLong(DisplayListViewWndProc)));

    RECT rcClient;  GetClientRect(g_hwndDisplayList, &rcClient);

    m_hwndDisplayListTreeView = CreateWindowEx(
            0, WC_TREEVIEW, NULL,
            WS_VISIBLE | WS_CHILD | WS_BORDER | TVS_HASLINES | TVS_HASBUTTONS | TVS_LINESATROOT | TVS_SHOWSELALWAYS,
            0, 0, rcClient.right, rcClient.bottom,
            g_hwndDisplayList, NULL, g_hInst, NULL);

    HFONT hFont = CreateMonospacedFont();
    ::SendMessage(m_hwndDisplayListTreeView, WM_SETFONT, (WPARAM)hFont, NULL);

    DiaplayList_FillTreeView();
    ::SetFocus(m_hwndDisplayListViewer);
}

LRESULT CALLBACK DisplayListViewWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_SETFOCUS:
        ::SetFocus(m_hwndDisplayListViewer);
        break;
    case WM_DESTROY:
        g_hwndDisplayList = (HWND)INVALID_HANDLE_VALUE;  // We are closed! Bye-bye!..
        return CallWindowProc(m_wndprocDisplayListToolWindow, hWnd, message, wParam, lParam);
    default:
        return CallWindowProc(m_wndprocDisplayListToolWindow, hWnd, message, wParam, lParam);
    }
    return (LRESULT)FALSE;
}

LRESULT CALLBACK DisplayListViewViewerWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_DESTROY:
        // Free resources
        return DefWindowProc(hWnd, message, wParam, lParam);
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
        SetFocus(hWnd);
        break;
        //case WM_KEYDOWN:
        //    return (LRESULT)DisplayListView_OnKeyDown(wParam, lParam);
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return (LRESULT)FALSE;
}

void DiaplayList_FillTreeView()
{
    const CMotherboard* pBoard = g_pBoard;
    uint16_t vdptaslo = pBoard->GetRAMWordView(0000010);  // VDPTAS
    uint16_t vdptashi = pBoard->GetRAMWordView(0000012);  // VDPTAS
    uint16_t vdptaplo = pBoard->GetRAMWordView(0000004);  // VDPTAP
    uint16_t vdptaphi = pBoard->GetRAMWordView(0000006);  // VDPTAP
    uint32_t tasaddr = (((uint32_t)vdptaslo) << 2) | (((uint32_t)(vdptashi & 0x000f)) << 18);

    const size_t buffersize = 256 - 1;
    TCHAR buffer[buffersize + 1];
    _sntprintf(buffer, buffersize, _T("VDPTAS=%06o:%06o VDPTAP=%06o:%06o"), vdptashi, vdptaslo, vdptaphi, vdptaplo);

    TVITEM item = {};
    item.mask = TVIF_TEXT;
    item.cchTextMax = buffersize;
    item.pszText = buffer;

    TVINSERTSTRUCT tvins = {};
    tvins.hParent = TVI_ROOT;
    tvins.hInsertAfter = NULL;
    tvins.item = item;
    HTREEITEM hRoot = (HTREEITEM)::SendMessage(m_hwndDisplayListTreeView, TVM_INSERTITEM, 0, (LPARAM)&tvins);

    for (int line = -2; line < 300; line++)  // Цикл по строкам -2..299, первые две строки не видны
    {
        uint16_t linelo = pBoard->GetRAMWordView(tasaddr);
        uint16_t linehi = pBoard->GetRAMWordView(tasaddr + 2);
        tasaddr += 4;

        _sntprintf(buffer, buffersize, _T("Line %d: %06o:%06o"), line, linehi, linelo);
        tvins.hParent = hRoot;
        HTREEITEM hLine = (HTREEITEM)SendMessage(m_hwndDisplayListTreeView, TVM_INSERTITEM, 0, (LPARAM)&tvins);

        uint32_t lineaddr = (((uint32_t)linelo) << 2) | (((uint32_t)(linehi & 0x000f)) << 18);
        bool firstOtr = true;  // Признак первого отрезка в строке
        int bar = 52;  // Счётчик полосок от 52 к 0
        for (int otrno = 1; ; otrno++)  // Цикл по видеоотрезкам строки, до полного заполнения строки
        {
            uint16_t otrlo = pBoard->GetRAMWordView(lineaddr);
            uint16_t otrhi = pBoard->GetRAMWordView(lineaddr + 2);
            lineaddr += 4;
            // Получаем параметры отрезка
            int otrcount = 32 - (otrhi >> 10) & 037;  // Длина отрезка в 32-разрядных словах
            if (otrcount == 0) otrcount = 32;
            // Определяем, сколько 16-пиксельных полосок нужно заполнить
            int barcount = otrcount * 2;
            if (!firstOtr) barcount--;
            if (barcount > bar) barcount = bar;
            bar -= barcount;

            uint16_t vd = (otrhi >> 8) & 3;  // биты VD1 VD0
            LPCTSTR vdstr;
            switch (vd)
            {
            case 3:  vdstr = _T("vd208"); break;
            case 2:  vdstr = _T("vd104"); break;
            case 1:  vdstr = _T("vd52 "); break;
            default: vdstr = _T("vd52s"); break;
            }
            uint16_t vm = (otrhi & 0x8000) >> 13 | (otrhi >> 6) & 3;  // биты PB VN1 VN0
            LPCTSTR vmstr;
            switch (vm)
            {
            case 0: case 4: vmstr = _T("VM1 "); break;
            case 1: case 5: vmstr = _T("VM2 "); break;
            case 2:  vmstr = _T("VM40"); break;
            case 3:  vmstr = _T("VM41"); break;
            case 6:  vmstr = _T("VM4 "); break;
            default: vmstr = _T("VM8 "); break;
            }
            uint16_t palno = (otrhi >> 4) & 3;  // PN1 PN0 - номер палитры

            _sntprintf(buffer, buffersize, _T("%d: %06o:%06o dwlen:%d %s %s pal:%d"), otrno, otrhi, otrlo, otrcount, vdstr, vmstr, palno);
            tvins.hParent = hLine;
            ::SendMessage(m_hwndDisplayListTreeView, TVM_INSERTITEM, 0, (LPARAM)&tvins);

            if (bar <= 0) break;
            firstOtr = false;
        }
    }

    ::SendMessage(m_hwndDisplayListTreeView, TVM_EXPAND, TVE_EXPAND, (LPARAM)hRoot);
}

//////////////////////////////////////////////////////////////////////
