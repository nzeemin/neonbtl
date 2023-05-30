/*  This file is part of NEONBTL.
    NEONBTL is free software: you can redistribute it and/or modify it under the terms
of the GNU Lesser General Public License as published by the Free Software Foundation,
either version 3 of the License, or (at your option) any later version.
    NEONBTL is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU Lesser General Public License for more details.
    You should have received a copy of the GNU Lesser General Public License along with
NEONBTL. If not, see <http://www.gnu.org/licenses/>. */

#pragma once

#include "res/resource.h"

//////////////////////////////////////////////////////////////////////


#define MAX_LOADSTRING 100

extern TCHAR g_szTitle[MAX_LOADSTRING];            // The title bar text
extern TCHAR g_szWindowClass[MAX_LOADSTRING];      // Main window class name

extern HINSTANCE g_hInst; // current instance


//////////////////////////////////////////////////////////////////////
// Main Window

extern HWND g_hwnd;  // Main window handle

extern LPCTSTR g_CommandLineHelp;

void MainWindow_RegisterClass();
BOOL CreateMainWindow();
void MainWindow_RestoreSettings();
void MainWindow_UpdateMenu();
void MainWindow_UpdateWindowTitle();
void MainWindow_UpdateAllViews();
BOOL MainWindow_InitToolbar();
BOOL MainWindow_InitStatusbar();
void MainWindow_ShowHideDebug();
void MainWindow_ShowHideToolbar();
void MainWindow_ShowHideKeyboard();
void MainWindow_AdjustWindowSize();

void MainWindow_SetToolbarImage(int commandId, int imageIndex);
void MainWindow_SetStatusbarText(int part, LPCTSTR message);
void MainWindow_SetStatusbarBitmap(int part, UINT resourceId);
void MainWindow_SetStatusbarIcon(int part, HICON hIcon);

enum ToolbarButtons
{
    ToolbarButtonRun = 0,
    ToolbarButtonReset = 1,
    // Separator
    ToolbarButtonColor = 3,
};

enum ToolbarButtonImages
{
    ToolbarImageRun = 0,
    ToolbarImagePause = 1,
    ToolbarImageReset = 2,
    ToolbarImageFloppyDisk = 3,
    ToolbarImageFloppySlot = 4,
    ToolbarImageCartridge = 5,
    ToolbarImageCartSlot = 6,
    ToolbarImageSoundOn = 7,
    ToolbarImageSoundOff = 8,
    ToolbarImageFloppyDiskWP = 9,
    ToolbarImageHardSlot = 10,
    ToolbarImageHardDrive = 11,
    ToolbarImageHardDriveWP = 12,
    ToolbarImageScreenshot = 13,
    ToolbarImageDebugger = 14,
    ToolbarImageStepInto = 15,
    ToolbarImageStepOver = 16,
    ToolbarImageWordByte = 17,
    ToolbarImageGotoAddress = 18,
    ToolbarImageMemoryCpu = 19,
    ToolbarImageMemoryHalt = 20,
    ToolbarImageMemoryUser = 21,
};

enum StatusbarParts
{
    StatusbarPartMessage = 0,
    StatusbarPartFloppyEngine = 1,
    StatusbarPartFPS = 2,
    StatusbarPartUptime = 3,
};

enum ColorIndices
{
    ColorScreenBack         = 0,
    ColorDebugText          = 1,
    ColorDebugBackCurrent   = 2,
    ColorDebugValueChanged  = 3,
    ColorDebugPrevious      = 4,
    ColorDebugMemoryRom     = 5,
    ColorDebugMemoryIO      = 6,
    ColorDebugMemoryNA      = 7,
    ColorDebugValue         = 8,
    ColorDebugValueRom      = 9,
    ColorDebugSubtitles     = 10,
    ColorDebugJump          = 11,
    ColorDebugJumpYes       = 12,
    ColorDebugJumpNo        = 13,
    ColorDebugJumpHint      = 14,
    ColorDebugHint          = 15,
    ColorDebugBreakpoint    = 16,
    ColorDebugHighlight     = 17,
    ColorDebugBreakptZone   = 18,

    ColorIndicesCount       = 19,
};


//////////////////////////////////////////////////////////////////////
// Settings

void Settings_Init();
void Settings_Done();
BOOL Settings_GetWindowRect(RECT * pRect);
void Settings_SetWindowRect(const RECT * pRect);
void Settings_SetWindowMaximized(BOOL flag);
BOOL Settings_GetWindowMaximized();
void Settings_SetWindowFullscreen(BOOL flag);
BOOL Settings_GetWindowFullscreen();
void Settings_SetConfiguration(int configuration);
int  Settings_GetConfiguration();
void Settings_SetFloppyFilePath(int slot, LPCTSTR sFilePath);
void Settings_GetFloppyFilePath(int slot, LPTSTR buffer);
void Settings_SetHardFilePath(LPCTSTR sFilePath);
void Settings_GetHardFilePath(LPTSTR buffer);
void Settings_SetScreenViewMode(int mode);
int  Settings_GetScreenViewMode();
void Settings_SetScreenHeightMode(int mode);
int  Settings_GetScreenHeightMode();
void Settings_SetDebug(BOOL flag);
BOOL Settings_GetDebug();
void Settings_GetDebugFontName(LPTSTR buffer);
void Settings_SetDebugFontName(LPCTSTR sFontName);
void Settings_SetDebugMemoryMode(WORD mode);
WORD Settings_GetDebugMemoryMode();
void Settings_SetDebugMemoryAddress(WORD address);
WORD Settings_GetDebugMemoryAddress();
void Settings_SetDebugMemoryBase(WORD address);
WORD Settings_GetDebugMemoryBase();
BOOL Settings_GetDebugMemoryByte();
void Settings_SetDebugMemoryByte(BOOL flag);
void Settings_SetAutostart(BOOL flag);
BOOL Settings_GetAutostart();
void Settings_SetRealSpeed(WORD speed);
WORD Settings_GetRealSpeed();
void Settings_SetSound(BOOL flag);
BOOL Settings_GetSound();
void Settings_SetSoundVolume(WORD value);
WORD Settings_GetSoundVolume();
void Settings_SetSoundCovox(BOOL flag);
BOOL Settings_GetSoundCovox();
void Settings_SetToolbar(BOOL flag);
BOOL Settings_GetToolbar();
void Settings_SetKeyboard(BOOL flag);
BOOL Settings_GetKeyboard();
void Settings_SetMouse(BOOL flag);
BOOL Settings_GetMouse();

LPCTSTR Settings_GetColorFriendlyName(ColorIndices colorIndex);
COLORREF Settings_GetColor(ColorIndices colorIndex);
COLORREF Settings_GetDefaultColor(ColorIndices colorIndex);
void Settings_SetColor(ColorIndices colorIndex, COLORREF color);


//////////////////////////////////////////////////////////////////////
// Options

extern bool Option_ShowHelp;


//////////////////////////////////////////////////////////////////////
