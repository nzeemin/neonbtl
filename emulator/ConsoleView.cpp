/*  This file is part of NEONBTL.
    NEONBTL is free software: you can redistribute it and/or modify it under the terms
of the GNU Lesser General Public License as published by the Free Software Foundation,
either version 3 of the License, or (at your option) any later version.
    NEONBTL is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU Lesser General Public License for more details.
    You should have received a copy of the GNU Lesser General Public License along with
NEONBTL. If not, see <http://www.gnu.org/licenses/>. */

// ConsoleView.cpp

#include "stdafx.h"
#include "Main.h"
#include "Views.h"
#include "ToolWindow.h"
#include "Emulator.h"
#include "emubase\Emubase.h"

//////////////////////////////////////////////////////////////////////


COLORREF COLOR_COMMANDFOCUS = RGB(255, 242, 157);

HWND g_hwndConsole = (HWND) INVALID_HANDLE_VALUE;  // Console View window handle
WNDPROC m_wndprocConsoleToolWindow = NULL;  // Old window proc address of the ToolWindow

HWND m_hwndConsoleLog = (HWND) INVALID_HANDLE_VALUE;  // Console log window - read-only edit control
HWND m_hwndConsoleEdit = (HWND) INVALID_HANDLE_VALUE;  // Console line - edit control
HWND m_hwndConsolePrompt = (HWND) INVALID_HANDLE_VALUE;  // Console prompt - static control
HFONT m_hfontConsole = NULL;
WNDPROC m_wndprocConsoleEdit = NULL;  // Old window proc address of the console prompt
HBRUSH m_hbrConsoleFocused = NULL;

CProcessor* ConsoleView_GetCurrentProcessor();
void ConsoleView_AdjustWindowLayout();
LRESULT CALLBACK ConsoleEditWndProc(HWND, UINT, WPARAM, LPARAM);
void ConsoleView_DoConsoleCommand();

void ConsoleView_ShowHelp();
void ConsoleView_ClearConsole();
void ConsoleView_PrintConsolePrompt();
void ConsoleView_PrintRegister(LPCTSTR strName, WORD value);
void ConsoleView_PrintMemoryDump(CProcessor* pProc, WORD address, int lines);
BOOL ConsoleView_SaveMemoryDump(CProcessor* pProc);

const LPCTSTR MESSAGE_UNKNOWN_COMMAND = _T("  Unknown command.\r\n");
const LPCTSTR MESSAGE_WRONG_VALUE = _T("  Wrong value.\r\n");


//////////////////////////////////////////////////////////////////////


void ConsoleView_RegisterClass()
{
    WNDCLASSEX wcex;
    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = ConsoleViewWndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = g_hInst;
    wcex.hIcon          = NULL;
    wcex.hCursor        = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_BTNFACE + 1);
    wcex.lpszMenuName   = NULL;
    wcex.lpszClassName  = CLASSNAME_CONSOLEVIEW;
    wcex.hIconSm        = NULL;

    RegisterClassEx(&wcex);
}

// Create Console View as child of Main Window
void ConsoleView_Create(HWND hwndParent, int x, int y, int width, int height)
{
    ASSERT(hwndParent != NULL);

    g_hwndConsole = CreateWindow(
            CLASSNAME_TOOLWINDOW, NULL,
            WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
            x, y, width, height,
            hwndParent, NULL, g_hInst, NULL);
    SetWindowText(g_hwndConsole, _T("Debug Console"));

    // ToolWindow subclassing
    m_wndprocConsoleToolWindow = (WNDPROC) LongToPtr( SetWindowLongPtr(
            g_hwndConsole, GWLP_WNDPROC, PtrToLong(ConsoleViewWndProc)) );

    RECT rcConsole;  GetClientRect(g_hwndConsole, &rcConsole);

    m_hwndConsoleEdit = CreateWindowEx(
            WS_EX_CLIENTEDGE,
            _T("EDIT"), NULL,
            WS_CHILD | WS_VISIBLE,
            90, rcConsole.bottom - 20,
            rcConsole.right - 90, 20,
            g_hwndConsole, NULL, g_hInst, NULL);
    m_hwndConsoleLog = CreateWindowEx(
            WS_EX_CLIENTEDGE,
            _T("EDIT"), NULL,
            WS_CHILD | WS_VSCROLL | WS_VISIBLE | ES_READONLY | ES_MULTILINE,
            0, 0,
            rcConsole.right, rcConsole.bottom - 20,
            g_hwndConsole, NULL, g_hInst, NULL);
    m_hwndConsolePrompt = CreateWindowEx(
            0,
            _T("STATIC"), NULL,
            WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE | SS_CENTER | SS_NOPREFIX,
            0, rcConsole.bottom - 20,
            90, 20,
            g_hwndConsole, NULL, g_hInst, NULL);

    m_hfontConsole = CreateMonospacedFont();
    SendMessage(m_hwndConsolePrompt, WM_SETFONT, (WPARAM) m_hfontConsole, 0);
    SendMessage(m_hwndConsoleEdit, WM_SETFONT, (WPARAM) m_hfontConsole, 0);
    SendMessage(m_hwndConsoleLog, WM_SETFONT, (WPARAM) m_hfontConsole, 0);

    // Edit box subclassing
    m_wndprocConsoleEdit = (WNDPROC) LongToPtr( SetWindowLongPtr(
            m_hwndConsoleEdit, GWLP_WNDPROC, PtrToLong(ConsoleEditWndProc)) );

    ShowWindow(g_hwndConsole, SW_SHOW);
    UpdateWindow(g_hwndConsole);

    ConsoleView_Print(_T("Use 'h' command to show help.\r\n\r\n"));
    ConsoleView_PrintConsolePrompt();
    SetFocus(m_hwndConsoleEdit);
}

// Adjust position of client windows
void ConsoleView_AdjustWindowLayout()
{
    RECT rc;  GetClientRect(g_hwndConsole, &rc);
    int promptWidth = 65;

    if (m_hwndConsolePrompt != (HWND) INVALID_HANDLE_VALUE)
        SetWindowPos(m_hwndConsolePrompt, NULL, 0, rc.bottom - 20, promptWidth, 20, SWP_NOZORDER);
    if (m_hwndConsoleEdit != (HWND) INVALID_HANDLE_VALUE)
        SetWindowPos(m_hwndConsoleEdit, NULL, promptWidth, rc.bottom - 20, rc.right - promptWidth, 20, SWP_NOZORDER);
    if (m_hwndConsoleLog != (HWND) INVALID_HANDLE_VALUE)
        SetWindowPos(m_hwndConsoleLog, NULL, 0, 0, rc.right, rc.bottom - 24, SWP_NOZORDER);
}

LRESULT CALLBACK ConsoleViewWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    LRESULT lResult;
    switch (message)
    {
    case WM_DESTROY:
        g_hwndConsole = (HWND) INVALID_HANDLE_VALUE;  // We are closed! Bye-bye!..
        break;
    case WM_CTLCOLORSTATIC:
        if (((HWND)lParam) == m_hwndConsoleLog)
        {
            SetBkColor((HDC)wParam, ::GetSysColor(COLOR_WINDOW));
            return (LRESULT) ::GetSysColorBrush(COLOR_WINDOW);
        }
        break;
    case WM_CTLCOLOREDIT:
        if (((HWND)lParam) == m_hwndConsoleEdit && ::GetFocus() == m_hwndConsoleEdit)
        {
            if (m_hbrConsoleFocused == NULL)
                m_hbrConsoleFocused = ::CreateSolidBrush(COLOR_COMMANDFOCUS);
            SetBkColor((HDC)wParam, COLOR_COMMANDFOCUS);
            return (LRESULT)m_hbrConsoleFocused;
        }
        return CallWindowProc(m_wndprocConsoleToolWindow, hWnd, message, wParam, lParam);
    case WM_SIZE:
        lResult = CallWindowProc(m_wndprocConsoleToolWindow, hWnd, message, wParam, lParam);
        ConsoleView_AdjustWindowLayout();
        return lResult;
    }

    return CallWindowProc(m_wndprocConsoleToolWindow, hWnd, message, wParam, lParam);
}

LRESULT CALLBACK ConsoleEditWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CHAR:
        if (wParam == 13)
        {
            ConsoleView_DoConsoleCommand();
            return 0;
        }
        if (wParam == VK_ESCAPE)
        {
            TCHAR command[32];
            GetWindowText(m_hwndConsoleEdit, command, 32);
            if (*command == 0)  // If command is empty
                SetFocus(g_hwndScreen);
            else
                SendMessage(m_hwndConsoleEdit, WM_SETTEXT, 0, (LPARAM)_T(""));  // Clear command
            return 0;
        }
        break;
    }

    return CallWindowProc(m_wndprocConsoleEdit, hWnd, message, wParam, lParam);
}

void ConsoleView_Activate()
{
    if (g_hwndConsole == INVALID_HANDLE_VALUE) return;

    SetFocus(m_hwndConsoleEdit);
}

CProcessor* ConsoleView_GetCurrentProcessor()
{
    return g_pBoard->GetCPU();
}

void ConsoleView_PrintFormat(LPCTSTR pszFormat, ...)
{
    TCHAR buffer[512];

    va_list ptr;
    va_start(ptr, pszFormat);
    _vsntprintf_s(buffer, 512, 512 - 1, pszFormat, ptr);
    va_end(ptr);

    ConsoleView_Print(buffer);
}
void ConsoleView_Print(LPCTSTR message)
{
    if (m_hwndConsoleLog == INVALID_HANDLE_VALUE) return;

    // Put selection to the end of text
    SendMessage(m_hwndConsoleLog, EM_SETSEL, 0x100000, 0x100000);
    // Insert the message
    SendMessage(m_hwndConsoleLog, EM_REPLACESEL, (WPARAM) FALSE, (LPARAM) message);
    // Scroll to caret
    SendMessage(m_hwndConsoleLog, EM_SCROLLCARET, 0, 0);
}

void ConsoleView_ClearConsole()
{
    if (m_hwndConsoleLog == INVALID_HANDLE_VALUE) return;

    SendMessage(m_hwndConsoleLog, WM_SETTEXT, 0, (LPARAM) _T(""));
}

void ConsoleView_PrintConsolePrompt()
{
    CProcessor* pProc = ConsoleView_GetCurrentProcessor();
    TCHAR bufferAddr[7];
    PrintOctalValue(bufferAddr, pProc->GetPC());
    TCHAR buffer[14];
    wsprintf(buffer, _T("%s> "), bufferAddr);
    ::SetWindowText(m_hwndConsolePrompt, buffer);
}

// Print register name, octal value and binary value
void ConsoleView_PrintRegister(LPCTSTR strName, WORD value)
{
    TCHAR buffer[31];
    TCHAR* p = buffer;
    *p++ = _T(' ');
    *p++ = _T(' ');
    lstrcpy(p, strName);  p += 2;
    *p++ = _T(' ');
    PrintOctalValue(p, value);  p += 6;
    *p++ = _T(' ');
    PrintBinaryValue(p, value);  p += 16;
    *p++ = _T('\r');
    *p++ = _T('\n');
    *p++ = 0;
    ConsoleView_Print(buffer);
}

BOOL ConsoleView_SaveMemoryDump(CProcessor *pProc)
{
    BOOL okHaltMode = pProc->IsHaltMode();
    uint8_t buf[65536];
    for (uint16_t i = 0; i < 65536; i++)
    {
        buf[i] = g_pBoard->GetByte(i, okHaltMode);
    }

    const TCHAR fname[] = _T("memdump.bin");
    HANDLE file = ::CreateFile(fname,
            GENERIC_WRITE, FILE_SHARE_READ, NULL,
            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    DWORD dwLength = 65536;
    DWORD dwBytesWritten = 0;
    ::WriteFile(file, buf, dwLength, &dwBytesWritten, NULL);
    ::CloseHandle(file);
    if (dwBytesWritten != dwLength)
        return false;

    return true;
}

// Print memory dump
void ConsoleView_PrintMemoryDump(CProcessor* pProc, WORD address, int lines)
{
    address &= ~1;  // Line up to even address

    BOOL okHaltMode = pProc->IsHaltMode();

    for (int line = 0; line < lines; line++)
    {
        WORD dump[8];
        for (uint16_t i = 0; i < 8; i++)
            dump[i] = g_pBoard->GetWord(address + i * 2, okHaltMode);

        TCHAR buffer[2 + 6 + 2 + 7 * 8 + 1 + 16 + 1 + 2];
        TCHAR* pBuf = buffer;
        *pBuf = _T(' ');  pBuf++;
        *pBuf = _T(' ');  pBuf++;
        PrintOctalValue(pBuf, address);  pBuf += 6;
        *pBuf = _T(' ');  pBuf++;
        *pBuf = _T(' ');  pBuf++;
        for (int i = 0; i < 8; i++)
        {
            PrintOctalValue(pBuf, dump[i]);  pBuf += 6;
            *pBuf = _T(' ');  pBuf++;
        }
        *pBuf = _T(' ');  pBuf++;
        for (int i = 0; i < 8; i++)
        {
            WORD word = dump[i];
            BYTE ch1 = LOBYTE(word);
            TCHAR wch1 = Translate_BK_Unicode(ch1);
            if (ch1 < 32) wch1 = _T('�');
            *pBuf = wch1;  pBuf++;
            BYTE ch2 = HIBYTE(word);
            TCHAR wch2 = Translate_BK_Unicode(ch2);
            if (ch2 < 32) wch2 = _T('�');
            *pBuf = wch2;  pBuf++;
        }
        *pBuf++ = _T('\r');
        *pBuf++ = _T('\n');
        *pBuf = 0;

        ConsoleView_Print(buffer);

        address += 16;
    }
}
// Print disassembled instructions
// Return value: number of words in the last instruction
int ConsoleView_PrintDisassemble(CProcessor* pProc, uint16_t address, BOOL okOneInstr, BOOL okShort)
{
    bool okHaltMode = pProc->IsHaltMode();

    const int nWindowSize = 30;
    uint16_t memory[nWindowSize + 2];
    int addrtype;
    for (uint16_t i = 0; i < nWindowSize + 2; i++)
        memory[i] = g_pBoard->GetWordView(address + i * 2, okHaltMode, TRUE, &addrtype);

    TCHAR bufaddr[7];
    TCHAR bufvalue[7];
    TCHAR buffer[64];

    int lastLength = 0;
    int length = 0;
    for (int index = 0; index < nWindowSize; index++)  // ������ ������
    {
        PrintOctalValue(bufaddr, address);
        uint16_t value = memory[index];
        PrintOctalValue(bufvalue, value);

        if (length > 0)
        {
            if (!okShort)
            {
                wsprintf(buffer, _T("  %s  %s\r\n"), bufaddr, bufvalue);
                ConsoleView_Print(buffer);
            }
        }
        else
        {
            if (okOneInstr && index > 0)
                break;
            TCHAR instr[8];
            TCHAR args[32];
            length = DisassembleInstruction(memory + index, address, instr, args);
            lastLength = length;
            if (index + length > nWindowSize)
                break;
            if (okShort)
                wsprintf(buffer, _T("  %s  %-7s %s\r\n"), bufaddr, instr, args);
            else
                wsprintf(buffer, _T("  %s  %s  %-7s %s\r\n"), bufaddr, bufvalue, instr, args);
            ConsoleView_Print(buffer);
        }
        length--;
        address += 2;
    }

    return lastLength;
}

void ConsoleView_StepInto()
{
    // Put command to console prompt
    SendMessage(m_hwndConsoleEdit, WM_SETTEXT, 0, (LPARAM) _T("s"));
    // Execute command
    ConsoleView_DoConsoleCommand();
}
void ConsoleView_StepOver()
{
    // Put command to console prompt
    SendMessage(m_hwndConsoleEdit, WM_SETTEXT, 0, (LPARAM) _T("so"));
    // Execute command
    ConsoleView_DoConsoleCommand();
}

void ConsoleView_ShowHelp()
{
    ConsoleView_Print(_T("Console command list:\r\n")
            _T("  c          Clear console log\r\n")
            _T("  d          Disassemble from PC; use D for short format\r\n")
            _T("  dXXXXXX    Disassemble from address XXXXXX\r\n")
            _T("  g          Go; free run\r\n")
            _T("  gXXXXXX    Go; run and stop at address XXXXXX\r\n")
            _T("  m          Memory dump at current address\r\n")
            _T("  mXXXXXX    Memory dump at address XXXXXX\r\n")
            _T("  mrN        Memory dump at address from register N; N=0..7\r\n")
            _T("  r          Show register values\r\n")
            _T("  rN         Show value of register N; N=0..7,ps\r\n")
            _T("  rN XXXXXX  Set register N to value XXXXXX; N=0..7,ps\r\n")
            _T("  s          Step Into; executes one instruction\r\n")
            _T("  so         Step Over; executes and stops after the current instruction\r\n")
            _T("  b          List all breakpoints\r\n")
            _T("  bXXXXXX    Set breakpoint at address XXXXXX\r\n")
            _T("  bcXXXXXX   Remove breakpoint at address XXXXXX\r\n")
            _T("  bc         Remove all breakpoints\r\n")
            _T("  u          Save memory dump to file memdump.bin\r\n")
#if !defined(PRODUCT)
            _T("  t          Tracing on/off to trace.log file\r\n")
#endif
                     );
}

void ConsoleView_ShowBreakpoints()
{
    const uint16_t* pbps = Emulator_GetCPUBreakpointList();
    if (pbps == nullptr || *pbps == 0177777)
    {
        ConsoleView_Print(_T("  No breakpoints.\r\n"));
    }
    else
    {
        while (*pbps != 0177777)
        {
            ConsoleView_PrintFormat(_T("  %06ho\r\n"), *pbps);
            pbps++;
        }
    }
}
void ConsoleView_RemoveAllBreakpoints()
{
    Emulator_RemoveAllBreakpoints();
    DebugView_Redraw();
    DisasmView_Redraw();
}
void ConsoleView_AddBreakpoint(WORD address)
{
    bool result = Emulator_AddCPUBreakpoint(address);
    if (!result)
        ConsoleView_Print(_T("  Failed to add breakpoint.\r\n"));
    DebugView_Redraw();
    DisasmView_Redraw();
}
void ConsoleView_RemoveBreakpoint(WORD address)
{
    bool result = Emulator_RemoveCPUBreakpoint(address);
    if (!result)
        ConsoleView_Print(_T("  Failed to remove breakpoint.\r\n"));
    DebugView_Redraw();
    DisasmView_Redraw();
}

void ConsoleView_DoConsoleCommand()
{
    // Get command text
    TCHAR command[32];
    GetWindowText(m_hwndConsoleEdit, command, 32);
    SendMessage(m_hwndConsoleEdit, WM_SETTEXT, 0, (LPARAM) _T(""));  // Clear command

    if (command[0] == 0) return;  // Nothing to do

    // Echo command to the log
    TCHAR buffer[36];
    ::GetWindowText(m_hwndConsolePrompt, buffer, 14);
    ConsoleView_Print(buffer);
    wsprintf(buffer, _T(" %s\r\n"), command);
    ConsoleView_Print(buffer);

    BOOL okUpdateAllViews = FALSE;  // Flag - need to update all debug views
    CProcessor* pProc = ConsoleView_GetCurrentProcessor();

    // Execute the command
    switch (command[0])
    {
    case _T('h'):
        ConsoleView_ShowHelp();
        break;
    case _T('c'):  // Clear log
        ConsoleView_ClearConsole();
        break;
    case _T('r'):  // Register operations
        if (command[1] == 0)  // Print all registers
        {
            for (int r = 0; r < 8; r++)
            {
                LPCTSTR name = REGISTER_NAME[r];
                WORD value = pProc->GetReg(r);
                ConsoleView_PrintRegister(name, value);
            }
            ConsoleView_PrintRegister(_T("PS"), pProc->GetPSW());
        }
        else if (command[1] >= _T('0') && command[1] <= _T('7'))  // "r0".."r7"
        {
            int r = command[1] - _T('0');
            LPCTSTR name = REGISTER_NAME[r];
            if (command[2] == 0)  // "rN" - show register N
            {
                WORD value = pProc->GetReg(r);
                ConsoleView_PrintRegister(name, value);
            }
            else if (command[2] == _T('=') || command[2] == _T(' '))  // "rN=XXXXXX" - set register N to value XXXXXX
            {
                WORD value;
                if (! ParseOctalValue(command + 3, &value))
                    ConsoleView_Print(MESSAGE_WRONG_VALUE);
                else
                {
                    pProc->SetReg(r, value);
                    ConsoleView_PrintRegister(name, value);
                    okUpdateAllViews = TRUE;
                }
            }
            else
                ConsoleView_Print(MESSAGE_UNKNOWN_COMMAND);
        }
        else if (command[1] == _T('p') && command[2] == _T('s'))  // "rps"
        {
            if (command[3] == 0)  // "rps" - show PSW
            {
                WORD value = pProc->GetPSW();
                ConsoleView_PrintRegister(_T("PS"), value);
            }
            else if (command[3] == _T('=') || command[3] == _T(' '))  // "rps=XXXXXX" - set PSW to value XXXXXX
            {
                WORD value;
                if (! ParseOctalValue(command + 4, &value))
                    ConsoleView_Print(MESSAGE_WRONG_VALUE);
                else
                {
                    pProc->SetPSW(value);
                    ConsoleView_PrintRegister(_T("PS"), value);
                    okUpdateAllViews = TRUE;
                }
            }
            else
                ConsoleView_Print(MESSAGE_UNKNOWN_COMMAND);
        }
        else
            ConsoleView_Print(MESSAGE_UNKNOWN_COMMAND);
        break;
    case _T('s'):  // Step
        if (command[1] == 0)  // "s" - Step Into, execute one instruction
        {
            ConsoleView_PrintDisassemble(pProc, pProc->GetPC(), TRUE, FALSE);

            //pProc->Execute();
            g_pBoard->DebugTicks();

            okUpdateAllViews = TRUE;
        }
        else if (command[1] == _T('o'))  // "so" - Step Over
        {
            int instrLength = ConsoleView_PrintDisassemble(pProc, pProc->GetPC(), TRUE, FALSE);
            WORD bpaddress = (WORD)(pProc->GetPC() + instrLength * 2);

            Emulator_SetTempCPUBreakpoint(bpaddress);
            Emulator_Start();
        }
        break;
    case _T('d'):  // Disassemble
    case _T('D'):  // Disassemble, short format
        {
            BOOL okShort = (command[0] == _T('D'));
            if (command[1] == 0)  // "d" - disassemble at current address
                ConsoleView_PrintDisassemble(pProc, pProc->GetPC(), FALSE, okShort);
            else if (command[1] >= _T('0') && command[1] <= _T('7'))  // "dXXXXXX" - disassemble at address XXXXXX
            {
                WORD value;
                if (! ParseOctalValue(command + 1, &value))
                    ConsoleView_Print(MESSAGE_WRONG_VALUE);
                else
                {
                    ConsoleView_PrintDisassemble(pProc, value, FALSE, okShort);
                }
            }
            else
                ConsoleView_Print(MESSAGE_UNKNOWN_COMMAND);
        }
        break;
    case _T('u'):
        ConsoleView_SaveMemoryDump(pProc);
        break;
    case _T('m'):
        if (command[1] == 0)  // "m" - dump memory at current address
        {
            ConsoleView_PrintMemoryDump(pProc, pProc->GetPC(), 8);
        }
        else if (command[1] >= _T('0') && command[1] <= _T('7'))  // "mXXXXXX" - dump memory at address XXXXXX
        {
            WORD value;
            if (! ParseOctalValue(command + 1, &value))
                ConsoleView_Print(MESSAGE_WRONG_VALUE);
            else
            {
                ConsoleView_PrintMemoryDump(pProc, value, 8);
            }
        }
        else if (command[1] == _T('r') &&
                command[2] >= _T('0') && command[2] <= _T('7'))  // "mrN" - dump memory at address from register N
        {
            int r = command[2] - _T('0');
            WORD address = pProc->GetReg(r);
            ConsoleView_PrintMemoryDump(pProc, address, 8);
        }
        else
            ConsoleView_Print(MESSAGE_UNKNOWN_COMMAND);
        break;
        //TODO: "mXXXXXX YYYYYY" - set memory cell at XXXXXX to value YYYYYY
        //TODO: "mrN YYYYYY" - set memory cell at address from rN to value YYYYYY
    case _T('g'):
        if (command[1] == 0)
        {
            Emulator_Start();
        }
        else
        {
            WORD value;
            if (! ParseOctalValue(command + 1, &value))
                ConsoleView_Print(MESSAGE_WRONG_VALUE);
            else
            {
                Emulator_SetTempCPUBreakpoint(value);
                Emulator_Start();
            }
        }
        break;
    case _T('b'):
        if (command[1] == 0)  // b - list breakpoints
        {
            ConsoleView_ShowBreakpoints();
        }
        else if (command[1] == _T('c'))
        {
            if (command[2] == 0)  // bc - remove all breakpoints
            {
                ConsoleView_RemoveAllBreakpoints();
            }
            else  // bcXXXXXX - remove breakpoint XXXXXX
            {
                WORD value;
                if (ParseOctalValue(command + 2, &value))
                    ConsoleView_RemoveBreakpoint(value);
                else
                    ConsoleView_Print(MESSAGE_WRONG_VALUE);
            }
        }
        else if (command[1] >= _T('0') && command[1] <= _T('7'))  // "bXXXXXX" - add breakpoint XXXXXX
        {
            WORD value;
            if (ParseOctalValue(command + 1, &value))
                ConsoleView_AddBreakpoint(value);
            else
                ConsoleView_Print(MESSAGE_WRONG_VALUE);
        }
        else
            ConsoleView_Print(MESSAGE_UNKNOWN_COMMAND);
        break;
#if !defined(PRODUCT)
    case _T('t'):
        {
            BOOL okTrace = !g_pBoard->GetTrace();
            g_pBoard->SetTrace(okTrace);
            if (okTrace)
                ConsoleView_Print(_T("  Trace is ON.\r\n"));
            else
                ConsoleView_Print(_T("  Trace is OFF.\r\n"));
        }
        break;
#endif
    default:
        ConsoleView_Print(MESSAGE_UNKNOWN_COMMAND);
        break;
    }

    ConsoleView_PrintConsolePrompt();

    if (okUpdateAllViews)
    {
        MainWindow_UpdateAllViews();
    }
}


//////////////////////////////////////////////////////////////////////
