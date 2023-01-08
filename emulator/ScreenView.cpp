/*  This file is part of NEONBTL.
    NEONBTL is free software: you can redistribute it and/or modify it under the terms
of the GNU Lesser General Public License as published by the Free Software Foundation,
either version 3 of the License, or (at your option) any later version.
    NEONBTL is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU Lesser General Public License for more details.
    You should have received a copy of the GNU Lesser General Public License along with
NEONBTL. If not, see <http://www.gnu.org/licenses/>. */

// ScreenView.cpp

#include "stdafx.h"
#include <windowsx.h>
#include <mmintrin.h>
#include <Vfw.h>
#include "Main.h"
#include "Views.h"
#include "Emulator.h"
#include "util/BitmapFile.h"

//////////////////////////////////////////////////////////////////////


#define COLOR_BK_BACKGROUND RGB(115,115,115)


HWND g_hwndScreen = NULL;  // Screen View window handle

HDRAWDIB m_hdd = NULL;
BITMAPINFO m_bmpinfo;
HBITMAP m_hbmp = NULL;
DWORD * m_bits = NULL;
int m_ScreenMode = 0;
int m_cxScreenWidth = NEON_SCREEN_WIDTH / 2;
int m_cyScreenHeight = NEON_SCREEN_HEIGHT;
int m_xScreenOffset = 0;
int m_yScreenOffset = 0;
BYTE m_ScreenKeyState[256];
uint8_t m_KeyboardMatrix[8];

void ScreenView_CreateDisplay();
void ScreenView_OnDraw(HDC hdc);
//BOOL ScreenView_OnKeyEvent(WPARAM vkey, BOOL okExtKey, BOOL okPressed);
void ScreenView_OnRButtonDown(int mousex, int mousey);


//////////////////////////////////////////////////////////////////////

void ScreenView_RegisterClass()
{
    WNDCLASSEX wcex;
    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = ScreenViewWndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = g_hInst;
    wcex.hIcon          = NULL;
    wcex.hCursor        = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground  = NULL; //(HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = NULL;
    wcex.lpszClassName  = CLASSNAME_SCREENVIEW;
    wcex.hIconSm        = NULL;

    RegisterClassEx(&wcex);
}

void ScreenView_Init()
{
    m_hdd = DrawDibOpen();
    ScreenView_CreateDisplay();
}

void ScreenView_Done()
{
    if (m_hbmp != NULL)
    {
        VERIFY(::DeleteObject(m_hbmp));
        m_hbmp = NULL;
    }

    DrawDibClose(m_hdd);
}

void ScreenView_CreateDisplay()
{
    ASSERT(g_hwnd != NULL);

    if (m_hbmp != NULL)
    {
        VERIFY(::DeleteObject(m_hbmp));
        m_hbmp = NULL;
    }

    HDC hdc = ::GetDC(g_hwnd);

    m_bmpinfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    m_bmpinfo.bmiHeader.biWidth = m_cxScreenWidth;
    m_bmpinfo.bmiHeader.biHeight = m_cyScreenHeight;
    m_bmpinfo.bmiHeader.biPlanes = 1;
    m_bmpinfo.bmiHeader.biBitCount = 32;
    m_bmpinfo.bmiHeader.biCompression = BI_RGB;
    m_bmpinfo.bmiHeader.biSizeImage = 0;
    m_bmpinfo.bmiHeader.biXPelsPerMeter = 0;
    m_bmpinfo.bmiHeader.biYPelsPerMeter = 0;
    m_bmpinfo.bmiHeader.biClrUsed = 0;
    m_bmpinfo.bmiHeader.biClrImportant = 0;

    m_hbmp = CreateDIBSection(hdc, &m_bmpinfo, DIB_RGB_COLORS, (void **) &m_bits, NULL, 0);

    VERIFY(::ReleaseDC(g_hwnd, hdc));
}

// Create Screen View as child of Main Window
void ScreenView_Create(HWND hwndParent, int x, int y)
{
    ASSERT(hwndParent != NULL);

    int xLeft = x;
    int yTop = y;
    int cxWidth = 4 + NEON_SCREEN_HEIGHT / 2 + 4;
    int cyHeight = 4 + NEON_SCREEN_HEIGHT + 4;

    g_hwndScreen = CreateWindow(
            CLASSNAME_SCREENVIEW, NULL,
            WS_CHILD | WS_VISIBLE,
            xLeft, yTop, cxWidth, cyHeight,
            hwndParent, NULL, g_hInst, NULL);

    // Initialize m_ScreenKeyState
    ::memset(m_ScreenKeyState, 0, sizeof(m_ScreenKeyState));
}

LRESULT CALLBACK ScreenViewWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
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

            ScreenView_PrepareScreen();  //DEBUG
            ScreenView_OnDraw(hdc);

            EndPaint(hWnd, &ps);
        }
        break;
    case WM_LBUTTONDOWN:
        SetFocus(hWnd);
        break;
    case WM_RBUTTONDOWN:
        ScreenView_OnRButtonDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        break;
        //case WM_KEYDOWN:
        //    //if ((lParam & (1 << 30)) != 0)  // Auto-repeats should be ignored
        //    //    return (LRESULT) TRUE;
        //    //return (LRESULT) ScreenView_OnKeyEvent(wParam, (lParam & (1 << 24)) != 0, TRUE);
        //    return (LRESULT) TRUE;
        //case WM_KEYUP:
        //    //return (LRESULT) ScreenView_OnKeyEvent(wParam, (lParam & (1 << 24)) != 0, FALSE);
        //    return (LRESULT) TRUE;
    case WM_SETCURSOR:
        if (::GetFocus() == g_hwndScreen)
        {
            SetCursor(::LoadCursor(NULL, MAKEINTRESOURCE(IDC_IBEAM)));
            return (LRESULT) TRUE;
        }
        else
            return DefWindowProc(hWnd, message, wParam, lParam);
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return (LRESULT)FALSE;
}

void ScreenView_OnRButtonDown(int mousex, int mousey)
{
    ::SetFocus(g_hwndScreen);

    HMENU hMenu = ::CreatePopupMenu();
    ::AppendMenu(hMenu, 0, ID_FILE_SCREENSHOT, _T("Screenshot"));
    ::AppendMenu(hMenu, 0, ID_FILE_SCREENSHOTTOCLIPBOARD, _T("Screenshot to Clipboard"));

    POINT pt = { mousex, mousey };
    ::ClientToScreen(g_hwndScreen, &pt);
    ::TrackPopupMenu(hMenu, 0, pt.x, pt.y, 0, g_hwndScreen, NULL);

    VERIFY(::DestroyMenu(hMenu));
}

int ScreenView_GetScreenMode()
{
    return m_ScreenMode;
}
void ScreenView_SetScreenMode(int newMode)
{
    if (m_ScreenMode == newMode) return;

    m_ScreenMode = newMode;

    // Ask Emulator module for screen width and height
    int cxWidth, cyHeight;
    Emulator_GetScreenSize(newMode, &cxWidth, &cyHeight);
    m_cxScreenWidth = cxWidth;
    m_cyScreenHeight = cyHeight;
    ScreenView_CreateDisplay();

    ScreenView_RedrawScreen();
}

void ScreenView_OnDraw(HDC hdc)
{
    if (m_bits == NULL) return;

    HBRUSH hBrush = ::CreateSolidBrush(COLOR_BK_BACKGROUND);
    HGDIOBJ hOldBrush = ::SelectObject(hdc, hBrush);

    RECT rc;  ::GetClientRect(g_hwndScreen, &rc);
    m_xScreenOffset = 0;
    m_yScreenOffset = 0;
    if (rc.right > m_cxScreenWidth)
    {
        m_xScreenOffset = (rc.right - m_cxScreenWidth) / 2;
        ::PatBlt(hdc, 0, 0, m_xScreenOffset, rc.bottom, PATCOPY);
        ::PatBlt(hdc, rc.right, 0, m_cxScreenWidth + m_xScreenOffset - rc.right, rc.bottom, PATCOPY);
    }
    if (rc.bottom > m_cyScreenHeight)
    {
        m_yScreenOffset = (rc.bottom - m_cyScreenHeight) / 2;
        ::PatBlt(hdc, m_xScreenOffset, 0, m_cxScreenWidth, m_yScreenOffset, PATCOPY);
        int frombottom = rc.bottom - m_yScreenOffset - m_cyScreenHeight;
        ::PatBlt(hdc, m_xScreenOffset, rc.bottom, m_cxScreenWidth, -frombottom, PATCOPY);
    }

    ::SelectObject(hdc, hOldBrush);
    VERIFY(::DeleteObject(hBrush));

    DrawDibDraw(m_hdd, hdc,
            m_xScreenOffset, m_yScreenOffset, -1, -1,
            &m_bmpinfo.bmiHeader, m_bits, 0, 0,
            m_cxScreenWidth, m_cyScreenHeight,
            0);
}

void ScreenView_RedrawScreen()
{
    ScreenView_PrepareScreen();

    HDC hdc = ::GetDC(g_hwndScreen);
    ScreenView_OnDraw(hdc);
    VERIFY(::ReleaseDC(g_hwndScreen, hdc));
}

void ScreenView_PrepareScreen()
{
    if (m_bits == NULL) return;

    Emulator_PrepareScreenRGB32(m_bits, m_ScreenMode);
}

#define NOKEY 0x0000

// ¿–2 = Esc; “‡· = Tab; «¡ = Backspace;  1.. 5 = F1..F5
// —¡–Œ— = F11; —“Œœ = F12; œŒÃ ”—“ »—œ = F6..F8
// Õ– = LShift; ”œ– = LCtrl; ¿À‘ = RShift; √–¿‘ = RCtrl
// +; = `~
const uint16_t arrPcscan2VscanRus[256] =    // Device keys from PC keys, RUS
{
    /*         0      1      2      3      4      5      6      7      8      9      a      b      c      d      e      f  */
    /*0*/    NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, 0x503, 0x180, NOKEY, NOKEY, NOKEY, 0x608, NOKEY, NOKEY,
    /*1*/    NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY,
    /*2*/    NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, 0x420, 0x610, 0x508, 0x510, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY,
    /*3*/    0x720, 0x102, 0x104, 0x103, 0x008, 0x110, 0x105, 0x020, 0x006, 0x706, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY,
    /*4*/    NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY,
    /*5*/    NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY,
    /*6*/    0x580, 0x501, 0x601, 0x701, 0x502, 0x602, 0x702, 0x540, 0x640, 0x740, NOKEY, 0x504, NOKEY, 0x140, 0x680, NOKEY,
    /*7*/    0x002, 0x004, 0x003, 0x010, 0x005, 0x703, 0x603, 0x604, NOKEY, NOKEY, 0x704, 0x240, NOKEY, NOKEY, NOKEY, NOKEY,
    /*8*/    NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY,
    /*9*/    NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY,
    /*a*/    0x440, 0x480, 0x280, 0x380, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY,
    /*b*/    NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY,
    /*c*/    0x001, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY,
    /*d*/    NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY,
    /*e*/    NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY,
    /*f*/    NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY,
};
// @ = '; \ = \; / = /?; _ = -_; -= = =+; :* = ;:
const uint16_t arrPcscan2VscanLat[256] =    // Device keys from PC keys, LAT
{
    /*         0      1      2      3      4      5      6      7      8      9      a      b      c      d      e      f  */
    /*0*/    NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, 0x503, 0x180, NOKEY, NOKEY, NOKEY, 0x608, NOKEY, NOKEY,
    /*1*/    NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, 0x080, NOKEY, NOKEY, NOKEY, NOKEY,
    /*2*/    0x408, NOKEY, NOKEY, NOKEY, NOKEY, 0x420, 0x610, 0x508, 0x510, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY,
    /*3*/    0x720, 0x102, 0x104, 0x103, 0x008, 0x110, 0x105, 0x020, 0x006, 0x706, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY,
    /*4*/    NOKEY, 0x303, 0x320, 0x202, 0x206, 0x108, 0x201, 0x205, 0x620, 0x308, 0x101, 0x203, 0x220, 0x403, 0x210, 0x305,
    /*5*/    0x208, 0x301, 0x310, 0x404, 0x410, 0x204, 0x506, 0x304, 0x405, 0x302, 0x606, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY,
    /*6*/    0x580, 0x501, 0x601, 0x701, 0x502, 0x602, 0x702, 0x540, 0x640, 0x740, NOKEY, 0x504, NOKEY, 0x140, 0x680, NOKEY,
    /*7*/    0x002, 0x004, 0x003, 0x010, 0x005, 0x703, 0x603, 0x604, NOKEY, NOKEY, 0x704, 0x240, NOKEY, NOKEY, NOKEY, NOKEY,
    /*8*/    NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY,
    /*9*/    NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY,
    /*a*/    0x440, 0x480, 0x280, 0x380, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY,
    /*b*/    NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, 0x708, 0x705, 0x406, 0x605, 0x505, 0x710,
    /*c*/    0x001, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY,
    /*d*/    NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, 0x120, 0x520, 0x106, 0x306, NOKEY,
    /*e*/    NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY,
    /*f*/    NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY,
};

void ScreenView_ScanKeyboard()
{
    if (! g_okEmulatorRunning) return;
    if (::GetFocus() != g_hwndScreen)
        return;

    // Read current keyboard state
    BYTE keys[256];
    VERIFY(::GetKeyboardState(keys));

    //TODO: ¬˚·Ë‡ÂÏ Ú‡·ÎËˆÛ Ï‡ÔÔËÌ„‡ ‚ Á‡‚ËÒËÏÓÒÚË ÓÚ ÙÎ‡„‡ –”—/À¿“
    const uint16_t* pTable = arrPcscan2VscanLat;

    uint8_t matrix[8];
    ::memset(matrix, 0, sizeof(matrix));

    // Check every key for state change
    for (int scan = 0; scan < 256; scan++)
    {
        WORD vscan = pTable[scan];
        BYTE newstate = keys[scan];
        BYTE oldstate = m_ScreenKeyState[scan];
        if ((newstate & 128) != (oldstate & 128))  // Key state changed - key pressed or released
        {
            if ((newstate & 128) != 0 && scan > 2)
                DebugPrintFormat(_T("Key PC: 0x%02x 0x%04x\r\n"), scan, vscan);
            KeyboardView_KeyEvent(vscan, (newstate & 128) != 0);
        }
        if (vscan != 0 && (newstate & 128) != 0)
        {
            matrix[(vscan >> 8) & 7] |= (vscan & 0xff);
        }
    }

    // Check if we have a key pressed on KeyboardView
    WORD kvkey = KeyboardView_GetKeyPressed();
    if (kvkey != 0)
    {
        matrix[(kvkey >> 8) & 7] |= (kvkey & 0xff);
    }

    // Save keyboard state
    ::memcpy(m_ScreenKeyState, keys, 256);

    ::memcpy(m_KeyboardMatrix, matrix, sizeof(matrix));

    Emulator_UpdateKeyboardMatrix(m_KeyboardMatrix);
}

BOOL ScreenView_SaveScreenshot(LPCTSTR sFileName)
{
    ASSERT(sFileName != NULL);
    ASSERT(m_bits != NULL);

    uint32_t* pBits = (uint32_t*) ::calloc(m_cxScreenWidth * m_cyScreenHeight, sizeof(uint32_t));
    Emulator_PrepareScreenRGB32(pBits, m_ScreenMode);

    LPCTSTR sFileNameExt = _tcsrchr(sFileName, _T('.'));
    BitmapFileFormat format = BitmapFileFormatPng;
    if (sFileNameExt != NULL && _tcsicmp(sFileNameExt, _T(".bmp")) == 0)
        format = BitmapFileFormatBmp;
    else if (sFileNameExt != NULL && (_tcsicmp(sFileNameExt, _T(".tif")) == 0 || _tcsicmp(sFileNameExt, _T(".tiff")) == 0))
        format = BitmapFileFormatTiff;
    bool result = BitmapFile_SaveImageFile(
            (const uint32_t *)pBits, sFileName, format, m_cxScreenWidth, m_cyScreenHeight);

    ::free(pBits);

    return result;
}

HGLOBAL ScreenView_GetScreenshotAsDIB()
{
    void* pBits = ::calloc(m_cxScreenWidth * m_cyScreenHeight, 4);
    Emulator_PrepareScreenRGB32(pBits, m_ScreenMode);

    BITMAPINFOHEADER bi;
    ::ZeroMemory(&bi, sizeof(BITMAPINFOHEADER));
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = m_cxScreenWidth;
    bi.biHeight = m_cyScreenHeight;
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;
    bi.biSizeImage = bi.biWidth * bi.biHeight * 4;

    HGLOBAL hDIB = ::GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + bi.biSizeImage);
    if (hDIB == NULL)
    {
        ::free(pBits);
        return NULL;
    }

    LPBYTE p = (LPBYTE)::GlobalLock(hDIB);
    ::CopyMemory(p, &bi, sizeof(BITMAPINFOHEADER));
    p += sizeof(BITMAPINFOHEADER);
    for (int line = 0; line < m_cyScreenHeight; line++)
    {
        LPBYTE psrc = (LPBYTE)pBits + line * m_cxScreenWidth * 4;
        ::CopyMemory(p, psrc, m_cxScreenWidth * 4);
        p += m_cxScreenWidth * 4;
    }
    ::GlobalUnlock(hDIB);

    ::free(pBits);

    return hDIB;
}


//////////////////////////////////////////////////////////////////////
