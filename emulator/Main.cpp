/*  This file is part of NEONBTL.
    NEONBTL is free software: you can redistribute it and/or modify it under the terms
of the GNU Lesser General Public License as published by the Free Software Foundation,
either version 3 of the License, or (at your option) any later version.
    NEONBTL is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU Lesser General Public License for more details.
    You should have received a copy of the GNU Lesser General Public License along with
NEONBTL. If not, see <http://www.gnu.org/licenses/>. */

// Main.cpp : Defines the entry point for the application.

#include "stdafx.h"
#include <commdlg.h>
#include <crtdbg.h>
#include <mmintrin.h>
#include <Vfw.h>
#include <CommCtrl.h>
#include <shellapi.h>

#include "Main.h"
#include "Emulator.h"
#include "Dialogs.h"
#include "Views.h"
#include "util/BitmapFile.h"

//////////////////////////////////////////////////////////////////////
// Global Variables

HINSTANCE g_hInst = NULL; // current instance
HWND g_hwnd = NULL;
long m_nMainLastFrameTicks = 0;


//////////////////////////////////////////////////////////////////////
// Forward declarations

BOOL InitInstance(HINSTANCE, int);
void DoneInstance();
void ParseCommandLine();

LPCTSTR g_CommandLineHelp =
    _T("Usage: NEONBTL [options]\r\n\r\n")
    _T("Command line options:\r\n\r\n")
    _T("/h /help\r\n\tShow command line options (this box)\r\n")
    _T("/autostart /autostarton\r\n\tStart emulation on window open\r\n")
    _T("/noautostart /autostartoff\r\n\tDo not start emulation on window open\r\n")
    _T("/debug /debugon /debugger\r\n\tSwitch to debug mode\r\n")
    _T("/nodebug /debugoff\r\n\tSwitch off the debug mode\r\n")
    _T("/sound /soundon\r\n\tTurn sound on\r\n")
    _T("/nosound /soundoff\r\n\tTurn sound off\r\n")
    _T("/diskN:filePath\r\n\tAttach disk image, N=0..1\r\n")
    _T("/hard:filePath\r\n\tAttach hard disk image\r\n");


//////////////////////////////////////////////////////////////////////


int APIENTRY _tWinMain(
    HINSTANCE hInstance,
    HINSTANCE hPrevInstance,
    LPTSTR    lpCmdLine,
    int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

#ifdef _DEBUG
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF);
    int n = 0;
    _CrtSetBreakAlloc(n);
#endif

    g_hInst = hInstance; // Store instance handle in our global variable

    LARGE_INTEGER nFrameStartTime;
    nFrameStartTime.QuadPart = 0;

    // Initialize global strings
    LoadString(g_hInst, IDS_APP_TITLE, g_szTitle, MAX_LOADSTRING);
    LoadString(g_hInst, IDC_APPLICATION, g_szWindowClass, MAX_LOADSTRING);
    MainWindow_RegisterClass();

    // Perform application initialization
    if (! InitInstance(hInstance, nCmdShow))
        return FALSE;

    HACCEL hAccelTable = ::LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_APPLICATION));

    if (Option_ShowHelp)
        ::PostMessage(g_hwnd, WM_COMMAND, ID_HELP_COMMAND_LINE_HELP, NULL);

    LARGE_INTEGER nPerformanceFrequency;
    ::QueryPerformanceFrequency(&nPerformanceFrequency);

    // Main message loop
    MSG msg;
    for (;;)
    {
        ::QueryPerformanceCounter(&nFrameStartTime);

        if (!g_okEmulatorRunning)
            ::Sleep(1);
        else
        {
            if (!Emulator_SystemFrame())  // Breakpoint hit
            {
                Emulator_Stop();
                // Turn on debugger if not yet
                if (!Settings_GetDebug())
                    ::PostMessage(g_hwnd, WM_COMMAND, ID_VIEW_DEBUG, 0);
                else
                    ::FlashWindow(g_hwnd, TRUE);
            }

            ScreenView_RedrawScreen();
        }

        // Process all queue
        while (::PeekMessage( &msg, NULL, 0, 0, PM_REMOVE ))
        {
            if (msg.message == WM_QUIT)
                goto endprog;

            if (::TranslateAccelerator(g_hwnd, hAccelTable, &msg))
                continue;

            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
        }

        if (g_okEmulatorRunning /*&& Settings_GetRealSpeed()*/)
        {
            //if (true/*Settings_GetRealSpeed() == 0*/)
            //    ::Sleep(1);  // We should not consume 100% of CPU
            //else
            {
                // Slow down to 25 frames per second
                LARGE_INTEGER nFrameFinishTime;  // Frame start time
                ::QueryPerformanceCounter(&nFrameFinishTime);
                LONGLONG nTimeElapsed = (nFrameFinishTime.QuadPart - nFrameStartTime.QuadPart)
                        * 1000 / nPerformanceFrequency.QuadPart;
                if (nTimeElapsed > 0 && nTimeElapsed < 20)  // 1000 millisec / 25 = 40 millisec
                {
                    LONG nTimeToSleep = (LONG)(20 - nTimeElapsed);
                    ::Sleep((DWORD) nTimeToSleep / 2);
                    ScreenView_ScanKeyboard();
                    ::Sleep((DWORD) nTimeToSleep / 2);
                }
            }
        }

        //// Time bomb for perfomance analysis
        //if (Emulator_GetUptime() >= 300)  // 5 minutes
        //    ::PostQuitMessage(0);
    }
endprog:

    DoneInstance();

#ifdef _DEBUG
    if (_CrtDumpMemoryLeaks())
        ::MessageBeep(MB_ICONEXCLAMATION);
#endif

    return (int) msg.wParam;
}


//
//   FUNCTION: InitInstance(HINSTANCE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
//   COMMENTS:
//
//        In this function, we save the instance handle in a global variable and
//        create and display the main program window.
//
BOOL InitInstance(HINSTANCE /*hInstance*/, int /*nCmdShow*/)
{
    INITCOMMONCONTROLSEX ics;  ics.dwSize = sizeof(ics);
    ics.dwICC = ICC_WIN95_CLASSES | ICC_TAB_CLASSES;
    InitCommonControlsEx(&ics);

    BitmapFile_Init();

#if !defined(PRODUCT)
    DebugLogClear();
#endif
    Settings_Init();

    ParseCommandLine();  // Override settings by command-line option if needed

    if (!Emulator_Init())
        return FALSE;

    int conf = Settings_GetConfiguration();
    //if (conf == 0) //TODO
    if (!Emulator_InitConfiguration((NeonConfiguration)conf))
        return FALSE;

    Emulator_SetSound(Settings_GetSound() != 0);
    Emulator_SetCovox(Settings_GetSoundCovox() != 0);

    if (!CreateMainWindow())
        return FALSE;

    return TRUE;
}

// Instance finalization
void DoneInstance()
{
    ScreenView_Done();
    DisasmView_Done();

    Emulator_Done();

    BitmapFile_Done();

    Settings_Done();
}

void ParseCommandLine()
{
    LPTSTR commandline = ::GetCommandLine();

    int argnum = 0;
    LPTSTR* args = CommandLineToArgvW(commandline, &argnum);

    for (int curargn = 1; curargn < argnum; curargn++)
    {
        LPTSTR arg = args[curargn];

        if (_tcscmp(arg, _T("/help")) == 0 || _tcscmp(arg, _T("/h")) == 0)
        {
            Option_ShowHelp = true;
        }
        else if (_tcscmp(arg, _T("/autostart")) == 0 || _tcscmp(arg, _T("/autostarton")) == 0)
        {
            Settings_SetAutostart(TRUE);
        }
        else if (_tcscmp(arg, _T("/autostartoff")) == 0 || _tcscmp(arg, _T("/noautostart")) == 0)
        {
            Settings_SetAutostart(FALSE);
        }
        else if (_tcscmp(arg, _T("/debug")) == 0 || _tcscmp(arg, _T("/debugon")) == 0 || _tcscmp(arg, _T("/debugger")) == 0)
        {
            Settings_SetDebug(TRUE);
        }
        else if (_tcscmp(arg, _T("/debugoff")) == 0 || _tcscmp(arg, _T("/nodebug")) == 0)
        {
            Settings_SetDebug(FALSE);
        }
        else if (_tcscmp(arg, _T("/sound")) == 0 || _tcscmp(arg, _T("/soundon")) == 0)
        {
            Settings_SetSound(TRUE);
        }
        else if (_tcscmp(arg, _T("/soundoff")) == 0 || _tcscmp(arg, _T("/nosound")) == 0)
        {
            Settings_SetSound(FALSE);
        }
        else if (_tcslen(arg) > 7 && _tcsncmp(arg, _T("/disk"), 5) == 0)  // "/diskN:filePath", N=0..1
        {
            if (arg[5] >= _T('0') && arg[5] <= _T('1') && arg[6] == ':')
            {
                int slot = arg[5] - _T('0');
                LPCTSTR filePath = arg + 7;
                Settings_SetFloppyFilePath(slot, filePath);
            }
        }
        else if (_tcslen(arg) > 6 && _tcsncmp(arg, _T("/hard:"), 6) == 0)  // "/hard:filePath"
        {
            LPCTSTR filePath = arg + 6;
            Settings_SetHardFilePath(filePath);
        }
        //TODO: "/state:filepath" or "filepath.neonst"
    }

    ::LocalFree(args);
}


//////////////////////////////////////////////////////////////////////
