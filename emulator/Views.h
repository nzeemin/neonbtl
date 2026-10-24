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
// Window class names

#define CLASSNAMEPREFIX _T("NEONBTL")

const LPCTSTR CLASSNAME_SCREENVIEW      = CLASSNAMEPREFIX _T("SCREEN");
const LPCTSTR CLASSNAME_KEYBOARDVIEW    = CLASSNAMEPREFIX _T("KEYBOARD");
const LPCTSTR CLASSNAME_DEBUGPROCVIEW   = CLASSNAMEPREFIX _T("DEBUGPROC");
const LPCTSTR CLASSNAME_DEBUGSTACKVIEW  = CLASSNAMEPREFIX _T("DEBUGSTACK");
const LPCTSTR CLASSNAME_DEBUGPORTSVIEW  = CLASSNAMEPREFIX _T("DEBUGPOTRS");
const LPCTSTR CLASSNAME_DEBUGBREAKSVIEW = CLASSNAMEPREFIX _T("DEBUGBREAKS");
const LPCTSTR CLASSNAME_DEBUGMEMORYVIEW = CLASSNAMEPREFIX _T("DEBUGMEMORY");
const LPCTSTR CLASSNAME_DISASMVIEW      = CLASSNAMEPREFIX _T("DISASM");
const LPCTSTR CLASSNAME_INSTRHISTORYVIEW = CLASSNAMEPREFIX _T("INSTRHISTORY");
const LPCTSTR CLASSNAME_MEMORYVIEW      = CLASSNAMEPREFIX _T("MEMORY");
const LPCTSTR CLASSNAME_MEMORYMAPVIEW   = CLASSNAMEPREFIX _T("MEMORYMAP");
const LPCTSTR CLASSNAME_SPRITEVIEW      = CLASSNAMEPREFIX _T("SPRITE");
const LPCTSTR CLASSNAME_CONSOLEVIEW     = CLASSNAMEPREFIX _T("CONSOLE");
const LPCTSTR CLASSNAME_DISPLAYLISTVIEW = CLASSNAMEPREFIX _T("DISPLAYLIST");
const LPCTSTR CLASSNAME_PROCESSLISTVIEW = CLASSNAMEPREFIX _T("PROCESSLIST");
const LPCTSTR CLASSNAME_PALETTEVIEW     = CLASSNAMEPREFIX _T("PALETTE");


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
void ScreenView_UpdateMouse();
void ScreenView_RedrawScreen();  // Force to call PrepareScreen and to draw the image
void ScreenView_Create(HWND hwndParent, int x, int y);
LRESULT CALLBACK ScreenViewWndProc(HWND, UINT, WPARAM, LPARAM);
BOOL ScreenView_SaveScreenshot(LPCTSTR sFileName);
HGLOBAL ScreenView_GetScreenshotAsDIB();


//////////////////////////////////////////////////////////////////////
// KeyboardView

extern HWND g_hwndKeyboard;  // Keyboard View window handle

void KeyboardView_RegisterClass();
void KeyboardView_Init();
void KeyboardView_Done();
void KeyboardView_Create(HWND hwndParent, int x, int y, int width, int height);
LRESULT CALLBACK KeyboardViewWndProc(HWND, UINT, WPARAM, LPARAM);
void KeyboardView_KeyEvent(WORD keyscan, BOOL pressed);
WORD KeyboardView_GetKeyPressed();


//////////////////////////////////////////////////////////////////////
// DebugView

extern HWND g_hwndDebug;  // Debug View window handle

void DebugView_RegisterClasses();
void DebugView_Init();
void DebugView_Create(HWND hwndParent, int x, int y, int width, int height);
void DebugView_Redraw();
void DebugView_OnUpdate();
LRESULT CALLBACK DebugViewWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK DebugProcViewViewerWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK DebugStackViewViewerWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK DebugPortsViewViewerWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK DebugBreaksViewViewerWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK DebugMemoryViewViewerWndProc(HWND, UINT, WPARAM, LPARAM);


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
int  DisasmView_GetWidth();  // Width of the view when Instr History is shown to the right, in pixels
void DisasmView_LoadUnloadSubtitles();


//////////////////////////////////////////////////////////////////////
// InstrHistoryView

extern HWND g_hwndInstrHistory;  // Instr History View window handle

void InstrHistoryView_RegisterClass();
void InstrHistoryView_Create(HWND hwndParent, int x, int y, int width, int height);
void InstrHistoryView_Redraw();
LRESULT CALLBACK InstrHistoryViewWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK InstrHistoryViewViewerWndProc(HWND, UINT, WPARAM, LPARAM);
void InstrHistoryView_OnUpdate();  // Take the snapshot of the history ring
int  InstrHistoryView_GetWidth();  // Fixed width of the view, in pixels


//////////////////////////////////////////////////////////////////////
// MemoryView

extern HWND g_hwndMemory;  // Memory view window handler

enum MemoryViewMode
{
    MEMMODE_CPU = 0,   // CPU memory
    MEMMODE_HALT = 1,  // HALT mode memory
    MEMMODE_USER = 2,  // USER mode memory
    MEMMODE_PS   = 3,  // Process List
    MEMMODE_PAL  = 4,  // Palettes
    MEMMODE_LAST = 4,  // Last mode
};

enum MemoryViewNumeralMode
{
    MEMMODENUM_OCT = 0,
    MEMMODENUM_HEX = 1,
};

void MemoryView_RegisterClass();
void MemoryView_Create(HWND hwndParent, int x, int y, int width, int height);
LRESULT CALLBACK MemoryViewWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK MemoryViewViewerWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
void MemoryView_SetViewMode(MemoryViewMode);
void MemoryView_SwitchWordByte();
void MemoryView_SwitchNumeralMode();
void MemoryView_SelectAddress();


//////////////////////////////////////////////////////////////////////
// ConsoleView

extern HWND g_hwndConsole;  // Console View window handle

void ConsoleView_RegisterClass();
void ConsoleView_Create(HWND hwndParent, int x, int y, int width, int height);
LRESULT CALLBACK ConsoleViewWndProc(HWND, UINT, WPARAM, LPARAM);
void ConsoleView_OnUpdate();
void ConsoleView_PrintFormat(LPCTSTR pszFormat, ...);
void ConsoleView_Print(LPCTSTR message);
void ConsoleView_Activate();
void ConsoleView_StepInto();
void ConsoleView_StepOver();
void ConsoleView_ClearConsole();
void ConsoleView_DeleteAllBreakpoints();


//////////////////////////////////////////////////////////////////////
// DisplayListView

extern HWND g_hwndDisplayList;  // DisplayList view window handler

void DisplayListView_RegisterClass();
void DisplayListView_Create(int x, int y);
LRESULT CALLBACK DisplayListViewWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK DisplayListViewViewerWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);


//////////////////////////////////////////////////////////////////////
// ProcessListView

LRESULT CALLBACK ProcessListViewViewerWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

//////////////////////////////////////////////////////////////////////
// PaletteView

LRESULT CALLBACK PaletteViewViewerWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

//////////////////////////////////////////////////////////////////////
