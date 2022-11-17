/*  This file is part of NEONBTL.
    NEONBTL is free software: you can redistribute it and/or modify it under the terms
of the GNU Lesser General Public License as published by the Free Software Foundation,
either version 3 of the License, or (at your option) any later version.
    NEONBTL is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU Lesser General Public License for more details.
    You should have received a copy of the GNU Lesser General Public License along with
NEONBTL. If not, see <http://www.gnu.org/licenses/>. */

// DisasmView.cpp

#include "stdafx.h"
#include <vector>

#include <commdlg.h>
#include <windowsx.h>
#include "Main.h"
#include "Views.h"
#include "ToolWindow.h"
#include "Dialogs.h"
#include "Emulator.h"
#include "emubase/Emubase.h"

//////////////////////////////////////////////////////////////////////


enum DisasmSubtitleType
{
    SUBTYPE_NONE         = 0,
    SUBTYPE_COMMENT      = 1,
    SUBTYPE_BLOCKCOMMENT = 2,
    SUBTYPE_DATA         = 4,
};

struct DisasmSubtitleItem
{
    uint16_t address;
    DisasmSubtitleType type;
    LPCTSTR comment;
};

enum DisasmLineType
{
    LINETYPE_NONE     = 0,  // Empty line
    LINETYPE_DATA     = 1,  // Line contains a data (non-instruction)
    LINETYPE_INSTR    = 2,  // Line contains a disassembled instruction
    LINETYPE_JUMP     = 4,  // Line has jump
    LINETYPE_SUBTITLE = 8,  // Line has subtitle comment
};

struct DisasmLineItem
{
    int      type;          // Combination of DisasmLineType values
    uint16_t address;       // Line address for LINETYPE_DATA
    int      addrtype;      // Address type for LINETYPE_DATA, see ADDRTYPE_XXX constants
    uint16_t value;         // Data on the address for LINETYPE_DATA
    TCHAR    strInstr[8];   // Disassembled instruction for LINETYPE_DISASM
    TCHAR    strArg[32];    // Disassembled instruction arguments for LINETYPE_DISASM
    int      jumpdelta;     // Jump delta for LINETYPE_JUMP
    const DisasmSubtitleItem* pSubItem;  // Link to subtitles item for LINETYPE_SUBTITLE
};

HWND g_hwndDisasm = (HWND) INVALID_HANDLE_VALUE;  // Disasm View window handle
WNDPROC m_wndprocDisasmToolWindow = NULL;  // Old window proc address of the ToolWindow

HWND m_hwndDisasmViewer = (HWND) INVALID_HANDLE_VALUE;

uint16_t m_wDisasmBaseAddr = 0;

BOOL m_okDisasmSubtitles = FALSE;
TCHAR* m_strDisasmSubtitles = NULL;
DisasmSubtitleItem* m_pDisasmSubtitleItems = NULL;
int m_nDisasmSubtitleMax = 0;
int m_nDisasmSubtitleCount = 0;

const int MAX_DISASMLINECOUNT = 50;
DisasmLineItem* m_pDisasmLineItems = nullptr;

BOOL  m_okDisasmJumpPredict;
TCHAR m_strDisasmHint[42] = { 0 };
TCHAR m_strDisasmHint2[42] = { 0 };

int m_cxDisasmBreakpointZone = 16;  // Width of breakpoint zone at the left, for mouse click
int m_cyDisasmLine = 10;  // cyLine for the current font

void DisasmView_UpdateWindowText();
BOOL DisasmView_OnKeyDown(WPARAM vkey, LPARAM lParam);
void DisasmView_OnLButtonDown(WPARAM wParam, LPARAM lParam);
void DisasmView_DoSubtitles();
BOOL DisasmView_ParseSubtitles();
void DisasmView_DoDraw(HDC hdc);
int  DisasmView_DrawDisassemble(HDC hdc, CProcessor* pProc, WORD base, WORD previous, int x, int y);


//////////////////////////////////////////////////////////////////////


void DisasmView_RegisterClass()
{
    WNDCLASSEX wcex;
    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = DisasmViewViewerWndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = g_hInst;
    wcex.hIcon          = NULL;
    wcex.hCursor        = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName   = NULL;
    wcex.lpszClassName  = CLASSNAME_DISASMVIEW;
    wcex.hIconSm        = NULL;

    RegisterClassEx(&wcex);
}

void DisasmView_Init()
{
    m_pDisasmLineItems = static_cast<DisasmLineItem*>(::calloc(MAX_DISASMLINECOUNT, sizeof(DisasmLineItem)));
}

void DisasmView_Done()
{
    if (m_strDisasmSubtitles != nullptr)
    {
        free(m_strDisasmSubtitles);  m_strDisasmSubtitles = nullptr;
    }
    if (m_pDisasmSubtitleItems != nullptr)
    {
        free(m_pDisasmSubtitleItems);
        m_pDisasmSubtitleItems = nullptr;
    }

    if (m_pDisasmLineItems != nullptr)
    {
        free(m_pDisasmLineItems);
        m_pDisasmLineItems = nullptr;
    }
}

void DisasmView_Create(HWND hwndParent, int x, int y, int width, int height)
{
    ASSERT(hwndParent != NULL);

    g_hwndDisasm = CreateWindow(
            CLASSNAME_TOOLWINDOW, NULL,
            WS_CHILD | WS_VISIBLE,
            x, y, width, height,
            hwndParent, NULL, g_hInst, NULL);
    DisasmView_UpdateWindowText();

    // ToolWindow subclassing
    m_wndprocDisasmToolWindow = (WNDPROC) LongToPtr( SetWindowLongPtr(
            g_hwndDisasm, GWLP_WNDPROC, PtrToLong(DisasmViewWndProc)) );

    RECT rcClient;  GetClientRect(g_hwndDisasm, &rcClient);

    m_hwndDisasmViewer = CreateWindowEx(
            WS_EX_STATICEDGE,
            CLASSNAME_DISASMVIEW, NULL,
            WS_CHILD | WS_VISIBLE,
            0, 0, rcClient.right, rcClient.bottom,
            g_hwndDisasm, NULL, g_hInst, NULL);
}

void DisasmView_Redraw()
{
    RedrawWindow(g_hwndDisasm, NULL, NULL, RDW_ERASE | RDW_INVALIDATE | RDW_ALLCHILDREN);
}

// Adjust position of client windows
void DisasmView_AdjustWindowLayout()
{
    RECT rc;  GetClientRect(g_hwndDisasm, &rc);

    if (m_hwndDisasmViewer != (HWND) INVALID_HANDLE_VALUE)
        SetWindowPos(m_hwndDisasmViewer, NULL, 0, 0, rc.right, rc.bottom, SWP_NOZORDER);
}

LRESULT CALLBACK DisasmViewWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    LRESULT lResult;
    switch (message)
    {
    case WM_DESTROY:
        g_hwndDisasm = (HWND) INVALID_HANDLE_VALUE;  // We are closed! Bye-bye!..
        return CallWindowProc(m_wndprocDisasmToolWindow, hWnd, message, wParam, lParam);
    case WM_SIZE:
        lResult = CallWindowProc(m_wndprocDisasmToolWindow, hWnd, message, wParam, lParam);
        DisasmView_AdjustWindowLayout();
        return lResult;
    default:
        return CallWindowProc(m_wndprocDisasmToolWindow, hWnd, message, wParam, lParam);
    }
    //return (LRESULT)FALSE;
}

LRESULT CALLBACK DisasmViewViewerWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);

            DisasmView_DoDraw(hdc);

            EndPaint(hWnd, &ps);
        }
        break;
    case WM_LBUTTONDOWN:
        DisasmView_OnLButtonDown(wParam, lParam);
        break;
    case WM_KEYDOWN:
        return (LRESULT) DisasmView_OnKeyDown(wParam, lParam);
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        ::InvalidateRect(hWnd, NULL, TRUE);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return (LRESULT)FALSE;
}

BOOL DisasmView_OnKeyDown(WPARAM vkey, LPARAM /*lParam*/)
{
    switch (vkey)
    {
    case 0x53:  // S - Load/Unload Subtitles
        DisasmView_DoSubtitles();
        break;
    case VK_ESCAPE:
        ConsoleView_Activate();
        break;
    default:
        return TRUE;
    }
    return FALSE;
}

void DisasmView_OnLButtonDown(WPARAM /*wParam*/, LPARAM lParam)
{
    ::SetFocus(m_hwndDisasmViewer);

    // For click in the breakpoint zone at the left - try to find the line/address and add/remove breakpoint
    if (GET_X_LPARAM(lParam) < m_cxDisasmBreakpointZone)
    {
        int lineindex = (GET_Y_LPARAM(lParam) - 2) / m_cyDisasmLine;
        if (lineindex >= 0 && lineindex < MAX_DISASMLINECOUNT)
        {
            DisasmLineItem* pLineItem = m_pDisasmLineItems + lineindex;
            if (pLineItem->type != LINETYPE_NONE)
            {
                WORD address = pLineItem->address;
                if (!Emulator_IsBreakpoint(address))
                {
                    bool result = Emulator_AddCPUBreakpoint(address);
                    if (!result)
                        AlertWarningFormat(_T("Failed to add breakpoint at %06ho."), address);
                }
                else
                {
                    bool result = Emulator_RemoveCPUBreakpoint(address);
                    if (!result)
                        AlertWarningFormat(_T("Failed to remove breakpoint at %06ho."), address);
                }

                DebugView_Redraw();
                DisasmView_Redraw();
            }
        }
    }
}

void DisasmView_UpdateWindowText()
{
    if (m_okDisasmSubtitles)
        ::SetWindowText(g_hwndDisasm, _T("Disassemble - Subtitles"));
    else
        ::SetWindowText(g_hwndDisasm, _T("Disassemble"));
}

void DisasmView_ResizeSubtitleArray(int newSize)
{
    DisasmSubtitleItem* pNewMemory = (DisasmSubtitleItem*) ::calloc(newSize, sizeof(DisasmSubtitleItem));
    if (m_pDisasmSubtitleItems != NULL)
    {
        ::memcpy(pNewMemory, m_pDisasmSubtitleItems, sizeof(DisasmSubtitleItem) * m_nDisasmSubtitleMax);
        ::free(m_pDisasmSubtitleItems);
    }

    m_pDisasmSubtitleItems = pNewMemory;
    m_nDisasmSubtitleMax = newSize;
}
void DisasmView_AddSubtitle(WORD address, int type, LPCTSTR pCommentText)
{
    if (m_nDisasmSubtitleCount >= m_nDisasmSubtitleMax)
    {
        // Расширить массив
        int newsize = m_nDisasmSubtitleMax + m_nDisasmSubtitleMax / 2;
        DisasmView_ResizeSubtitleArray(newsize);
    }

    m_pDisasmSubtitleItems[m_nDisasmSubtitleCount].address = address;
    m_pDisasmSubtitleItems[m_nDisasmSubtitleCount].type = (DisasmSubtitleType) type;
    m_pDisasmSubtitleItems[m_nDisasmSubtitleCount].comment = pCommentText;
    m_nDisasmSubtitleCount++;
}

void DisasmView_DoSubtitles()
{
    if (m_okDisasmSubtitles)  // Reset subtitles
    {
        ::free(m_strDisasmSubtitles);  m_strDisasmSubtitles = NULL;
        ::free(m_pDisasmSubtitleItems);  m_pDisasmSubtitleItems = NULL;
        m_nDisasmSubtitleMax = m_nDisasmSubtitleCount = 0;
        m_okDisasmSubtitles = FALSE;
        DisasmView_UpdateWindowText();
        DisasmView_OnUpdate();  // We have to re-build the list of lines to show
        return;
    }

    // File Open dialog
    TCHAR bufFileName[MAX_PATH];
    BOOL okResult = ShowOpenDialog(g_hwnd,
            _T("Open Disassemble Subtitles"),
            _T("Subtitles (*.lst)\0*.lst\0All Files (*.*)\0*.*\0\0"),
            bufFileName);
    if (! okResult) return;

    // Load subtitles text from the file
    HANDLE hSubFile = CreateFile(bufFileName, GENERIC_READ, FILE_SHARE_READ, NULL,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hSubFile == INVALID_HANDLE_VALUE)
    {
        AlertWarning(_T("Failed to load subtitles file."));
        return;
    }
    DWORD dwSubFileSize = ::GetFileSize(hSubFile, NULL);
    if (dwSubFileSize > 1024 * 1024)
    {
        ::CloseHandle(hSubFile);
        AlertWarning(_T("Subtitles file is too big (over 1 MB)."));
        return;
    }

    m_strDisasmSubtitles = (TCHAR*) ::calloc(dwSubFileSize + 2, 1);
    DWORD dwBytesRead;
    ::ReadFile(hSubFile, m_strDisasmSubtitles, dwSubFileSize, &dwBytesRead, NULL);
    ASSERT(dwBytesRead == dwSubFileSize);
    ::CloseHandle(hSubFile);

    // Estimate comment count and allocate memory
    int estimateSubtitleCount = dwSubFileSize / (75 * sizeof(TCHAR));
    if (estimateSubtitleCount < 256)
        estimateSubtitleCount = 256;
    DisasmView_ResizeSubtitleArray(estimateSubtitleCount);

    // Parse subtitles
    if (!DisasmView_ParseSubtitles())
    {
        ::free(m_strDisasmSubtitles);  m_strDisasmSubtitles = NULL;
        ::free(m_pDisasmSubtitleItems);  m_pDisasmSubtitleItems = NULL;
        AlertWarning(_T("Failed to parse subtitles file."));
        return;
    }

    m_okDisasmSubtitles = TRUE;
    DisasmView_UpdateWindowText();
    DisasmView_OnUpdate();  // We have to re-build the list of lines to show
}

// Разбор текста "субтитров".
// На входе -- текст в m_strDisasmSubtitles в формате UTF16 LE, заканчивается символом с кодом 0.
// На выходе -- массив описаний [адрес в памяти, тип, адрес строки комментария] в m_pDisasmSubtitleItems.
BOOL DisasmView_ParseSubtitles()
{
    ASSERT(m_strDisasmSubtitles != NULL);
    TCHAR* pText = m_strDisasmSubtitles;
    if (*pText == 0 || *pText == 0xFFFE)  // EOF or Unicode Big Endian
        return FALSE;
    if (*pText == 0xFEFF)
        pText++;  // Skip Unicode LE mark

    m_nDisasmSubtitleCount = 0;
    TCHAR* pBlockCommentStart = NULL;

    for (;;)  // Text reading loop - line by line
    {
        // Line starts
        if (*pText == 0) break;
        if (*pText == _T('\n') || *pText == _T('\r'))
        {
            pText++;
            continue;
        }

        if (*pText >= _T('0') && *pText <= _T('9'))  // Цифра -- считаем что это адрес
        {
            // Парсим адрес
            TCHAR* pAddrStart = pText;
            while (*pText != 0 && *pText >= _T('0') && *pText <= _T('9')) pText++;
            if (*pText == 0) break;
            TCHAR chSave = *pText;
            *pText++ = 0;
            WORD address;
            ParseOctalValue(pAddrStart, &address);
            *pText = chSave;

            if (pBlockCommentStart != NULL)  // На предыдущей строке был комментарий к блоку
            {
                // Сохраняем комментарий к блоку в массиве
                DisasmView_AddSubtitle(address, SUBTYPE_BLOCKCOMMENT, pBlockCommentStart);
                pBlockCommentStart = NULL;
            }

            // Пропускаем разделители
            while (*pText != 0 &&
                   (*pText == _T(' ') || *pText == _T('\t') || *pText == _T('$') || *pText == _T(':')))
                pText++;
            BOOL okDirective = (*pText == _T('.'));

            // Ищем начало комментария и конец строки
            while (*pText != 0 && *pText != _T(';') && *pText != _T('\n') && *pText != _T('\r')) pText++;
            if (*pText == 0) break;
            if (*pText == _T('\n') || *pText == _T('\r'))  // EOL, комментарий не обнаружен
            {
                pText++;

                if (okDirective)
                    DisasmView_AddSubtitle(address, SUBTYPE_DATA, NULL);
                continue;
            }

            // Нашли начало комментария -- ищем конец строки или файла
            TCHAR* pCommentStart = pText;
            while (*pText != 0 && *pText != _T('\n') && *pText != _T('\r')) pText++;

            // Сохраняем комментарий в массиве
            DisasmView_AddSubtitle(address,
                    (okDirective ? SUBTYPE_COMMENT | SUBTYPE_DATA : SUBTYPE_COMMENT),
                    pCommentStart);

            if (*pText == 0) break;
            *pText = 0;  // Обозначаем конец комментария
            pText++;
        }
        else  // Не цифра -- пропускаем до конца строки
        {
            if (*pText == _T(';'))  // Строка начинается с комментария - предположительно, комментарий к блоку
                pBlockCommentStart = pText;
            else
                pBlockCommentStart = NULL;

            while (*pText != 0 && *pText != _T('\n') && *pText != _T('\r')) pText++;
            if (*pText == 0) break;
            if (*pText == _T('\n') || *pText == _T('\r'))  // EOL
            {
                *pText = 0;  // Обозначаем конец комментария - для комментария к блоку
                pText++;
            }
        }
    }

    return TRUE;
}

DisasmSubtitleItem* DisasmView_FindSubtitle(WORD address, int typemask)
{
    DisasmSubtitleItem* pItem = m_pDisasmSubtitleItems;
    while (pItem->type != 0)
    {
        if (pItem->address == address && (pItem->type & typemask) != 0)
            return pItem;
        pItem++;
    }

    return nullptr;
}


//////////////////////////////////////////////////////////////////////


// Update after Run or Step
void DisasmView_OnUpdate()
{
    CProcessor* pProc = g_pBoard->GetCPU();
    ASSERT(pProc != nullptr);
    m_wDisasmBaseAddr = pProc->GetPC();

    ASSERT(m_pDisasmLineItems != nullptr);
    memset(m_pDisasmLineItems, 0, sizeof(DisasmLineItem) * MAX_DISASMLINECOUNT);
    m_strDisasmHint[0] = 0;
    m_strDisasmHint2[0] = 0;

    uint16_t proccurrent = pProc->GetPC();
    uint16_t current = m_wDisasmBaseAddr;
    uint16_t previous = g_wEmulatorPrevCpuPC;

    // Read from the processor memory to the buffer
    const int nWindowSize = 30;
    uint16_t memory[nWindowSize + 2];
    int addrtype[nWindowSize + 2];
    for (int idx = 0; idx < nWindowSize; idx++)
    {
        memory[idx] = g_pBoard->GetWordView(
                (WORD)(current + idx * 2 - 10), pProc->IsHaltMode(), TRUE, addrtype + idx);
    }

    uint16_t address = current - 10;
    uint16_t disasmfrom = current;
    if (previous >= address && previous < current)
        disasmfrom = previous;

    // Prepare the list of lines in m_pDisasmLineItems
    int lineindex = 0;
    int length = 0;
    for (int index = 0; index < nWindowSize; index++)  // Preparing lines
    {
        DisasmLineItem* pLineItem = m_pDisasmLineItems + lineindex;
        pLineItem->address = address;
        pLineItem->value = memory[index];
        pLineItem->addrtype = addrtype[index];

        bool okData = false;
        if (m_okDisasmSubtitles)
        {
            // Subtitles - find a comment for a block
            const DisasmSubtitleItem* pSubItem = DisasmView_FindSubtitle(address, SUBTYPE_BLOCKCOMMENT);
            if (pSubItem != nullptr && pSubItem->comment != nullptr)
            {
                pLineItem->type = LINETYPE_SUBTITLE;
                pLineItem->pSubItem = pSubItem;
                // Opening next line
                lineindex++;
                if (lineindex >= MAX_DISASMLINECOUNT)
                    break;
                pLineItem = m_pDisasmLineItems + lineindex;
                pLineItem->address = address;
                pLineItem->value = memory[index];
                pLineItem->addrtype = addrtype[index];
            }

            // Subtitles - find a comment for an instruction or data
            pSubItem = DisasmView_FindSubtitle(address, SUBTYPE_COMMENT | SUBTYPE_DATA);
            if (pSubItem != nullptr && (pSubItem->type & SUBTYPE_DATA) != 0)
            {
                okData = true;
                pLineItem->type |= LINETYPE_DATA;
            }
            if (pSubItem != nullptr && (pSubItem->type & SUBTYPE_COMMENT) != 0 && pSubItem->comment != nullptr)
            {
                pLineItem->type |= LINETYPE_SUBTITLE;
                pLineItem->pSubItem = pSubItem;
                // Строку с субтитром мы можем использовать как опорную для дизассемблера
                if (disasmfrom > address)
                    disasmfrom = address;
            }
        }

        if ((pLineItem->type & LINETYPE_DATA) == 0)
            pLineItem->type |= LINETYPE_INSTR;  // if it's not a data then an instruction

        if (address >= disasmfrom && length == 0)
        {
            if (okData)  // We have non-instruction on the address -- no need to disassemble
            {
                length = 1;
            }
            else
            {
                pLineItem->type |= LINETYPE_INSTR;
                length = DisassembleInstruction(memory + index, address, pLineItem->strInstr, pLineItem->strArg);

                if (!m_okDisasmSubtitles)  //NOTE: Subtitles can move lines down
                {
                    if (Disasm_CheckForJump(memory + index, &pLineItem->jumpdelta))
                    {
                        pLineItem->type |= LINETYPE_JUMP;
                    }

                    if (address == proccurrent)  // For current instruction, prepare the instruction hints
                    {
                        m_okDisasmJumpPredict = Disasm_GetJumpConditionHint(memory + index, pProc, g_pBoard, m_strDisasmHint);
                        if (*m_strDisasmHint == 0)  // we don't have the jump hint
                        {
                            Disasm_GetInstructionHint(memory + index, pProc, g_pBoard, m_strDisasmHint, m_strDisasmHint2);
                        }
                    }
                }
            }
        }
        if (length > 0) length--;

        address += 2;
        lineindex++;
        if (lineindex >= MAX_DISASMLINECOUNT)
            break;
    }
}


//////////////////////////////////////////////////////////////////////
// Draw functions

void DisasmView_DrawJump(HDC hdc, int yFrom, int delta, int x, int cyLine, COLORREF color)
{
    int dist = abs(delta);
    if (dist < 2) dist = 2;
    if (dist > 20) dist = 16;

    int yTo = yFrom + delta * cyLine;
    yFrom += cyLine / 2;

    HPEN hPenJump = ::CreatePen(PS_SOLID, 1, color);
    HGDIOBJ oldPen = ::SelectObject(hdc, hPenJump);

    POINT points[4];
    points[0].x = x;  points[0].y = yFrom;
    points[1].x = x + dist * 4;  points[1].y = yFrom;
    points[2].x = x + dist * 12;  points[2].y = yTo;
    points[3].x = x;  points[3].y = yTo;
    PolyBezier(hdc, points, 4);
    MoveToEx(hdc, x - 4, points[3].y, NULL);
    LineTo(hdc, x + 4, yTo - 1);
    MoveToEx(hdc, x - 4, points[3].y, NULL);
    LineTo(hdc, x + 4, yTo + 1);

    ::SelectObject(hdc, oldPen);
    VERIFY(::DeleteObject(hPenJump));
}

void DisasmView_DoDraw(HDC hdc)
{
    ASSERT(g_pBoard != nullptr);

    // Create and select font
    HFONT hFont = CreateMonospacedFont();
    HGDIOBJ hOldFont = SelectObject(hdc, hFont);
    int cxChar, cyLine;  GetFontWidthAndHeight(hdc, &cxChar, &cyLine);
    COLORREF colorOld = SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
    SetBkMode(hdc, TRANSPARENT);
    //COLORREF colorBkOld = SetBkColor(hdc, GetSysColor(COLOR_WINDOW));

    CProcessor* pDisasmPU = g_pBoard->GetCPU();

    // Draw disassembly for the current processor
    uint16_t prevPC = g_wEmulatorPrevCpuPC;
    int yFocus = DisasmView_DrawDisassemble(hdc, pDisasmPU, m_wDisasmBaseAddr, prevPC, 0, 2 + 0 * cyLine);

    SetTextColor(hdc, colorOld);
    //SetBkColor(hdc, colorBkOld);
    SelectObject(hdc, hOldFont);
    VERIFY(::DeleteObject(hFont));

    if (::GetFocus() == m_hwndDisasmViewer)
    {
        RECT rcFocus;
        GetClientRect(m_hwndDisasmViewer, &rcFocus);
        if (yFocus >= 0)
        {
            rcFocus.top = yFocus - 1;
            rcFocus.bottom = yFocus + cyLine;
        }
        DrawFocusRect(hdc, &rcFocus);
    }
}

void DisasmView_DrawBreakpoint(HDC hdc, int x, int y, int size)
{
    COLORREF colorBreakpoint = Settings_GetColor(ColorDebugBreakpoint);
    HBRUSH hBreakBrush = CreateSolidBrush(colorBreakpoint);
    HGDIOBJ hOldBrush = SelectObject(hdc, hBreakBrush);
    HGDIOBJ hOldPen = SelectObject(hdc, GetStockObject(NULL_PEN));
    Ellipse(hdc, x, y, x + size, y + size);
    ::SelectObject(hdc, hOldPen);
    ::SelectObject(hdc, hOldBrush);
    VERIFY(::DeleteObject(hBreakBrush));
}

int DisasmView_DrawDisassemble(HDC hdc, CProcessor* pProc, WORD current, WORD previous, int x, int y)
{
    int result = -1;

    int cxChar, cyLine;  GetFontWidthAndHeight(hdc, &cxChar, &cyLine);
    m_cxDisasmBreakpointZone = x + cxChar * 2;
    m_cyDisasmLine = cyLine;
    COLORREF colorText = Settings_GetColor(ColorDebugText);
    COLORREF colorPrev = Settings_GetColor(ColorDebugPrevious);
    COLORREF colorValue = Settings_GetColor(ColorDebugValue);
    COLORREF colorValueRom = Settings_GetColor(ColorDebugValueRom);
    COLORREF colorSubtitle = Settings_GetColor(ColorDebugSubtitles);
    COLORREF colorJump = Settings_GetColor(ColorDebugJump);
    ::SetTextColor(hdc, colorText);

    uint16_t proccurrent = pProc->GetPC();

    // Draw breakpoint zone
    COLORREF colorBreakptZone = Settings_GetColor(ColorDebugBreakptZone);
    HBRUSH hBrushBreakptZone = ::CreateSolidBrush(colorBreakptZone);
    HGDIOBJ hBrushOld = ::SelectObject(hdc, hBrushBreakptZone);
    ::PatBlt(hdc, 0, 0, m_cxDisasmBreakpointZone, cyLine * MAX_DISASMLINECOUNT, PATCOPY);
    ::SelectObject(hdc, hBrushOld);
    VERIFY(::DeleteObject(hBrushBreakptZone));

    // Draw current line background
    if (!m_okDisasmSubtitles)  //NOTE: Subtitles can move lines down
    {
        int yCurrent = (proccurrent - (current - 5)) * cyLine;
        COLORREF colorBackCurr = Settings_GetColor(ColorDebugBackCurrent);
        HBRUSH hBrushCurrent = ::CreateSolidBrush(colorBackCurr);
        HGDIOBJ oldBrush = ::SelectObject(hdc, hBrushCurrent);
        PatBlt(hdc, 0, yCurrent, 1000, cyLine, PATCOPY);
        ::SelectObject(hdc, oldBrush);
        ::DeleteObject(hBrushCurrent);
    }

    for (int lineindex = 0; lineindex < MAX_DISASMLINECOUNT; lineindex++)  // Draw the lines
    {
        DisasmLineItem* pLineItem = m_pDisasmLineItems + lineindex;
        if (pLineItem->type == LINETYPE_NONE)
            break;
        WORD address = pLineItem->address;

        if ((pLineItem->type & LINETYPE_SUBTITLE) != 0 && (pLineItem->type & (LINETYPE_DATA | LINETYPE_INSTR)) == 0 &&
            pLineItem->pSubItem != nullptr)  // Subtitles - comment for a block
        {
            LPCTSTR strBlockSubtitle = pLineItem->pSubItem->comment;

            ::SetTextColor(hdc, colorSubtitle);
            TextOut(hdc, x + 21 * cxChar, y, strBlockSubtitle, (int) _tcslen(strBlockSubtitle));
            ::SetTextColor(hdc, colorText);

            y += cyLine;
            continue;
        }

        if (Emulator_IsBreakpoint(address))  // Breakpoint
        {
            DisasmView_DrawBreakpoint(hdc, x + cxChar / 2, y, cyLine);
        }

        DrawOctalValue(hdc, x + 5 * cxChar, y, address);  // Address
        // Value at the address
        WORD value = pLineItem->value;
        int memorytype = pLineItem->addrtype;
        ::SetTextColor(hdc, (memorytype == ADDRTYPE_ROM) ? colorValueRom : colorValue);
        DrawOctalValue(hdc, x + 13 * cxChar, y, value);
        ::SetTextColor(hdc, colorText);

        // Current position
        if (address == current)
        {
            //TextOut(hdc, x + 2 * cxChar, y, _T(" > "), 3);
            result = y;  // Remember line for the focus rect
        }
        if (address == proccurrent)
            TextOut(hdc, x + 2 * cxChar, y, _T("PC>"), 3);
        else if (address == previous)
        {
            ::SetTextColor(hdc, colorPrev);
            TextOut(hdc, x + 2 * cxChar, y, _T(" > "), 3);
        }

        int posAfterArgs = 30;
        if ((pLineItem->type & (LINETYPE_DATA | LINETYPE_INSTR)) != 0)
        {
            LPCTSTR strInstr = pLineItem->strInstr;
            LPCTSTR strArg = pLineItem->strArg;
            ::SetTextColor(hdc, colorText);
            TextOut(hdc, x + 21 * cxChar, y, strInstr, (int)_tcslen(strInstr));
            TextOut(hdc, x + 29 * cxChar, y, strArg, (int)_tcslen(strArg));
            posAfterArgs += _tcslen(strArg);
        }

        if ((pLineItem->type & LINETYPE_SUBTITLE) != 0 && (pLineItem->type & (LINETYPE_DATA | LINETYPE_INSTR)) != 0 &&
            pLineItem->pSubItem != nullptr)  // Show subtitle comment for instruction or data
        {
            LPCTSTR strComment = pLineItem->pSubItem->comment;
            if (strComment != nullptr)
            {
                ::SetTextColor(hdc, colorSubtitle);
                TextOut(hdc, x + 52 * cxChar, y, strComment, (int)_tcslen(strComment));
                ::SetTextColor(hdc, colorText);
            }
        }

        if (!m_okDisasmSubtitles)  // We don't show jumps and hints with subtitles
        {
            bool isjump = (pLineItem->type & LINETYPE_JUMP) != 0;

            if (isjump)
            {
                int delta = pLineItem->jumpdelta;
                if (abs(delta) < 40)
                {
                    COLORREF jumpcolor = colorJump;
                    if (address == proccurrent)
                        jumpcolor = Settings_GetColor(m_okDisasmJumpPredict ? ColorDebugJumpYes : ColorDebugJumpNo);
                    DisasmView_DrawJump(hdc, y, delta, x + posAfterArgs * cxChar, cyLine, jumpcolor);
                }
            }

            if (address == proccurrent && *m_strDisasmHint != 0)  // For current instruction, draw "Instruction Hints"
            {
                COLORREF hintcolor = Settings_GetColor(isjump ? ColorDebugJumpHint : ColorDebugHint);
                ::SetTextColor(hdc, hintcolor);
                TextOut(hdc, x + 52 * cxChar, y, m_strDisasmHint, (int)_tcslen(m_strDisasmHint));
                if (*m_strDisasmHint2 != 0)
                    TextOut(hdc, x + 52 * cxChar, y + cyLine, m_strDisasmHint2, (int)_tcslen(m_strDisasmHint2));
                ::SetTextColor(hdc, colorText);
            }
        }

        y += cyLine;
    }

    return result;
}


//////////////////////////////////////////////////////////////////////
