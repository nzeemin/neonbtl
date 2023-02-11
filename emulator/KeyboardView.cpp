/*  This file is part of NEONBTL.
    NEONBTL is free software: you can redistribute it and/or modify it under the terms
of the GNU Lesser General Public License as published by the Free Software Foundation,
either version 3 of the License, or (at your option) any later version.
    NEONBTL is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU Lesser General Public License for more details.
    You should have received a copy of the GNU Lesser General Public License along with
NEONBTL. If not, see <http://www.gnu.org/licenses/>. */

// KeyboardView.cpp

#include "stdafx.h"
#include "Main.h"
#include "Views.h"
#include "Emulator.h"

//////////////////////////////////////////////////////////////////////


HWND g_hwndKeyboard = (HWND)INVALID_HANDLE_VALUE;  // Keyboard View window handle

int m_nKeyboardBitmapLeft = 0;
int m_nKeyboardBitmapTop = 0;
WORD m_nKeyboardKeyPressed = 0;  // NEON scan-code for the key pressed, or 0

void KeyboardView_OnDraw(HDC hdc);
WORD KeyboardView_GetKeyByPoint(int x, int y);
void Keyboard_DrawKey(HDC hdc, WORD keyscan);


//////////////////////////////////////////////////////////////////////

// Keyboard key mapping to bitmap
const WORD m_arrKeyboardKeys[] =
{
    /*   x1,y1    w,h    NEONscan  */
    18,  15,  42, 27,    0x002,     // K1
    62,  15,  42, 27,    0x004,     // K2
    106, 15,  42, 27,    0x003,     // K3
    151, 15,  42, 27,    0x010,     // K4
    195, 15,  42, 27,    0x005,     // K5
    343, 15,  42, 27,    0x703,     // POM
    387, 15,  42, 27,    0x603,     // UST
    431, 15,  42, 27,    0x604,     // ISP
    506, 15,  42, 27,    0x704,     // SBROS (RESET)
    551, 15,  42, 27,    0x240,     // STOP

    18,  56,  28, 27,    0x080,     // AR2
    47,  56,  28, 27,    0x001,     // ; +
    77,  56,  27, 27,    0x102,     // 1
    106, 56,  28, 27,    0x104,     // 2
    136, 56,  27, 27,    0x103,     // 3
    165, 56,  28, 27,    0x008,     // 4
    195, 56,  27, 27,    0x110,     // 5
    224, 56,  28, 27,    0x105,     // 6
    254, 56,  27, 27,    0x540,     // 7
    283, 56,  28, 27,    0x006,     // 8
    313, 56,  27, 27,    0x706,     // 9
    342, 56,  28, 27,    0x720,     // 0
    372, 56,  27, 27,    0x705,     // - =
    401, 56,  28, 27,    0x710,     // / ?
    431, 56,  42, 27,    0x503,     // Backspace

    18,  86,  42, 27,    0x180,     // TAB
    62,  86,  27, 27,    0x101,     // Й J
    91,  86,  28, 27,    0x202,     // Ц C
    121, 86,  27, 27,    0x204,     // У U
    150, 86,  28, 27,    0x203,     // К K
    180, 86,  27, 27,    0x108,     // Е E
    210, 86,  28, 27,    0x210,     // Н N
    239, 86,  27, 27,    0x205,     // Г G
    269, 86,  27, 27,    0x120,     // Ш [
    298, 86,  28, 27,    0x106,     // Щ ]
    328, 86,  27, 27,    0x606,     // З Z
    357, 86,  28, 27,    0x620,     // Х H
    387, 86,  27, 27,    0x605,     // Ъ
    416, 86,  28, 27,    0x708,     // : *

    18,  115, 49, 27,    0x280,     // UPR
    69,  115, 28, 27,    0x201,     // Ф F
    99,  115, 27, 27,    0x302,     // Ы Y
    128, 115, 28, 27,    0x304,     // В W
    158, 115, 27, 27,    0x303,     // А A
    187, 115, 28, 27,    0x208,     // П P
    217, 115, 27, 27,    0x310,     // Р R
    246, 115, 28, 27,    0x305,     // О O
    276, 115, 27, 27,    0x220,     // Л L
    305, 115, 28, 27,    0x206,     // Д D
    335, 115, 27, 27,    0x506,     // Ж V
    364, 115, 28, 27,    0x520,     // Э Backslash
    394, 115, 35, 27,    0x505,     // . >
    431, 115, 15, 27,    0x608,     // ENTER - left part
    446, 86,  27, 56,    0x608,     // ENTER - right part

    18,  145, 34, 27,    0x480,     // ALF
    55,  145, 27, 27,    0x380,     // GRAF
    84,  145, 27, 27,    0x301,     // Я Q
    114, 145, 27, 27,    0x402,     // Ч ^
    143, 145, 27, 27,    0x404,     // С S
    173, 145, 27, 27,    0x403,     // М
    202, 145, 27, 27,    0x308,     // И I
    232, 145, 27, 27,    0x410,     // Т
    261, 145, 27, 27,    0x405,     // Ь X
    291, 145, 27, 27,    0x320,     // Б B
    320, 145, 28, 27,    0x306,     // Ю @
    350, 145, 34, 27,    0x406,     // , <

    18,  174, 56, 27,    0x440,     // Left Shift
    77,  174, 34, 27,    0x401,     // FIKS
    114, 174, 211, 27,   0x408,     // Space bar
    328, 174, 56, 27,    0x440,     // Right Shift

    387, 145, 27, 56,    0x420,     // Left
    416, 145, 28, 27,    0x610,     // Up
    416, 174, 28, 27,    0x510,     // Down
    446, 145, 27, 56,    0x508,     // Right

    506, 56,  28, 27,    0x504,     // + NumPad
    536, 56,  27, 27,    0x140,     // - NumPad
    565, 56,  28, 27,    0x040,     // , NumPad
    506, 86,  28, 27,    0x540,     // 7 NumPad
    536, 86,  27, 27,    0x640,     // 8 NumPad
    565, 86,  28, 27,    0x740,     // 9 NumPad
    506, 115, 28, 27,    0x502,     // 4 NumPad
    536, 115, 27, 27,    0x110,     // 5 NumPad
    565, 115, 28, 27,    0x105,     // 6 NumPad
    506, 145, 28, 27,    0x501,     // 1 NumPad
    536, 145, 27, 27,    0x601,     // 2 NumPad
    565, 145, 28, 27,    0x701,     // 3 NumPad
    506, 174, 28, 27,    0x580,     // 0 NumPad
    536, 174, 27, 27,    0x680,     // . NumPad
    565, 174, 28, 27,    0x780,     // ENTER NumPad

};
const int m_nKeyboardKeysCount = sizeof(m_arrKeyboardKeys) / sizeof(WORD) / 5;

//////////////////////////////////////////////////////////////////////


void KeyboardView_RegisterClass()
{
    WNDCLASSEX wcex;
    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = KeyboardViewWndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = g_hInst;
    wcex.hIcon          = NULL;
    wcex.hCursor        = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_BTNFACE + 1);
    wcex.lpszMenuName   = NULL;
    wcex.lpszClassName  = CLASSNAME_KEYBOARDVIEW;
    wcex.hIconSm        = NULL;

    RegisterClassEx(&wcex);
}

void KeyboardView_Init()
{
}

void KeyboardView_Done()
{
}

void KeyboardView_Create(HWND hwndParent, int x, int y, int width, int height)
{
    ASSERT(hwndParent != NULL);

    g_hwndKeyboard = CreateWindow(
            CLASSNAME_KEYBOARDVIEW, NULL,
            WS_CHILD | WS_VISIBLE,
            x, y, width, height,
            hwndParent, NULL, g_hInst, NULL);
}

LRESULT CALLBACK KeyboardViewWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);

            KeyboardView_OnDraw(hdc);

            EndPaint(hWnd, &ps);
        }
        break;
    case WM_SETCURSOR:
        {
            POINT ptCursor;  ::GetCursorPos(&ptCursor);
            ::ScreenToClient(g_hwndKeyboard, &ptCursor);
            WORD keyscan = KeyboardView_GetKeyByPoint(ptCursor.x, ptCursor.y);
            LPCTSTR cursor = (keyscan == 0) ? IDC_ARROW : IDC_HAND;
            ::SetCursor(::LoadCursor(NULL, cursor));
        }
        return (LRESULT)TRUE;
    case WM_LBUTTONDOWN:
        {
            int x = LOWORD(lParam);
            int y = HIWORD(lParam);
            WORD keyscan = KeyboardView_GetKeyByPoint(x, y);
            if (keyscan != 0)
            {
                // Fire keydown event and capture mouse
                //ScreenView_KeyEvent(keyscan, TRUE);
                ::SetCapture(g_hwndKeyboard);

                // Draw focus frame for the key pressed
                HDC hdc = ::GetDC(g_hwndKeyboard);
                Keyboard_DrawKey(hdc, keyscan);
                ::ReleaseDC(g_hwndKeyboard, hdc);

                // Remember key pressed
                m_nKeyboardKeyPressed = keyscan;
            }
        }
        break;
    case WM_LBUTTONUP:
        if (m_nKeyboardKeyPressed != 0)
        {
            // Fire keyup event and release mouse
            //ScreenView_KeyEvent(m_nKeyboardKeyPressed, FALSE);
            ::ReleaseCapture();

            // Draw focus frame for the released key
            HDC hdc = ::GetDC(g_hwndKeyboard);
            Keyboard_DrawKey(hdc, m_nKeyboardKeyPressed);
            ::ReleaseDC(g_hwndKeyboard, hdc);

            m_nKeyboardKeyPressed = 0;
        }
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return (LRESULT)FALSE;
}

void KeyboardView_OnDraw(HDC hdc)
{
    HBITMAP hBmp = ::LoadBitmap(g_hInst, MAKEINTRESOURCE(IDB_KEYBOARD));
    HBITMAP hBmpMask = ::LoadBitmap(g_hInst, MAKEINTRESOURCE(IDB_KEYBOARDMASK));

    HDC hdcMem = ::CreateCompatibleDC(hdc);
    HGDIOBJ hOldBitmap = ::SelectObject(hdcMem, hBmp);

    RECT rc;  ::GetClientRect(g_hwndKeyboard, &rc);

    BITMAP bitmap;
    VERIFY(::GetObject(hBmp, sizeof(BITMAP), &bitmap));
    int cxBitmap = (int) bitmap.bmWidth;
    int cyBitmap = (int) bitmap.bmHeight;
    m_nKeyboardBitmapLeft = (rc.right - cxBitmap) / 2;
    m_nKeyboardBitmapTop = (rc.bottom - cyBitmap) / 2;
    ::MaskBlt(hdc, m_nKeyboardBitmapLeft, m_nKeyboardBitmapTop, cxBitmap, cyBitmap, hdcMem, 0, 0,
            hBmpMask, 0, 0, MAKEROP4(SRCCOPY, SRCAND));

    ::SelectObject(hdcMem, hOldBitmap);
    ::DeleteDC(hdcMem);
    ::DeleteObject(hBmp);

    if (m_nKeyboardKeyPressed != 0)
        Keyboard_DrawKey(hdc, m_nKeyboardKeyPressed);

    //// Show key mappings
    //HFONT hfont = CreateDialogFont();
    //HGDIOBJ hOldFont = ::SelectObject(hdc, hfont);
    //::SetBkMode(hdc, TRANSPARENT);
    //::SetTextColor(hdc, RGB(240, 0, 0));
    //for (int i = 0; i < m_nKeyboardKeysCount; i++)
    //{
    //    if (m_arrKeyboardKeys[i * 5 + 4] == 0)
    //        continue;
    //    TCHAR text[10];
    //    _sntprintf(text, sizeof(text) / sizeof(TCHAR) - 1, _T("%03x"), (int)m_arrKeyboardKeys[i * 5 + 4]);

    //    RECT rcKey;
    //    rcKey.left = m_nKeyboardBitmapLeft + m_arrKeyboardKeys[i * 5];
    //    rcKey.top = m_nKeyboardBitmapTop + m_arrKeyboardKeys[i * 5 + 1];
    //    rcKey.right = rcKey.left + m_arrKeyboardKeys[i * 5 + 2];
    //    rcKey.bottom = rcKey.top + m_arrKeyboardKeys[i * 5 + 3];

    //    //::DrawFocusRect(hdc, &rcKey);
    //    ::DrawText(hdc, text, wcslen(text), &rcKey, DT_NOPREFIX | DT_SINGLELINE | DT_RIGHT | DT_BOTTOM);
    //}
    //::SelectObject(hdc, hOldFont);
    //VERIFY(::DeleteObject(hfont));
}

// Returns: NEON scan-code of key under the cursor position, or 0 if not found
WORD KeyboardView_GetKeyByPoint(int x, int y)
{
    for (int i = 0; i < m_nKeyboardKeysCount; i++)
    {
        RECT rcKey;
        rcKey.left = m_nKeyboardBitmapLeft + m_arrKeyboardKeys[i * 5];
        rcKey.top = m_nKeyboardBitmapTop + m_arrKeyboardKeys[i * 5 + 1];
        rcKey.right = rcKey.left + m_arrKeyboardKeys[i * 5 + 2];
        rcKey.bottom = rcKey.top + m_arrKeyboardKeys[i * 5 + 3];

        if (x >= rcKey.left && x < rcKey.right && y >= rcKey.top && y < rcKey.bottom)
        {
            return m_arrKeyboardKeys[i * 5 + 4];
        }
    }
    return 0;
}

void Keyboard_DrawKey(HDC hdc, WORD keyscan)
{
    for (int i = 0; i < m_nKeyboardKeysCount; i++)
        if (keyscan == m_arrKeyboardKeys[i * 5 + 4])
        {
            int x = m_nKeyboardBitmapLeft + m_arrKeyboardKeys[i * 5];
            int y = m_nKeyboardBitmapTop + m_arrKeyboardKeys[i * 5 + 1];
            int w = m_arrKeyboardKeys[i * 5 + 2];
            int h = m_arrKeyboardKeys[i * 5 + 3];
            ::PatBlt(hdc, x, y, w, h, PATINVERT);
        }
}

// Get pressed key if any, to call from ScreenView
WORD KeyboardView_GetKeyPressed()
{
    return m_nKeyboardKeyPressed;
}
// Display key pressed, to call from ScreenView
void KeyboardView_KeyEvent(WORD keyscan, BOOL pressed)
{
    HDC hdc = ::GetDC(g_hwndKeyboard);
    Keyboard_DrawKey(hdc, keyscan);
    ::ReleaseDC(g_hwndKeyboard, hdc);
}


//////////////////////////////////////////////////////////////////////
