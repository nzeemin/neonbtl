﻿/*  This file is part of NEONBTL.
    NEONBTL is free software: you can redistribute it and/or modify it under the terms
of the GNU Lesser General Public License as published by the Free Software Foundation,
either version 3 of the License, or (at your option) any later version.
    NEONBTL is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU Lesser General Public License for more details.
    You should have received a copy of the GNU Lesser General Public License along with
NEONBTL. If not, see <http://www.gnu.org/licenses/>. */

// DebugView.cpp

#include "stdafx.h"
#include <windowsx.h>
#include <CommCtrl.h>
#include "Main.h"
#include "Views.h"
#include "ToolWindow.h"
#include "Emulator.h"
#include "emubase/Emubase.h"

//////////////////////////////////////////////////////////////////////


HWND g_hwndDebug = (HWND)INVALID_HANDLE_VALUE;  // Debug View window handle
WNDPROC m_wndprocDebugToolWindow = NULL;  // Old window proc address of the ToolWindow

HWND m_hwndDebugToolbar = (HWND)INVALID_HANDLE_VALUE;
HWND m_hwndDebugProcViewer = (HWND)INVALID_HANDLE_VALUE;
HWND m_hwndDebugStackViewer = (HWND)INVALID_HANDLE_VALUE;
HWND m_hwndDebugPortsViewer = (HWND)INVALID_HANDLE_VALUE;
HWND m_hwndDebugBreaksViewer = (HWND)INVALID_HANDLE_VALUE;
HWND m_hwndDebugMemoryViewer = (HWND)INVALID_HANDLE_VALUE;

WORD m_wDebugCpuR[11];  // Old register values - R0..R7, PSW
BOOL m_okDebugCpuRChanged[11];  // Register change flags
WORD m_wDebugCpuPswOld = 0;  // PSW value on previous step
WORD m_wDebugCpuR6Old = 0;  // SP value on previous step


//////////////////////////////////////////////////////////////////////

void DebugView_UpdateWindowText();
BOOL DebugView_OnKeyDown(WPARAM vkey, LPARAM lParam);

void DebugProcView_DoDraw(HDC hdc);
void DebugView_DrawProcessor(HDC hdc, const CProcessor* pProc, int x, int y, WORD* arrR, BOOL* arrRChanged, WORD oldPsw);

void DebugStackView_DoDraw(HDC hdc);
void DebugView_DrawMemoryForRegister(HDC hdc, int reg, const CProcessor* pProc, int x, int y, WORD oldValue);

void DebugPortsView_DoDraw(HDC hdc);
void DebugView_DrawPorts(HDC hdc, int x, int y);
int  DebugView_DrawWatches(HDC hdc, const CProcessor* pProc, int x, int y);

void DebugBreaksView_DoDraw(HDC hdc);
void DebugBreaksView_OnRButtonDown(int mousex, int mousey);

void DebugMemoryView_DoDraw(HDC hdc);
void DebugView_DrawMemoryMap(HDC hdc, int x, int y, const CProcessor* pProc);
void DebugView_DrawHRandUR(HDC hdc, int x, int y);


//////////////////////////////////////////////////////////////////////


void DebugView_RegisterClasses()
{
    WNDCLASSEX wcex;
    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = g_hInst;
    wcex.hIcon          = NULL;
    wcex.hCursor        = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName   = NULL;
    wcex.hIconSm        = NULL;

    wcex.lpszClassName = CLASSNAME_DEBUGPROCVIEW;
    wcex.lpfnWndProc = DebugProcViewViewerWndProc;
    RegisterClassEx(&wcex);

    wcex.lpszClassName = CLASSNAME_DEBUGSTACKVIEW;
    wcex.lpfnWndProc = DebugStackViewViewerWndProc;
    RegisterClassEx(&wcex);

    wcex.lpszClassName = CLASSNAME_DEBUGPORTSVIEW;
    wcex.lpfnWndProc = DebugPortsViewViewerWndProc;
    RegisterClassEx(&wcex);

    wcex.lpszClassName = CLASSNAME_DEBUGBREAKSVIEW;
    wcex.lpfnWndProc = DebugBreaksViewViewerWndProc;
    RegisterClassEx(&wcex);

    wcex.lpszClassName = CLASSNAME_DEBUGMEMORYVIEW;
    wcex.lpfnWndProc = DebugMemoryViewViewerWndProc;
    RegisterClassEx(&wcex);
}

void DebugView_Init()
{
    memset(m_wDebugCpuR, 0, sizeof(m_wDebugCpuR));
    memset(m_okDebugCpuRChanged, 0, sizeof(m_okDebugCpuRChanged));
    m_wDebugCpuPswOld = 0;
    m_wDebugCpuR6Old = 0;
}

void DebugView_Create(HWND hwndParent, int x, int y, int width, int height)
{
    ASSERT(hwndParent != NULL);

    g_hwndDebug = CreateWindow(
            CLASSNAME_TOOLWINDOW, NULL,
            WS_CHILD | WS_VISIBLE,
            x, y, width, height,
            hwndParent, NULL, g_hInst, NULL);
    DebugView_UpdateWindowText();

    // ToolWindow subclassing
    m_wndprocDebugToolWindow = (WNDPROC)LongToPtr( SetWindowLongPtr(
            g_hwndDebug, GWLP_WNDPROC, PtrToLong(DebugViewWndProc)) );

    RECT rcClient;  GetClientRect(g_hwndDebug, &rcClient);

    m_hwndDebugProcViewer = CreateWindowEx(
            0,
            CLASSNAME_DEBUGPROCVIEW, NULL,
            WS_CHILD | WS_VISIBLE,
            0, 0, rcClient.right, rcClient.bottom,
            g_hwndDebug, NULL, g_hInst, NULL);

    m_hwndDebugStackViewer = CreateWindowEx(
            0,
            CLASSNAME_DEBUGSTACKVIEW, NULL,
            WS_CHILD | WS_VISIBLE,
            0, 0, rcClient.right, rcClient.bottom,
            g_hwndDebug, NULL, g_hInst, NULL);

    m_hwndDebugPortsViewer = CreateWindowEx(
            0,
            CLASSNAME_DEBUGPORTSVIEW, NULL,
            WS_CHILD | WS_VISIBLE,
            0, 0, rcClient.right, rcClient.bottom,
            g_hwndDebug, NULL, g_hInst, NULL);

    m_hwndDebugBreaksViewer = CreateWindowEx(
            0,
            CLASSNAME_DEBUGBREAKSVIEW, NULL,
            WS_CHILD | WS_VISIBLE,
            0, 0, rcClient.right, rcClient.bottom,
            g_hwndDebug, NULL, g_hInst, NULL);

    m_hwndDebugMemoryViewer = CreateWindowEx(
            0,
            CLASSNAME_DEBUGMEMORYVIEW, NULL,
            WS_CHILD | WS_VISIBLE,
            0, 0, rcClient.right, rcClient.bottom,
            g_hwndDebug, NULL, g_hInst, NULL);

    m_hwndDebugToolbar = CreateWindowEx(0, TOOLBARCLASSNAME, NULL,
            WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TRANSPARENT | TBSTYLE_TOOLTIPS | CCS_NOPARENTALIGN | CCS_NODIVIDER | CCS_VERT,
            4, 4, 32, rcClient.bottom, m_hwndDebugProcViewer,
            (HMENU)102,
            g_hInst, NULL);

    TBADDBITMAP addbitmap;
    addbitmap.hInst = g_hInst;
    addbitmap.nID = IDB_TOOLBAR;
    SendMessage(m_hwndDebugToolbar, TB_ADDBITMAP, 2, (LPARAM)&addbitmap);

    SendMessage(m_hwndDebugToolbar, TB_BUTTONSTRUCTSIZE, (WPARAM)sizeof(TBBUTTON), 0);
    SendMessage(m_hwndDebugToolbar, TB_SETBUTTONSIZE, 0, (LPARAM)MAKELONG(26, 26));

    TBBUTTON buttons[3];
    ZeroMemory(buttons, sizeof(buttons));
    for (size_t i = 0; i < sizeof(buttons) / sizeof(TBBUTTON); i++)
    {
        buttons[i].fsState = TBSTATE_ENABLED | TBSTATE_WRAP;
        buttons[i].fsStyle = BTNS_BUTTON;
        buttons[i].iString = -1;
    }
    buttons[0].idCommand = ID_VIEW_DEBUG;
    buttons[0].iBitmap = ToolbarImageDebugger;
    buttons[0].fsState = TBSTATE_ENABLED | TBSTATE_WRAP | TBSTATE_CHECKED;
    buttons[1].idCommand = ID_DEBUG_STEPINTO;
    buttons[1].iBitmap = ToolbarImageStepInto;
    buttons[2].idCommand = ID_DEBUG_STEPOVER;
    buttons[2].iBitmap = ToolbarImageStepOver;

    SendMessage(m_hwndDebugToolbar, TB_ADDBUTTONS, (WPARAM) sizeof(buttons) / sizeof(TBBUTTON), (LPARAM)&buttons);
}

void DebugView_Redraw()
{
    RedrawWindow(g_hwndDebug, NULL, NULL, RDW_ERASE | RDW_INVALIDATE | RDW_ALLCHILDREN);
}

// Adjust position of client windows
void DebugView_AdjustWindowLayout()
{
    RECT rc;  GetClientRect(g_hwndDebug, &rc);

    // Get cxChar, cyLine
    HDC hdc = ::GetDC(g_hwndDebug);
    HFONT hFont = CreateMonospacedFont();
    HGDIOBJ hOldFont = SelectObject(hdc, hFont);
    int cxChar, cyLine;  GetFontWidthAndHeight(hdc, &cxChar, &cyLine);
    SelectObject(hdc, hOldFont);
    VERIFY(::DeleteObject(hFont));
    ::ReleaseDC(g_hwndDebug, hdc);

    int cxDebug = 32 + 4 + cxChar * 33;
    int cxStack = cxChar * 17 + cxChar / 2;
    int cxPorts = cxChar * 22;
    int cxBreaks = cxChar * 9;
    int cxMemory = 4 + cxChar * 24 + 4 + cxChar * 12 + 4;

    int xDebug = 0;
    if (m_hwndDebugProcViewer != (HWND)INVALID_HANDLE_VALUE)
        SetWindowPos(m_hwndDebugProcViewer, NULL, xDebug, 0, cxDebug, rc.bottom, SWP_NOZORDER);
    int xStack = xDebug + cxDebug + 4;
    if (m_hwndDebugStackViewer != (HWND)INVALID_HANDLE_VALUE)
        SetWindowPos(m_hwndDebugStackViewer, NULL, xStack, 0, cxStack, rc.bottom, SWP_NOZORDER);
    int xPorts = xStack + cxStack + 4;
    if (m_hwndDebugPortsViewer != (HWND)INVALID_HANDLE_VALUE)
        SetWindowPos(m_hwndDebugPortsViewer, NULL, xPorts, 0, cxPorts, rc.bottom, SWP_NOZORDER);
    int xBreaks = xPorts + cxPorts + 4;
    if (m_hwndDebugBreaksViewer != (HWND)INVALID_HANDLE_VALUE)
        SetWindowPos(m_hwndDebugBreaksViewer, NULL, xBreaks, 0, cxBreaks, rc.bottom, SWP_NOZORDER);
    int xMemory = xBreaks + cxBreaks + 4;
    if (m_hwndDebugMemoryViewer != (HWND)INVALID_HANDLE_VALUE)
        SetWindowPos(m_hwndDebugMemoryViewer, NULL, xMemory, 0, cxMemory, rc.bottom, SWP_NOZORDER);
}

LRESULT CALLBACK DebugViewWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    LRESULT lResult;
    switch (message)
    {
    case WM_DESTROY:
        g_hwndDebug = (HWND)INVALID_HANDLE_VALUE;  // We are closed! Bye-bye!..
        return CallWindowProc(m_wndprocDebugToolWindow, hWnd, message, wParam, lParam);
    case WM_SIZE:
        lResult = CallWindowProc(m_wndprocDebugToolWindow, hWnd, message, wParam, lParam);
        DebugView_AdjustWindowLayout();
        return lResult;
    default:
        return CallWindowProc(m_wndprocDebugToolWindow, hWnd, message, wParam, lParam);
    }
    //return (LRESULT)FALSE;
}

BOOL DebugView_OnKeyDown(WPARAM vkey, LPARAM /*lParam*/)
{
    switch (vkey)
    {
    case VK_ESCAPE:
        ConsoleView_Activate();
        break;
    default:
        return TRUE;
    }
    return FALSE;
}

void DebugView_UpdateWindowText()
{
    ::SetWindowText(g_hwndDebug, _T("Debug"));
}

// Update after Run or Step
void DebugView_OnUpdate()
{
    CProcessor* pCPU = g_pBoard->GetCPU();
    ASSERT(pCPU != nullptr);

    // Get new register values and set change flags
    m_wDebugCpuR6Old = m_wDebugCpuR[6];
    for (int r = 0; r < 8; r++)
    {
        WORD value = pCPU->GetReg(r);
        m_okDebugCpuRChanged[r] = (m_wDebugCpuR[r] != value);
        m_wDebugCpuR[r] = value;
    }
    WORD pswCPU = pCPU->GetPSW();
    m_okDebugCpuRChanged[8] = (m_wDebugCpuR[8] != pswCPU);
    m_wDebugCpuPswOld = m_wDebugCpuR[8];
    m_wDebugCpuR[8] = pswCPU;
    WORD cpcCPU = pCPU->GetCPC();
    m_okDebugCpuRChanged[9] = (m_wDebugCpuR[9] != cpcCPU);
    m_wDebugCpuR[9] = cpcCPU;
    WORD cpswCPU = pCPU->GetCPSW();
    m_okDebugCpuRChanged[10] = (m_wDebugCpuR[10] != cpswCPU);
    m_wDebugCpuR[10] = cpswCPU;
}

void DebugView_DrawAddressAndValue(HDC hdc, const CProcessor* pProc, uint16_t address, int x, int y, int cxChar)
{
    COLORREF colorText = Settings_GetColor(ColorDebugText);
    SetTextColor(hdc, colorText);
    DrawOctalValue(hdc, x + 0 * cxChar, y, address);
    x += 7 * cxChar;

    int addrtype = ADDRTYPE_DENY;
    uint16_t value = g_pBoard->GetWordView(address, pProc->IsHaltMode(), FALSE, &addrtype);
    if (addrtype <= ADDRTYPE_RAM4)  // ADDRTYPE_RAM, ADDRTYPE_RAM2, ADDRTYPE_RAM4
    {
        DrawOctalValue(hdc, x, y, value);
    }
    else if (addrtype == ADDRTYPE_ROM)
    {
        SetTextColor(hdc, Settings_GetColor(ColorDebugMemoryRom));
        DrawOctalValue(hdc, x, y, value);
    }
    else if (addrtype == ADDRTYPE_IO || addrtype == ADDRTYPE_EMUL)
    {
        value = g_pBoard->GetPortView(address);
        SetTextColor(hdc, Settings_GetColor(ColorDebugMemoryIO));
        DrawOctalValue(hdc, x, y, value);
    }
    else //if (addrtype == ADDRTYPE_DENY)
    {
        SetTextColor(hdc, Settings_GetColor(ColorDebugMemoryNA));
        TextOut(hdc, x, y, _T("  NA  "), 6);
    }

    SetTextColor(hdc, colorText);
}


//////////////////////////////////////////////////////////////////////

LRESULT CALLBACK DebugProcViewViewerWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_COMMAND:
        ::PostMessage(g_hwnd, WM_COMMAND, wParam, lParam);
        break;
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);

            DebugProcView_DoDraw(hdc);

            EndPaint(hWnd, &ps);
        }
        break;
    case WM_LBUTTONDOWN:
        ::SetFocus(hWnd);
        break;
        //case WM_RBUTTONDOWN:
        //    DebugView_OnRButtonDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        //    break;
    case WM_KEYDOWN:
        return (LRESULT)DebugView_OnKeyDown(wParam, lParam);
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        ::InvalidateRect(hWnd, NULL, TRUE);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return (LRESULT)FALSE;
}

void DebugProcView_DoDraw(HDC hdc)
{
    ASSERT(g_pBoard != nullptr);

    // Create and select font
    HFONT hFont = CreateMonospacedFont();
    HGDIOBJ hOldFont = SelectObject(hdc, hFont);
    int cxChar, cyLine;  GetFontWidthAndHeight(hdc, &cxChar, &cyLine);
    int cyHeight = cyLine * 17;
    COLORREF colorOld = SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
    COLORREF colorBkOld = SetBkColor(hdc, GetSysColor(COLOR_WINDOW));

    CProcessor* pDebugPU = g_pBoard->GetCPU();
    ASSERT(pDebugPU != nullptr);
    WORD* arrR = m_wDebugCpuR;
    BOOL* arrRChanged = m_okDebugCpuRChanged;
    WORD oldPsw = m_wDebugCpuPswOld;

    HGDIOBJ hOldBrush = ::SelectObject(hdc, ::GetSysColorBrush(COLOR_BTNFACE));
    int x = 32;
    ::PatBlt(hdc, x, 0, 4, cyHeight, PATCOPY);
    x += 4;
    int xProc = x;
    ::SelectObject(hdc, hOldBrush);

    DebugView_DrawProcessor(hdc, pDebugPU, xProc + cxChar, cyLine / 2, arrR, arrRChanged, oldPsw);

    SetTextColor(hdc, colorOld);
    SetBkColor(hdc, colorBkOld);
    SelectObject(hdc, hOldFont);
    VERIFY(::DeleteObject(hFont));

    if (::GetFocus() == m_hwndDebugProcViewer)
    {
        RECT rcClient;
        GetClientRect(m_hwndDebugProcViewer, &rcClient);
        rcClient.left += 32 + 4;
        DrawFocusRect(hdc, &rcClient);
    }
}

void DebugView_DrawProcessor(HDC hdc, const CProcessor* pProc, int x, int y, WORD* arrR, BOOL* arrRChanged, WORD oldPsw)
{
    int cxChar, cyLine;  GetFontWidthAndHeight(hdc, &cxChar, &cyLine);
    COLORREF colorText = Settings_GetColor(ColorDebugText);
    COLORREF colorChanged = Settings_GetColor(ColorDebugValueChanged);
    ::SetTextColor(hdc, colorText);

    // Registers
    for (int r = 0; r < 8; r++)
    {
        ::SetTextColor(hdc, arrRChanged[r] ? colorChanged : colorText);

        LPCTSTR strRegName = REGISTER_NAME[r];
        TextOut(hdc, x, y + r * cyLine, strRegName, (int)_tcslen(strRegName));

        WORD value = arrR[r]; //pProc->GetReg(r);
        DrawOctalValue(hdc, x + cxChar * 3, y + r * cyLine, value);
        DrawHexValue(hdc, x + cxChar * 10, y + r * cyLine, value);
        DrawBinaryValue(hdc, x + cxChar * 15, y + r * cyLine, value);
    }
    ::SetTextColor(hdc, colorText);

    // CPC value
    ::SetTextColor(hdc, arrRChanged[9] ? colorChanged : colorText);
    TextOut(hdc, x, y + 8 * cyLine, _T("PC'"), 3);
    WORD cpc = arrR[9];
    DrawOctalValue(hdc, x + cxChar * 3, y + 8 * cyLine, cpc);
    DrawHexValue(hdc, x + cxChar * 10, y + 8 * cyLine, cpc);
    DrawBinaryValue(hdc, x + cxChar * 15, y + 8 * cyLine, cpc);

    // PSW value
    ::SetTextColor(hdc, arrRChanged[8] ? colorChanged : colorText);
    TextOut(hdc, x, y + 10 * cyLine, _T("PS"), 2);
    WORD psw = arrR[8]; // pProc->GetPSW();
    DrawOctalValue(hdc, x + cxChar * 3, y + 10 * cyLine, psw);
    DrawHexValue(hdc, x + cxChar * 10, y + 10 * cyLine, psw);
    ::SetTextColor(hdc, colorText);
    TextOut(hdc, x + cxChar * 15, y + 9 * cyLine, _T("       HP  TNZVC"), 16);

    // PSW value bits colored bit-by-bit
    TCHAR buffera[2];  buffera[1] = 0;
    for (int i = 0; i < 16; i++)
    {
        WORD bitpos = 1 << i;
        buffera[0] = (psw & bitpos) ? '1' : '0';
        ::SetTextColor(hdc, ((psw & bitpos) != (oldPsw & bitpos)) ? colorChanged : colorText);
        TextOut(hdc, x + cxChar * (15 + 15 - i), y + 10 * cyLine, buffera, 1);
    }

    // CPSW value
    ::SetTextColor(hdc, arrRChanged[10] ? colorChanged : colorText);
    TextOut(hdc, x, y + 11 * cyLine, _T("PS'"), 3);
    WORD cpsw = arrR[10];
    DrawOctalValue(hdc, x + cxChar * 3, y + 11 * cyLine, cpsw);
    //DrawHexValue(hdc, x + cxChar * 10, y + 11 * cyLine, cpsw);
    DrawBinaryValue(hdc, x + cxChar * 15, y + 11 * cyLine, cpsw);

    ::SetTextColor(hdc, colorText);

    // Processor mode - HALT or USER
    BOOL okHaltMode = pProc->IsHaltMode();
    TextOut(hdc, x, y + 13 * cyLine, okHaltMode ? _T("HALT") : _T("USER"), 4);

    // Processor HALT pin
    if (pProc->GetHALTPin())
    {
        uint16_t ppib = g_pBoard->GetPortView(0161032);
        TCHAR bufhp[32];
        _sntprintf(bufhp, 32, _T("HALTpin:%s%s%s%s"),
                (ppib & 1) == 0 ? _T(" EF0") : _T(""),
                (ppib & 2) == 0 ? _T(" EF1") : _T(""),
                (ppib & 8) == 0 ? _T(" IHLT") : _T(""),
                (ppib & 4) == 4 ? _T(" IOINT") : _T(""));
        TextOut(hdc, x, y + 14 * cyLine, bufhp, (int)_tcslen(bufhp));
    }

    if (pProc->GetVIRQPin())
    {
        TextOut(hdc, x, y + 15 * cyLine, _T("VIRQrq"), 4);
    }

    //// "Stopped" flag
    //BOOL okStopped = pProc->IsStopped();
    //if (okStopped)
    //    TextOut(hdc, x + 6 * cxChar, y + 14 * cyLine, _T("STOP"), 4);
}


//////////////////////////////////////////////////////////////////////

LRESULT CALLBACK DebugStackViewViewerWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_COMMAND:
        ::PostMessage(g_hwnd, WM_COMMAND, wParam, lParam);
        break;
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);

            DebugStackView_DoDraw(hdc);

            EndPaint(hWnd, &ps);
        }
        break;
    case WM_LBUTTONDOWN:
        ::SetFocus(hWnd);
        break;
        //case WM_RBUTTONDOWN:
        //    DebugView_OnRButtonDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        //    break;
    case WM_KEYDOWN:
        return (LRESULT)DebugView_OnKeyDown(wParam, lParam);
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        ::InvalidateRect(hWnd, NULL, TRUE);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return (LRESULT)FALSE;
}

void DebugStackView_DoDraw(HDC hdc)
{
    ASSERT(g_pBoard != nullptr);

    // Create and select font
    HFONT hFont = CreateMonospacedFont();
    HGDIOBJ hOldFont = SelectObject(hdc, hFont);
    int cxChar, cyLine;  GetFontWidthAndHeight(hdc, &cxChar, &cyLine);

    CProcessor* pDebugPU = g_pBoard->GetCPU();
    ASSERT(pDebugPU != nullptr);
    WORD oldSP = m_wDebugCpuR6Old;

    // Draw stack for the current processor
    DebugView_DrawMemoryForRegister(hdc, 6, pDebugPU, cxChar / 2, cyLine / 2, oldSP);

    SelectObject(hdc, hOldFont);
    VERIFY(::DeleteObject(hFont));

    if (::GetFocus() == m_hwndDebugStackViewer)
    {
        RECT rcClient;
        GetClientRect(m_hwndDebugStackViewer, &rcClient);
        DrawFocusRect(hdc, &rcClient);
    }
}

void DebugView_DrawMemoryForRegister(HDC hdc, int reg, const CProcessor* pProc, int x, int y, WORD oldValue)
{
    int cxChar, cyLine;  GetFontWidthAndHeight(hdc, &cxChar, &cyLine);
    COLORREF colorText = Settings_GetColor(ColorDebugText);
    COLORREF colorChanged = Settings_GetColor(ColorDebugValueChanged);
    COLORREF colorPrev = Settings_GetColor(ColorDebugPrevious);
    COLORREF colorOld = SetTextColor(hdc, colorText);

    uint16_t current = pProc->GetReg(reg) & ~1;
    uint16_t previous = oldValue;

    WORD address = current - 16;
    for (int index = 0; index < 16; index++)
    {
        DebugView_DrawAddressAndValue(hdc, pProc, address, x + 3 * cxChar, y, cxChar);

        if (address == current)  // Current position
        {
            SetTextColor(hdc, colorText);
            TextOut(hdc, x + 2 * cxChar, y, _T(">"), 1);
            if (current != previous) SetTextColor(hdc, colorChanged);
            TextOut(hdc, x, y, REGISTER_NAME[reg], 2);
        }
        else if (address == previous)
        {
            SetTextColor(hdc, colorPrev);
            TextOut(hdc, x + 2 * cxChar, y, _T(">"), 1);
        }

        address += 2;
        y += cyLine;
    }

    SetTextColor(hdc, colorOld);
}


//////////////////////////////////////////////////////////////////////

LRESULT CALLBACK DebugPortsViewViewerWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_COMMAND:
        ::PostMessage(g_hwnd, WM_COMMAND, wParam, lParam);
        break;
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);

            DebugPortsView_DoDraw(hdc);

            EndPaint(hWnd, &ps);
        }
        break;
    case WM_LBUTTONDOWN:
        ::SetFocus(hWnd);
        break;
        //case WM_RBUTTONDOWN:
        //    DebugView_OnRButtonDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        //    break;
    case WM_KEYDOWN:
        return (LRESULT)DebugView_OnKeyDown(wParam, lParam);
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        ::InvalidateRect(hWnd, NULL, TRUE);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return (LRESULT)FALSE;
}

void DebugPortsView_DoDraw(HDC hdc)
{
    ASSERT(g_pBoard != nullptr);

    // Create and select font
    HFONT hFont = CreateMonospacedFont();
    HGDIOBJ hOldFont = SelectObject(hdc, hFont);
    int cxChar, cyLine;  GetFontWidthAndHeight(hdc, &cxChar, &cyLine);

    CProcessor* pDebugPU = g_pBoard->GetCPU();
    ASSERT(pDebugPU != nullptr);

    int nWatches = DebugView_DrawWatches(hdc, pDebugPU, cxChar, cyLine / 2);
    DebugView_DrawPorts(hdc, cxChar, cyLine / 2 + (nWatches > 0 ? 2 + nWatches : 0) * cyLine);

    SelectObject(hdc, hOldFont);
    VERIFY(::DeleteObject(hFont));

    if (::GetFocus() == m_hwndDebugPortsViewer)
    {
        RECT rcClient;
        GetClientRect(m_hwndDebugPortsViewer, &rcClient);
        DrawFocusRect(hdc, &rcClient);
    }
}

struct DebugViewPortWatch
{
    uint16_t address;
    LPCTSTR description;
}
m_DebugViewPorts[] =
{
    //{ 0161030, _T("PPIA") },
    { 0161032, _T("PPIB") },
    { 0161034, _T("PPIC") },
    { 0161000, _T("PICRR") },
    { 0161002, _T("PICMR") },
    { 0161200, _T("HR0") },
    { 0161202, _T("HR1") },
    { 0161070, _T("FD.CSR") },
    //{ 0161072, _T("FD.BUF") },
    //{ 0161400, _T("RTC csec") },
    //{ 0161402, _T("RTC cmin") },
    //{ 0161404, _T("RTC chou") },
    //{ 0161406, _T("RTC cdow") },
    //{ 0161407, _T("RTC cday") },
    //{ 0161410, _T("RTC cmon") },
    //{ 0161411, _T("RTC cyea") },
};

void DebugView_DrawPorts(HDC hdc, int x, int y)
{
    int cxChar, cyLine;  GetFontWidthAndHeight(hdc, &cxChar, &cyLine);

    TextOut(hdc, x, y, _T("Ports"), 5);

    CProcessor* pProc = g_pBoard->GetCPU();

    int portsCount = sizeof(m_DebugViewPorts) / sizeof(m_DebugViewPorts[0]);
    for (int i = 0; i < portsCount; i++)
    {
        y += cyLine;
        const DebugViewPortWatch& watch = m_DebugViewPorts[i];
        DebugView_DrawAddressAndValue(hdc, pProc, watch.address, x, y, cxChar);
        TextOut(hdc, x + 14 * cxChar, y, watch.description, _tcslen(watch.description));
    }
}

int DebugView_DrawWatches(HDC hdc, const CProcessor* pProc, int x, int y)
{
    const uint16_t* pws = Emulator_GetWatchList();
    if (*pws == 0177777)
        return 0;

    int cxChar, cyLine;  GetFontWidthAndHeight(hdc, &cxChar, &cyLine);

    int nWatches = 0;
    TextOut(hdc, x, y, _T("Watches"), 7);
    y += cyLine;
    while (*pws != 0177777)
    {
        uint16_t address = *pws;
        DebugView_DrawAddressAndValue(hdc, pProc, address, x, y, cxChar);
        y += cyLine;
        pws++;  nWatches++;
    }

    return nWatches;
}


//////////////////////////////////////////////////////////////////////

LRESULT CALLBACK DebugBreaksViewViewerWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_COMMAND:
        ::PostMessage(g_hwnd, WM_COMMAND, wParam, lParam);
        break;
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);

            DebugBreaksView_DoDraw(hdc);

            EndPaint(hWnd, &ps);
        }
        break;
    case WM_LBUTTONDOWN:
        ::SetFocus(hWnd);
        break;
    case WM_RBUTTONDOWN:
        DebugBreaksView_OnRButtonDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        break;
    case WM_KEYDOWN:
        return (LRESULT)DebugView_OnKeyDown(wParam, lParam);
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        ::InvalidateRect(hWnd, NULL, TRUE);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return (LRESULT)FALSE;
}

void DebugBreaksView_DoDraw(HDC hdc)
{
    ASSERT(g_pBoard != nullptr);

    // Create and select font
    HFONT hFont = CreateMonospacedFont();
    HGDIOBJ hOldFont = SelectObject(hdc, hFont);
    int cxChar, cyLine;  GetFontWidthAndHeight(hdc, &cxChar, &cyLine);

    int x = cxChar / 2, y = cyLine / 2;
    TextOut(hdc, x, y, _T("Breakpts"), 8);

    const uint32_t* pbps = Emulator_GetCPUBreakpointList();
    if (*pbps != NOBREAKPOINT)
    {
        x += cxChar;
        y += cyLine;
        while (*pbps != NOBREAKPOINT)
        {
            uint32_t bpvalue = *pbps;
            uint16_t address = bpvalue & 0xffff;
            TCHAR buffer[8];
            buffer[0] = (bpvalue & BREAKPOINT_HALT) != 0 ? _T('H') : _T('U');
            PrintOctalValue(buffer + 1, address);
            TextOut(hdc, x, y, buffer, 7);
            y += cyLine;
            pbps++;
        }
    }

    SelectObject(hdc, hOldFont);
    VERIFY(::DeleteObject(hFont));

    if (::GetFocus() == m_hwndDebugBreaksViewer)
    {
        RECT rcClient;
        GetClientRect(m_hwndDebugBreaksViewer, &rcClient);
        DrawFocusRect(hdc, &rcClient);
    }
}

void DebugBreaksView_OnRButtonDown(int mousex, int mousey)
{
    ::SetFocus(m_hwndDebugBreaksViewer);

    HMENU hMenu = ::CreatePopupMenu();
    ::AppendMenu(hMenu, 0, ID_DEBUG_DELETEALLBREAKPTS, _T("Delete All Breakpoints"));

    POINT pt = { mousex, mousey };
    ::ClientToScreen(m_hwndDebugBreaksViewer, &pt);
    ::TrackPopupMenu(hMenu, 0, pt.x, pt.y, 0, m_hwndDebugBreaksViewer, NULL);

    VERIFY(::DestroyMenu(hMenu));
}


//////////////////////////////////////////////////////////////////////

LRESULT CALLBACK DebugMemoryViewViewerWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_COMMAND:
        ::PostMessage(g_hwnd, WM_COMMAND, wParam, lParam);
        break;
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);

            DebugMemoryView_DoDraw(hdc);

            EndPaint(hWnd, &ps);
        }
        break;
    case WM_LBUTTONDOWN:
        ::SetFocus(hWnd);
        break;
        //case WM_RBUTTONDOWN:
        //    DebugView_OnRButtonDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        //    break;
    case WM_KEYDOWN:
        return (LRESULT)DebugView_OnKeyDown(wParam, lParam);
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        ::InvalidateRect(hWnd, NULL, TRUE);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return (LRESULT)FALSE;
}

void DebugMemoryView_DoDraw(HDC hdc)
{
    ASSERT(g_pBoard != nullptr);

    // Create and select font
    HFONT hFont = CreateMonospacedFont();
    HGDIOBJ hOldFont = SelectObject(hdc, hFont);
    int cxChar, cyLine;  GetFontWidthAndHeight(hdc, &cxChar, &cyLine);
    COLORREF colorOld = SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
    COLORREF colorBkOld = SetBkColor(hdc, GetSysColor(COLOR_WINDOW));

    CProcessor* pDebugPU = g_pBoard->GetCPU();
    ASSERT(pDebugPU != nullptr);

    HGDIOBJ hOldBrush = ::SelectObject(hdc, ::GetSysColorBrush(COLOR_BTNFACE));
    int x = 4;
    int xMemmap = x;
    x += cxChar * 24;
    //::PatBlt(hdc, x, 0, 4, cyHeight, PATCOPY);
    //x += 4;
    int xHRandUR = x;
    ::SelectObject(hdc, hOldBrush);

    DebugView_DrawMemoryMap(hdc, xMemmap + cxChar / 2, 0, pDebugPU);

    DebugView_DrawHRandUR(hdc, xHRandUR + cxChar, cyLine / 2);

    SetTextColor(hdc, colorOld);
    SetBkColor(hdc, colorBkOld);
    SelectObject(hdc, hOldFont);
    VERIFY(::DeleteObject(hFont));

    if (::GetFocus() == m_hwndDebugMemoryViewer)
    {
        RECT rcClient;
        GetClientRect(m_hwndDebugMemoryViewer, &rcClient);
        DrawFocusRect(hdc, &rcClient);
    }
}

void DebugView_DrawMemoryMap(HDC hdc, int x, int y, const CProcessor* pProc)
{
    int cxChar, cyLine;  GetFontWidthAndHeight(hdc, &cxChar, &cyLine);

    int x1 = x + cxChar * 7;
    int y1 = y + cxChar / 2;
    int x2 = x1 + cxChar * 13;
    int y2 = y1 + cyLine * 16;
    int xtype = x1 + cxChar * 2;
    int ybase = y + cyLine * 16;

    HGDIOBJ hOldBrush = ::SelectObject(hdc, ::GetSysColorBrush(COLOR_BTNSHADOW));
    PatBlt(hdc, x1, y1, 1, y2 - y1, PATCOPY);
    PatBlt(hdc, x2, y1, 1, y2 - y1 + 1, PATCOPY);
    PatBlt(hdc, x1, y1, x2 - x1, 1, PATCOPY);
    PatBlt(hdc, x1, y2, x2 - x1, 1, PATCOPY);

    BOOL okHaltMode = pProc->IsHaltMode();
    TextOut(hdc, x, y + cyLine / 2, okHaltMode ? _T("HALT") : _T("USER"), 4);

    uint16_t portBaseAddr = pProc->IsHaltMode() ? 0161200 : 0161220;
    for (int i = 0; i < 8; i++)
    {
        int yp = y2 - i * cyLine * 2;
        PatBlt(hdc, x1, yp, x2 - x1, 1, PATCOPY);
        WORD addr = (WORD)(i * 020000);
        DrawOctalValue(hdc, x, yp - cyLine / 2, addr);
    }
    for (int i = 0; i < 7; i++)
    {
        int ytype = ybase - cyLine * i * 2 - cyLine;
        if (i < 2 && pProc->IsHaltMode())
            TextOut(hdc, xtype, ytype, _T("ROM"), 3);
        else
        {
            uint16_t value = g_pBoard->GetPortView((uint16_t)(portBaseAddr + 2 * i));
            TCHAR buffer[7];
            PrintOctalValue(buffer, (value & 0037760) >> 4);
            TextOut(hdc, xtype, ytype, buffer + 2, 4);
            if ((value & 4) != 0)
                TextOut(hdc, xtype + cxChar * 5, ytype, _T("OFF"), 3);
            else
                TextOut(hdc, xtype + cxChar * 5, ytype, _T("ON"), 2);
        }
    }

    TextOut(hdc, xtype, ybase - cyLine * 15, _T("I/O"), 3);

    uint16_t sp = pProc->GetSP();
    int ysp = y2 - ((y2 - y1) * sp / 65536);
    PatBlt(hdc, x2, ysp, cxChar, 1, PATCOPY);
    TextOut(hdc, x2 + cxChar, ysp - cyLine / 2, _T("SP"), 2);

    uint16_t pc = pProc->GetPC();
    int ypc = y2 - ((y2 - y1) * pc / 65536);
    PatBlt(hdc, x2, ypc, cxChar, 1, PATCOPY);
    TextOut(hdc, x2 + cxChar, ypc - cyLine / 2, _T("PC"), 2);

    ::SelectObject(hdc, hOldBrush);
}

void DebugView_DrawHRandUR(HDC hdc, int x, int y)
{
    int cxChar, cyLine;  GetFontWidthAndHeight(hdc, &cxChar, &cyLine);
    TCHAR buffer[16];

    for (int i = 0; i < 8; i++)
    {
        uint16_t hr = g_pBoard->GetPortView((uint16_t)(0161200 + i * 2));
        uint16_t ur = g_pBoard->GetPortView((uint16_t)(0161220 + i * 2));
        const TCHAR formatH[] = _T("HR%d %06o");
        _sntprintf(buffer, 24, formatH, i, hr, i, ur);
        TextOut(hdc, x, y + cyLine * (7 - i), buffer, (int)_tcslen(buffer));
        const TCHAR formatU[] = _T("UR%d %06o");
        _sntprintf(buffer, 24, formatU, i, hr, i, ur);
        TextOut(hdc, x, y + cyLine * (8 + 7 - i), buffer, (int)_tcslen(buffer));
    }
}


//////////////////////////////////////////////////////////////////////
