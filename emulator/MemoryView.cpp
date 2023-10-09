/*  This file is part of NEONBTL.
    NEONBTL is free software: you can redistribute it and/or modify it under the terms
of the GNU Lesser General Public License as published by the Free Software Foundation,
either version 3 of the License, or (at your option) any later version.
    NEONBTL is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU Lesser General Public License for more details.
    You should have received a copy of the GNU Lesser General Public License along with
NEONBTL. If not, see <http://www.gnu.org/licenses/>. */

// MemoryView.cpp

#include "stdafx.h"
#include <CommCtrl.h>
#include <windowsx.h>
#include "Main.h"
#include "Views.h"
#include "ToolWindow.h"
#include "Dialogs.h"
#include "Emulator.h"
#include "emubase/Emubase.h"

//////////////////////////////////////////////////////////////////////


HWND g_hwndMemory = (HWND)INVALID_HANDLE_VALUE;  // Memory view window handler
WNDPROC m_wndprocMemoryToolWindow = NULL;  // Old window proc address of the ToolWindow

HWND m_hwndMemoryTab = (HWND)INVALID_HANDLE_VALUE;
HWND m_hwndMemoryViewer = (HWND)INVALID_HANDLE_VALUE;
HWND m_hwndMemoryToolbar = (HWND)INVALID_HANDLE_VALUE;

HWND m_hwndProcessListViewer = (HWND)INVALID_HANDLE_VALUE;

int m_cxChar = 0;
int m_cyLineMemory = 0;  // Line height in pixels
int m_nPageSize = 100;  // Page size in lines

int     m_Mode = 0;  // See MemoryViewMode enum
int     m_NumeralMode = MEMMODENUM_OCT;
WORD    m_wBaseAddress = 0xFFFF;
WORD    m_wCurrentAddress = 0xFFFF;
BOOL    m_okMemoryByteMode = FALSE;
int     m_PostionIncrement = 100;  // Increment by X to the next word

void MemoryView_AdjustWindowLayout();
void MemoryView_OnTabChange();
BOOL MemoryView_OnKeyDown(WPARAM vkey, LPARAM lParam);
void MemoryView_OnLButtonDown(int mousex, int mousey);
void MemoryView_OnRButtonDown(int mousex, int mousey);
BOOL MemoryView_OnMouseWheel(WPARAM wParam, LPARAM lParam);
BOOL MemoryView_OnVScroll(WORD scrollcmd, WORD scrollpos);
void MemoryView_CopyValueToClipboard(WPARAM command);
void MemoryView_GotoAddress(WORD wAddress);
void MemoryView_ScrollTo(WORD wBaseAddress);
void MemoryView_UpdateWindowText();
LPCTSTR MemoryView_GetMemoryModeName();
void MemoryView_UpdateScrollPos();
void MemoryView_UpdateToolbar();
void MemoryView_GetCurrentValueRect(LPRECT pRect, int cxChar, int cyLine);
WORD MemoryView_GetWordFromMemory(WORD address, BOOL& okValid, int& addrtype, WORD& wChanged);
void MemoryView_OnDraw(HDC hdc);

void ProcessListView_OnDraw(HDC hdc);


//////////////////////////////////////////////////////////////////////


void MemoryView_RegisterClass()
{
    WNDCLASSEX wcex;
    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = MemoryViewViewerWndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = g_hInst;
    wcex.hIcon          = NULL;
    wcex.hCursor        = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName   = NULL;
    wcex.lpszClassName  = CLASSNAME_MEMORYVIEW;
    wcex.hIconSm        = NULL;
    RegisterClassEx(&wcex);

    wcex.lpfnWndProc = ProcessListViewViewerWndProc;
    wcex.lpszClassName = CLASSNAME_PROCESSLISTVIEW;
    RegisterClassEx(&wcex);
}

void MemoryView_Create(HWND hwndParent, int x, int y, int width, int height)
{
    ASSERT(hwndParent != NULL);

    g_hwndMemory = CreateWindow(
            CLASSNAME_TOOLWINDOW, NULL,
            WS_CHILD | WS_VISIBLE,
            x, y, width, height,
            hwndParent, NULL, g_hInst, NULL);
    MemoryView_UpdateWindowText();

    // ToolWindow subclassing
    m_wndprocMemoryToolWindow = (WNDPROC) LongToPtr( SetWindowLongPtr(
            g_hwndMemory, GWLP_WNDPROC, PtrToLong(MemoryViewWndProc)) );

    RECT rcClient;  GetClientRect(g_hwndMemory, &rcClient);

    m_hwndMemoryTab = CreateWindowEx(
            0,
            WC_TABCONTROL, NULL,
            WS_CHILD | WS_CLIPSIBLINGS | WS_VISIBLE,
            0, 0, rcClient.right, rcClient.bottom,
            g_hwndMemory, NULL, g_hInst, NULL);
    TCITEM tie;
    tie.mask = TCIF_TEXT;
    tie.pszText = _T(" CPU ");
    TabCtrl_InsertItem(m_hwndMemoryTab, 0, &tie);
    tie.pszText = _T(" HALT ");
    TabCtrl_InsertItem(m_hwndMemoryTab, 1, &tie);
    tie.pszText = _T(" USER ");
    TabCtrl_InsertItem(m_hwndMemoryTab, 2, &tie);
    tie.pszText = _T(" Process List ");
    TabCtrl_InsertItem(m_hwndMemoryTab, 3, &tie);

    HFONT hFont = CreateDialogFont();
    ::SendMessage(m_hwndMemoryTab, WM_SETFONT, (WPARAM)hFont, NULL);

    m_hwndMemoryViewer = CreateWindowEx(
            0,
            CLASSNAME_MEMORYVIEW, NULL,
            WS_CHILD | WS_VSCROLL | WS_TABSTOP,
            4, 28, rcClient.right, rcClient.bottom,
            g_hwndMemory, NULL, g_hInst, NULL);
    m_hwndMemoryToolbar = CreateWindowEx(0, TOOLBARCLASSNAME, NULL,
            WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TRANSPARENT | TBSTYLE_TOOLTIPS | CCS_NOPARENTALIGN | CCS_NODIVIDER | CCS_VERT,
            4, 4, 32, 32 * 8, m_hwndMemoryViewer,
            (HMENU) 102,
            g_hInst, NULL);

    m_hwndProcessListViewer = CreateWindowEx(
            0,
            CLASSNAME_PROCESSLISTVIEW, NULL,
            WS_CHILD | WS_VSCROLL | WS_TABSTOP,
            32 + 4, 0, rcClient.right, rcClient.bottom,
            g_hwndMemory, NULL, g_hInst, NULL);

    TBADDBITMAP addbitmap;
    addbitmap.hInst = g_hInst;
    addbitmap.nID = IDB_TOOLBAR;
    SendMessage(m_hwndMemoryToolbar, TB_ADDBITMAP, 2, (LPARAM) &addbitmap);

    SendMessage(m_hwndMemoryToolbar, TB_BUTTONSTRUCTSIZE, (WPARAM) sizeof(TBBUTTON), 0);
    SendMessage(m_hwndMemoryToolbar, TB_SETBUTTONSIZE, 0, (LPARAM) MAKELONG(26, 26));

    TBBUTTON buttons[4];
    ZeroMemory(buttons, sizeof(buttons));
    for (int i = 0; i < sizeof(buttons) / sizeof(TBBUTTON); i++)
    {
        buttons[i].fsState = TBSTATE_ENABLED | TBSTATE_WRAP;
        buttons[i].fsStyle = BTNS_BUTTON | TBSTYLE_GROUP;
        buttons[i].iString = -1;
    }
    buttons[0].idCommand = ID_DEBUG_MEMORY_GOTO;
    buttons[0].iBitmap = ToolbarImageGotoAddress;
    buttons[1].fsStyle = BTNS_SEP;
    buttons[2].idCommand = ID_DEBUG_MEMORY_WORDBYTE;
    buttons[2].iBitmap = ToolbarImageWordByte;
    buttons[3].idCommand = ID_DEBUG_MEMORY_HEXMODE;
    buttons[3].iBitmap = ToolbarImageHexMode;

    SendMessage(m_hwndMemoryToolbar, TB_ADDBUTTONS, (WPARAM) sizeof(buttons) / sizeof(TBBUTTON), (LPARAM) &buttons);

    MemoryView_ScrollTo(Settings_GetDebugMemoryBase());
    MemoryView_GotoAddress(Settings_GetDebugMemoryAddress());

    m_okMemoryByteMode = Settings_GetDebugMemoryByte();
    m_NumeralMode = Settings_GetDebugMemoryNumeral();

    int mode = Settings_GetDebugMemoryMode();
    if (mode > MEMMODE_LAST) mode = MEMMODE_LAST;
    MemoryView_SetViewMode((MemoryViewMode)mode);
}

// Adjust position of client windows
void MemoryView_AdjustWindowLayout()
{
    RECT rc;  GetClientRect(g_hwndMemory, &rc);

    if (m_hwndMemoryTab != (HWND)INVALID_HANDLE_VALUE)
        SetWindowPos(m_hwndMemoryTab, NULL, 0, 0, rc.right, rc.bottom, SWP_NOZORDER);

    TabCtrl_AdjustRect(m_hwndMemoryTab, FALSE, &rc);
    ::InflateRect(&rc, -4, -4);

    if (m_hwndMemoryViewer != (HWND) INVALID_HANDLE_VALUE)
        SetWindowPos(m_hwndMemoryViewer, NULL, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, SWP_NOZORDER);
    if (m_hwndProcessListViewer != (HWND)INVALID_HANDLE_VALUE)
        SetWindowPos(m_hwndProcessListViewer, NULL, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, SWP_NOZORDER);
}

LRESULT CALLBACK MemoryViewWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    LRESULT lResult;
    switch (message)
    {
    case WM_DESTROY:
        g_hwndMemory = (HWND) INVALID_HANDLE_VALUE;  // We are closed! Bye-bye!..
        return CallWindowProc(m_wndprocMemoryToolWindow, hWnd, message, wParam, lParam);
    case WM_SIZE:
        lResult = CallWindowProc(m_wndprocMemoryToolWindow, hWnd, message, wParam, lParam);
        MemoryView_AdjustWindowLayout();
        return lResult;
    case WM_NOTIFY:
        if (((LPNMHDR)lParam)->code == TCN_SELCHANGE)
            MemoryView_OnTabChange();
        else
            return DefWindowProc(hWnd, message, wParam, lParam);
        break;
    default:
        return CallWindowProc(m_wndprocMemoryToolWindow, hWnd, message, wParam, lParam);
    }
    return (LRESULT)FALSE;
}

LRESULT CALLBACK MemoryViewViewerWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_COMMAND:
        if (wParam == ID_DEBUG_COPY_VALUE || wParam == ID_DEBUG_COPY_ADDRESS)  // Copy command from context menu
            MemoryView_CopyValueToClipboard(wParam);
        else if (wParam == ID_DEBUG_GOTO_ADDRESS)  // "Go to Address" from context menu
            MemoryView_SelectAddress();
        else if (wParam == ID_DEBUG_MEMORY_HEXMODE)
            MemoryView_SwitchNumeralMode();
        else
            ::PostMessage(g_hwnd, WM_COMMAND, wParam, lParam);
        break;
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);

            MemoryView_OnDraw(hdc);

            EndPaint(hWnd, &ps);
        }
        break;
    case WM_LBUTTONDOWN:
        MemoryView_OnLButtonDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        break;
    case WM_RBUTTONDOWN:
        MemoryView_OnRButtonDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        break;
    case WM_KEYDOWN:
        return (LRESULT) MemoryView_OnKeyDown(wParam, lParam);
    case WM_MOUSEWHEEL:
        return (LRESULT) MemoryView_OnMouseWheel(wParam, lParam);
    case WM_VSCROLL:
        return (LRESULT) MemoryView_OnVScroll(LOWORD(wParam), HIWORD(wParam));
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        ::InvalidateRect(hWnd, NULL, TRUE);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return (LRESULT)FALSE;
}

void MemoryView_OnTabChange()
{
    int tab = TabCtrl_GetCurSel(m_hwndMemoryTab);

    MemoryView_SetViewMode((MemoryViewMode)tab);
}

// Returns even address 0-65534, or -1 if out of area
int MemoryView_GetAddressByPoint(int mousex, int mousey)
{
    int line = mousey / m_cyLineMemory - 1;
    if (line < 0) line = 0;
    else if (line >= m_nPageSize) return -1;
    int pos = (mousex - (32 + 4) - 9 * m_cxChar + m_cxChar / 2) / m_PostionIncrement;
    if (pos < 0) pos = 0;
    else if (pos > 7) pos = 7;

    return (WORD)(m_wBaseAddress + line * 16 + pos * 2);
}

BOOL MemoryView_OnKeyDown(WPARAM vkey, LPARAM /*lParam*/)
{
    switch (vkey)
    {
    case VK_ESCAPE:
        ConsoleView_Activate();
        break;
    case VK_HOME:
        MemoryView_GotoAddress(0);
        break;
    case VK_END:
        MemoryView_GotoAddress(0177777);
        break;
    case VK_LEFT:
        MemoryView_GotoAddress((WORD)(m_wCurrentAddress - 2));
        break;
    case VK_RIGHT:
        MemoryView_GotoAddress((WORD)(m_wCurrentAddress + 2));
        break;
    case VK_UP:
        MemoryView_GotoAddress((WORD)(m_wCurrentAddress - 16));
        break;
    case VK_DOWN:
        MemoryView_GotoAddress((WORD)(m_wCurrentAddress + 16));
        break;
    case VK_PRIOR:
        MemoryView_GotoAddress((WORD)(m_wCurrentAddress - m_nPageSize * 16));
        break;
    case VK_NEXT:
        MemoryView_GotoAddress((WORD)(m_wCurrentAddress + m_nPageSize * 16));
        break;
    case 0x47:  // G - Go To Address
        MemoryView_SelectAddress();
        break;
    case 0x42:  // B - change byte/word mode
    case 0x57:  // W
        MemoryView_SwitchWordByte();
        break;
    case 0x48:  // H - Hex
    case 0x4F:  // O - Octal
        MemoryView_SwitchNumeralMode();
        break;
    default:
        return TRUE;
    }
    return FALSE;
}

void MemoryView_OnLButtonDown(int mousex, int mousey)
{
    ::SetFocus(m_hwndMemoryViewer);

    int addr = MemoryView_GetAddressByPoint(mousex, mousey);
    if (addr >= 0)
        MemoryView_GotoAddress((WORD)addr);
}

void MemoryView_OnRButtonDown(int mousex, int mousey)
{
    ::SetFocus(m_hwndMemoryViewer);

    POINT pt = { mousex, mousey };
    HMENU hMenu = ::CreatePopupMenu();

    int addr = MemoryView_GetAddressByPoint(mousex, mousey);
    if (addr >= 0)
    {
        MemoryView_GotoAddress((WORD)addr);

        RECT rcValue;
        MemoryView_GetCurrentValueRect(&rcValue, m_cxChar, m_cyLineMemory);
        pt.x = rcValue.left;  pt.y = rcValue.bottom;

        int addrtype;
        BOOL okValid;
        WORD wChanged;
        uint16_t value = MemoryView_GetWordFromMemory((uint16_t)addr, okValid, addrtype, wChanged);

        TCHAR buffer[24];
        if (okValid)
        {
            LPCTSTR vformat = (m_NumeralMode == MEMMODENUM_OCT) ? _T("Copy Value: %06o") : _T("Copy Value: %04x");
            _sntprintf(buffer, sizeof(buffer) / sizeof(TCHAR) - 1, vformat, value);
            ::AppendMenu(hMenu, 0, ID_DEBUG_COPY_VALUE, buffer);
        }
        LPCTSTR aformat = (m_NumeralMode == MEMMODENUM_OCT) ? _T("Copy Address: %06o") : _T("Copy Address: %04x");
        _sntprintf(buffer, sizeof(buffer) / sizeof(TCHAR) - 1, aformat, addr);
        ::AppendMenu(hMenu, 0, ID_DEBUG_COPY_ADDRESS, buffer);
        ::AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
    }

    ::AppendMenu(hMenu, 0, ID_DEBUG_GOTO_ADDRESS, _T("Go to Address...\tG"));

    ::ClientToScreen(m_hwndMemoryViewer, &pt);
    ::TrackPopupMenu(hMenu, 0, pt.x, pt.y, 0, m_hwndMemoryViewer, NULL);

    VERIFY(::DestroyMenu(hMenu));
}

BOOL MemoryView_OnMouseWheel(WPARAM wParam, LPARAM /*lParam*/)
{
    short zDelta = GET_WHEEL_DELTA_WPARAM(wParam);

    int nDelta = zDelta / 120;
    if (nDelta > 5) nDelta = 5;
    if (nDelta < -5) nDelta = -5;

    MemoryView_GotoAddress((WORD)(m_wCurrentAddress - nDelta * 2 * 16));

    return FALSE;
}

BOOL MemoryView_OnVScroll(WORD scrollcmd, WORD scrollpos)
{
    switch (scrollcmd)
    {
    case SB_LINEDOWN:
        MemoryView_GotoAddress((WORD)(m_wCurrentAddress + 16));
        break;
    case SB_LINEUP:
        MemoryView_GotoAddress((WORD)(m_wCurrentAddress - 16));
        break;
    case SB_PAGEDOWN:
        MemoryView_GotoAddress((WORD)(m_wCurrentAddress + m_nPageSize * 16));
        break;
    case SB_PAGEUP:
        MemoryView_GotoAddress((WORD)(m_wCurrentAddress - m_nPageSize * 16));
        break;
    case SB_THUMBPOSITION:
        MemoryView_GotoAddress((WORD)(scrollpos * 16));
        break;
    }

    return FALSE;
}

void MemoryView_UpdateWindowText()
{
    if (m_Mode <= MEMMODE_USER)
    {
        TCHAR buffer[64];
        LPCTSTR format = (m_NumeralMode == MEMMODENUM_OCT) ? _T("Memory - %s - %06o") : _T("Memory - %s - %04x");
        _sntprintf(buffer, sizeof(buffer) / sizeof(TCHAR) - 1, format, MemoryView_GetMemoryModeName(), m_wCurrentAddress);
        ::SetWindowText(g_hwndMemory, buffer);
    }
    else
    {
        ::SetWindowText(g_hwndMemory, _T("Memory - Process List"));
    }
}

void MemoryView_CopyValueToClipboard(WPARAM command)
{
    WORD address = m_wCurrentAddress;
    WORD value;

    if (command == ID_DEBUG_COPY_ADDRESS)
    {
        value = address;
    }
    else
    {
        // Get word from memory
        int addrtype;
        BOOL okValid;
        WORD wChanged;
        value = MemoryView_GetWordFromMemory(address, okValid, addrtype, wChanged);

        if (!okValid)
        {
            AlertWarning(_T("Failed to get value: invalid memory type."));
            return;
        }
    }

    TCHAR buffer[7];
    if (m_NumeralMode == MEMMODENUM_OCT)
        PrintOctalValue(buffer, value);
    else
        PrintHexValue(buffer, value);

    CopyTextToClipboard(buffer);
}

void MemoryView_UpdateToolbar()
{
    SendMessage(m_hwndMemoryToolbar, TB_CHECKBUTTON, ID_DEBUG_MEMORY_HEXMODE, (Settings_GetDebugMemoryNumeral() == MEMMODENUM_OCT ? 0 : 1));
}

void MemoryView_SetViewMode(MemoryViewMode mode)
{
    if (mode < 0) mode = (MemoryViewMode)0;
    if (mode > MEMMODE_LAST) mode = MEMMODE_LAST;
    m_Mode = mode;
    Settings_SetDebugMemoryMode((WORD)m_Mode);

    bool okShowMemory = m_Mode <= MEMMODE_USER;
    ::ShowWindow(m_hwndMemoryViewer, okShowMemory ? SW_NORMAL : SW_HIDE);
    ::ShowWindow(m_hwndProcessListViewer, m_Mode == MEMMODE_PS ? SW_NORMAL : SW_HIDE);

    if (okShowMemory)
    {
        ::InvalidateRect(m_hwndMemoryViewer, NULL, TRUE);
        ::SetFocus(m_hwndMemoryViewer);
    }
    if (m_Mode == MEMMODE_PS)
    {
        ::InvalidateRect(m_hwndProcessListViewer, NULL, TRUE);
        ::SetFocus(m_hwndProcessListViewer);
    }

    TabCtrl_SetCurSel(m_hwndMemoryTab, m_Mode);
    MemoryView_UpdateWindowText();
    MemoryView_UpdateToolbar();
}

LPCTSTR MemoryView_GetMemoryModeName()
{
    switch (m_Mode)
    {
    case MEMMODE_CPU:   return _T("CPU");
    case MEMMODE_HALT:  return _T("HALT");
    case MEMMODE_USER:  return _T("USER");
    default:
        return _T("UKWN");  // Unknown mode
    }
}

void MemoryView_SwitchWordByte()
{
    m_okMemoryByteMode = !m_okMemoryByteMode;
    Settings_SetDebugMemoryByte(m_okMemoryByteMode);

    InvalidateRect(m_hwndMemoryViewer, NULL, TRUE);
}

void MemoryView_SelectAddress()
{
    WORD value = m_wCurrentAddress;
    if (InputBoxOctal(m_hwndMemoryViewer, _T("Go To Address"), &value))
        MemoryView_GotoAddress(value);
    ::SetFocus(m_hwndMemoryViewer);
}

void MemoryView_GotoAddress(WORD wAddress)
{
    m_wCurrentAddress = wAddress & ((WORD)~1);  // Address should be even
    Settings_SetDebugMemoryAddress(m_wCurrentAddress);

    int addroffset = wAddress - m_wBaseAddress;
    if (addroffset < 0)
    {
        WORD baseaddr = (m_wCurrentAddress & 0xFFF0);  // Base address should be 16-byte aligned
        MemoryView_ScrollTo(baseaddr);
    }
    else if (addroffset >= m_nPageSize * 16)
    {
        WORD baseaddr = (WORD)((m_wCurrentAddress & 0xFFF0) - (m_nPageSize - 1) * 16);
        MemoryView_ScrollTo(baseaddr);
    }

    MemoryView_UpdateWindowText();
}

// Scroll window to the given base address
void MemoryView_ScrollTo(WORD wBaseAddress)
{
    if (wBaseAddress == m_wBaseAddress)
        return;

    m_wBaseAddress = wBaseAddress;
    Settings_SetDebugMemoryBase(wBaseAddress);

    InvalidateRect(m_hwndMemoryViewer, NULL, TRUE);

    MemoryView_UpdateScrollPos();
}

void MemoryView_SwitchNumeralMode()
{
    int newMode = m_NumeralMode ^ 1;
    m_NumeralMode = newMode;
    InvalidateRect(m_hwndMemoryViewer, NULL, TRUE);
    Settings_SetDebugMemoryNumeral((WORD)newMode);

    MemoryView_UpdateWindowText();
    MemoryView_UpdateToolbar();
}

void MemoryView_UpdateScrollPos()
{
    SCROLLINFO si;
    ZeroMemory(&si, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask = SIF_PAGE | SIF_POS | SIF_RANGE;
    si.nPage = m_nPageSize;
    si.nPos = m_wBaseAddress / 16;
    si.nMin = 0;
    si.nMax = 0x10000 / 16 - 1;
    SetScrollInfo(m_hwndMemoryViewer, SB_VERT, &si, TRUE);
}

void MemoryView_GetCurrentValueRect(LPRECT pRect, int cxChar, int cyLine)
{
    ASSERT(pRect != NULL);

    int addroffset = m_wCurrentAddress - m_wBaseAddress;
    int line = addroffset / 16;
    int pos = addroffset & 15;

    pRect->left = 32 + 4 + cxChar * 9 + m_PostionIncrement * (pos / 2) - cxChar / 2;
    pRect->right = pRect->left + m_PostionIncrement - 1;
    pRect->top = (line + 1) * cyLine - 1;
    pRect->bottom = pRect->top + cyLine + 1;
}

WORD MemoryView_GetWordFromMemory(WORD address, BOOL& okValid, int& addrtype, WORD& wChanged)
{
    WORD word = 0;
    okValid = TRUE;
    wChanged = 0;
    bool okHalt = false;

    switch (m_Mode)
    {
    case MEMMODE_CPU:
        okHalt = g_pBoard->GetCPU()->IsHaltMode();
        break;
    case MEMMODE_HALT:
        okHalt = true;
        break;
    case MEMMODE_USER:
        okHalt = false;
        break;
    }

    uint32_t offset;
    addrtype = g_pBoard->TranslateAddress(address, okHalt, FALSE, &offset);
    okValid = (addrtype != ADDRTYPE_IO) && (addrtype != ADDRTYPE_DENY);
    if (okValid)
    {
        if (addrtype == ADDRTYPE_ROM)
            word = g_pBoard->GetROMWord((uint16_t)offset);
        else
        {
            word = g_pBoard->GetRAMWordView(offset);
            wChanged = Emulator_GetChangeRamStatus(offset);
        }
    }

    return word;
}

uint16_t MemoryView_GetWordFromHalt(WORD address)
{
    uint32_t offset;
    int addrtype = g_pBoard->TranslateAddress(address, true, FALSE, &offset);
    if (addrtype < ADDRTYPE_RAM4)
        return g_pBoard->GetRAMWordView(offset);
    return 0;
}

void MemoryView_OnDraw(HDC hdc)
{
    ASSERT(g_pBoard != NULL);

    HFONT hFont = CreateMonospacedFont();
    HGDIOBJ hOldFont = SelectObject(hdc, hFont);

    int cxChar, cyLine;  GetFontWidthAndHeight(hdc, &cxChar, &cyLine);
    COLORREF colorText = Settings_GetColor(ColorDebugText);
    COLORREF colorChanged = Settings_GetColor(ColorDebugValueChanged);
    COLORREF colorMemoryRom = Settings_GetColor(ColorDebugMemoryRom);
    COLORREF colorMemoryIO = Settings_GetColor(ColorDebugMemoryIO);
    COLORREF colorMemoryNA = Settings_GetColor(ColorDebugMemoryNA);
    COLORREF colorHighlight = Settings_GetColor(ColorDebugHighlight);

    RECT rcClip;
    GetClipBox(hdc, &rcClip);
    RECT rcClient;
    GetClientRect(m_hwndMemoryViewer, &rcClient);

    if (m_NumeralMode == MEMMODENUM_OCT)
        m_PostionIncrement = cxChar * 7;
    else
        m_PostionIncrement = cxChar * 5;
    if (m_okMemoryByteMode)
        m_PostionIncrement += cxChar;

    int xRight = 32 + 4 + cxChar * 27 + m_PostionIncrement * 8 + cxChar / 2;
    HGDIOBJ hOldBrush = ::SelectObject(hdc, ::GetSysColorBrush(COLOR_BTNFACE));
    ::PatBlt(hdc, 32, 0, 4, rcClient.bottom, PATCOPY);
    ::PatBlt(hdc, xRight, 0, 4, rcClient.bottom, PATCOPY);

    HBRUSH hbrHighlight = ::CreateSolidBrush(colorHighlight);
    ::SelectObject(hdc, hbrHighlight);
    ::SetBkMode(hdc, TRANSPARENT);

    m_cxChar = cxChar;
    m_cyLineMemory = cyLine;

    TCHAR buffer[7];
    const TCHAR* ADDRESS_LINE_OCT_WORDS = _T("  addr   0      2      4      6      10     12     14     16");
    const TCHAR* ADDRESS_LINE_OCT_BYTES = _T("  addr   0   1   2   3   4   5   6   7   10  11  12  13  14  15  16  17");
    const TCHAR* ADDRESS_LINE_HEX_WORDS = _T("  addr   0    2    4    6    8    a    c    e");
    const TCHAR* ADDRESS_LINE_HEX_BYTES = _T("  addr   0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f");
    if (m_NumeralMode == MEMMODENUM_OCT && !m_okMemoryByteMode)
        TextOut(hdc, cxChar * 5, 0, ADDRESS_LINE_OCT_WORDS, (int)_tcslen(ADDRESS_LINE_OCT_WORDS));
    else if (m_NumeralMode == MEMMODENUM_OCT && m_okMemoryByteMode)
        TextOut(hdc, cxChar * 5, 0, ADDRESS_LINE_OCT_BYTES, (int)_tcslen(ADDRESS_LINE_OCT_BYTES));
    else if (m_okMemoryByteMode)
        TextOut(hdc, cxChar * 5, 0, ADDRESS_LINE_HEX_BYTES, (int)_tcslen(ADDRESS_LINE_HEX_BYTES));
    else
        TextOut(hdc, cxChar * 5, 0, ADDRESS_LINE_HEX_WORDS, (int)_tcslen(ADDRESS_LINE_HEX_WORDS));

    m_nPageSize = rcClient.bottom / cyLine - 1;

    WORD address = m_wBaseAddress;
    int y = 1 * cyLine;
    for (;;)    // Draw lines
    {
        uint16_t lineAddress = address;

        if (m_NumeralMode == MEMMODENUM_OCT)
            DrawOctalValue(hdc, 6 * cxChar, y, address);
        else
            DrawHexValue(hdc, 7 * cxChar, y, address);

        int x = 14 * cxChar;
        TCHAR wchars[16];
        for (int j = 0; j < 8; j++)    // Draw words as octal value
        {
            // Get word from memory
            int addrtype;
            BOOL okValid;
            WORD wChanged;
            WORD word = MemoryView_GetWordFromMemory(address, okValid, addrtype, wChanged);

            if (address == m_wCurrentAddress)
                ::PatBlt(hdc, x - cxChar / 2, y, m_PostionIncrement, cyLine, PATCOPY);

            if (okValid)
            {
                if (addrtype == ADDRTYPE_ROM)
                    ::SetTextColor(hdc, colorMemoryRom);
                else
                    ::SetTextColor(hdc, (wChanged != 0) ? colorChanged : colorText);

                if (m_NumeralMode == MEMMODENUM_OCT && !m_okMemoryByteMode)
                    DrawOctalValue(hdc, x, y, word);
                else if (m_NumeralMode == MEMMODENUM_OCT && m_okMemoryByteMode)
                {
                    PrintOctalValue(buffer, (word & 0xff));
                    TextOut(hdc, x, y, buffer + 3, 3);
                    PrintOctalValue(buffer, (word >> 8));
                    TextOut(hdc, x + 4 * cxChar, y, buffer + 3, 3);
                }
                else if (m_NumeralMode == MEMMODENUM_HEX && !m_okMemoryByteMode)
                    DrawHexValue(hdc, x, y, word);
                else if (m_NumeralMode == MEMMODENUM_HEX && m_okMemoryByteMode)
                {
                    PrintHexValue(buffer, word);
                    TextOut(hdc, x, y, buffer + 2, 2);
                    TextOut(hdc, x + 3 * cxChar, y, buffer, 2);
                }
            }
            else  // !okValid
            {
                if (addrtype == ADDRTYPE_IO)
                {
                    ::SetTextColor(hdc, colorMemoryIO);
                    TextOut(hdc, x, y, _T("  IO"), 4);
                }
                else
                {
                    ::SetTextColor(hdc, colorMemoryNA);
                    TextOut(hdc, x, y, _T("  NA"), 4);
                }
            }

            // Prepare characters to draw at right
            BYTE ch1 = LOBYTE(word);
            TCHAR wch1 = TranslateDeviceCharToUnicode(ch1);
            if (ch1 < 32) wch1 = _T('·');
            wchars[j * 2] = wch1;
            BYTE ch2 = HIBYTE(word);
            TCHAR wch2 = TranslateDeviceCharToUnicode(ch2);
            if (ch2 < 32) wch2 = _T('·');
            wchars[j * 2 + 1] = wch2;

            address += 2;
            x += m_PostionIncrement;
        }

        // Highlight characters at right
        if (lineAddress <= m_wCurrentAddress && m_wCurrentAddress < lineAddress + 16)
        {
            int xHighlight = x + cxChar + (m_wCurrentAddress - lineAddress) * cxChar;
            ::PatBlt(hdc, xHighlight, y, cxChar * 2, cyLine, PATCOPY);
        }

        // Draw characters at right
        ::SetTextColor(hdc, colorText);
        ::SetBkMode(hdc, TRANSPARENT);
        int xch = x + cxChar;
        TextOut(hdc, xch, y, wchars, 16);

        y += cyLine;
        if (y > rcClip.bottom) break;
    }

    ::SelectObject(hdc, hOldBrush);
    VERIFY(::DeleteObject(hbrHighlight));

    if (::GetFocus() == m_hwndMemoryViewer)
    {
        RECT rcFocus;
        MemoryView_GetCurrentValueRect(&rcFocus, cxChar, cyLine);
        DrawFocusRect(hdc, &rcFocus);
    }

    SelectObject(hdc, hOldFont);
    VERIFY(::DeleteObject(hFont));
}

//////////////////////////////////////////////////////////////////////

LRESULT CALLBACK ProcessListViewViewerWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);

            ProcessListView_OnDraw(hdc);

            EndPaint(hWnd, &ps);
        }
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return (LRESULT)FALSE;
}

void ProcessListView_OnDraw(HDC hdc)
{
    ASSERT(g_pBoard != NULL);

    HFONT hFont = CreateMonospacedFont();
    HGDIOBJ hOldFont = SelectObject(hdc, hFont);

    const size_t PROLENW = 35;  // process descriptor size, words
    LPCTSTR PROCESS_LIST_HEADER = _T("#   Name            Descript Priority State  Mem   Low   High");
    LPCTSTR PROCESS_LIST_DIVIDER = _T("--- ----------------  ------  ------  -----  ----  ----  ----");
    const CMotherboard* pBoard = g_pBoard;
    uint16_t pdptr = pBoard->GetRAMWordView(020572); // pointer to first process-descriptor
    uint16_t freepr = pBoard->GetRAMWordView(020006); // pointer to first free process-descriptor
    uint16_t running = pBoard->GetRAMWordView(020016); // pointer to current process
    //uint16_t maplen = pBoard->GetRAMWordView(020002); // length of ram-bit-map in bytes

    int cxChar, cyLine;  GetFontWidthAndHeight(hdc, &cxChar, &cyLine);

    TextOut(hdc, 32 + 4 + cxChar * 1, cyLine * 0, PROCESS_LIST_HEADER, (int)_tcslen(PROCESS_LIST_HEADER));
    TextOut(hdc, 32 + 4 + cxChar * 1, cyLine * 1, PROCESS_LIST_DIVIDER, (int)_tcslen(PROCESS_LIST_DIVIDER));

    uint16_t procno = 1;
    int y = cyLine * 2;
    while (pdptr != 0 && pdptr != freepr)
    {
        uint16_t procinfo[PROLENW];
        for (int i = 0; i < PROLENW; i++)
            procinfo[i] = MemoryView_GetWordFromHalt((uint16_t)(pdptr + i * 2));

        TCHAR bufname[17];
        for (int i = 0; i < 16; i++)
            bufname[i] = TranslateDeviceCharToUnicode(((const uint8_t*)(procinfo + 24))[i]);
        bufname[16] = 0;

        uint16_t priority = procinfo[22];
        TCHAR bufprio[7];
        if ((priority & 0100000) == 0)
            PrintOctalValue(bufprio, priority);
        else
        {
            PrintOctalValue(bufprio, (uint16_t)(0200000 - priority));
            bufprio[0] = _T('-');
        }

        LPCTSTR statestr;
        if (pdptr == running)
            statestr = _T("Run ");
        else if ((procinfo[19] & 0040000) != 0)
            statestr = _T("Wait");
        else if ((procinfo[19] & 0000400) != 0)
            statestr = _T("Tim ");
        else if ((procinfo[19] & 0000377) != 0)
            statestr = _T("Int ");
        else
            statestr = _T("I/O ");

        LPCTSTR memstr = _T("No memory map  ");
        //TCHAR bufmem[18];
        uint16_t pmem = procinfo[23];
        if (pmem != 0)
        {
            //int memsum = 0, addrlo = maplen * 8, addrhi = 0;
            //for (int i = 0; i < (maplen + 1) / 2; i++)
            //{
            //    uint16_t membits = MemoryView_GetWordFromHalt((uint16_t)(pmem + i * 2));
            //    for (int b = 0; b < 16; b++)
            //    {
            //        if (membits & 1)
            //        {
            //            memsum += 1;
            //        }
            //        membits = membits >> 1;
            //    }
            //}

            //_sntprintf(bufmem, 17, _T("%3dk  %3dk  %3dk"), memsum, 0, 0);
            //memstr = bufmem;
            memstr = _T("");
        }

        TCHAR buffer[256];
        _sntprintf(buffer, 255, _T("%2d  %16s  %06o  %s  %4s   %s"), procno, bufname, pdptr, bufprio, statestr, memstr);
        TextOut(hdc, 32 + 4 + cxChar * 1, y, buffer, (int)_tcslen(buffer));

        pdptr = procinfo[34];  // p.dsucc
        procno++;
        y += cyLine;
    }

    SelectObject(hdc, hOldFont);
    VERIFY(::DeleteObject(hFont));
}


//////////////////////////////////////////////////////////////////////
