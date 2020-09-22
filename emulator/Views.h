/*  This file is part of NEONBTL.
    NEONBTL is free software: you can redistribute it and/or modify it under the terms
of the GNU Lesser General Public License as published by the Free Software Foundation,
either version 3 of the License, or (at your option) any later version.
    NEONBTL is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU Lesser General Public License for more details.
    You should have received a copy of the GNU Lesser General Public License along with
NEONBTL. If not, see <http://www.gnu.org/licenses/>. */

// Views.h
// Defines for all views of the application

#pragma once

//////////////////////////////////////////////////////////////////////


const LPCTSTR CLASSNAME_SCREENVIEW      = _T("NEONBTLSCREEN");
const LPCTSTR CLASSNAME_KEYBOARDVIEW    = _T("NEONBTLKEYBOARD");
const LPCTSTR CLASSNAME_DEBUGVIEW       = _T("NEONBTLDEBUG");
const LPCTSTR CLASSNAME_DISASMVIEW      = _T("NEONBTLDISASM");
const LPCTSTR CLASSNAME_MEMORYVIEW      = _T("NEONBTLMEMORY");
const LPCTSTR CLASSNAME_CONSOLEVIEW     = _T("NEONBTLCONSOLE");
const LPCTSTR CLASSNAME_TAPEVIEW        = _T("NEONBTLTAPE");


//////////////////////////////////////////////////////////////////////
// ScreenView

extern HWND g_hwndScreen;  // Screen View window handle

void ScreenView_RegisterClass();
void ScreenView_Init();
void ScreenView_Done();
int ScreenView_GetScreenMode();
void ScreenView_SetScreenMode(int);
void ScreenView_PrepareScreen();
void ScreenView_ScanKeyboard();
void ScreenView_ProcessKeyboard();
void ScreenView_RedrawScreen();  // Force to call PrepareScreen and to draw the image
void ScreenView_Create(HWND hwndParent, int x, int y, int cxWidth);
LRESULT CALLBACK ScreenViewWndProc(HWND, UINT, WPARAM, LPARAM);
BOOL ScreenView_SaveScreenshot(LPCTSTR sFileName);
void ScreenView_KeyEvent(BYTE keyscan, BOOL pressed);


//////////////////////////////////////////////////////////////////////
// KeyboardView

extern HWND g_hwndKeyboard;  // Keyboard View window handle

void KeyboardView_RegisterClass();
void KeyboardView_Init();
void KeyboardView_Done();
void KeyboardView_Create(HWND hwndParent, int x, int y, int width, int height);
LRESULT CALLBACK KeyboardViewWndProc(HWND, UINT, WPARAM, LPARAM);


//////////////////////////////////////////////////////////////////////
// DebugView

extern HWND g_hwndDebug;  // Debug View window handle

void DebugView_RegisterClass();
void DebugView_Init();
void DebugView_Create(HWND hwndParent, int x, int y, int width, int height);
void DebugView_Redraw();
LRESULT CALLBACK DebugViewWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK DebugViewViewerWndProc(HWND, UINT, WPARAM, LPARAM);
void DebugView_OnUpdate();
BOOL DebugView_IsRegisterChanged(int regno);


//////////////////////////////////////////////////////////////////////
// DisasmView

extern HWND g_hwndDisasm;  // Disasm View window handle

void DisasmView_RegisterClass();
void DisasmView_Init();
void DisasmView_Done();
void DisasmView_Create(HWND hwndParent, int x, int y, int width, int height);
void DisasmView_Redraw();
LRESULT CALLBACK DisasmViewWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK DisasmViewViewerWndProc(HWND, UINT, WPARAM, LPARAM);
void DisasmView_OnUpdate();


//////////////////////////////////////////////////////////////////////
// MemoryView

extern HWND g_hwndMemory;  // Memory view window handler

void MemoryView_RegisterClass();
void MemoryView_Create(HWND hwndParent, int x, int y, int width, int height);
LRESULT CALLBACK MemoryViewWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK MemoryViewViewerWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
void MemoryView_SwitchWordByte();
void MemoryView_SelectAddress();


//////////////////////////////////////////////////////////////////////
// ConsoleView

extern HWND g_hwndConsole;  // Console View window handle

void ConsoleView_RegisterClass();
void ConsoleView_Create(HWND hwndParent, int x, int y, int width, int height);
LRESULT CALLBACK ConsoleViewWndProc(HWND, UINT, WPARAM, LPARAM);
void ConsoleView_PrintFormat(LPCTSTR pszFormat, ...);
void ConsoleView_Print(LPCTSTR message);
void ConsoleView_Activate();
void ConsoleView_StepInto();
void ConsoleView_StepOver();


//////////////////////////////////////////////////////////////////////
// TapeView

extern HWND g_hwndTape;  // Tape View window handle

void TapeView_RegisterClass();
void TapeView_Create(HWND hwndParent, int x, int y, int width, int height);
LRESULT CALLBACK TapeViewWndProc(HWND, UINT, WPARAM, LPARAM);


//////////////////////////////////////////////////////////////////////
