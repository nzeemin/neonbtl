/*  This file is part of NEONBTL.
    NEONBTL is free software: you can redistribute it and/or modify it under the terms
of the GNU Lesser General Public License as published by the Free Software Foundation,
either version 3 of the License, or (at your option) any later version.
    NEONBTL is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU Lesser General Public License for more details.
    You should have received a copy of the GNU Lesser General Public License along with
NEONBTL. If not, see <http://www.gnu.org/licenses/>. */

// Common.h

#pragma once

//////////////////////////////////////////////////////////////////////

#ifdef _DEBUG
#define NEONBTL_VERSION_STRING "DEBUG"
#elif !defined(PRODUCT)
#define NEONBTL_VERSION_STRING "RELEASE"
#else
#include "Version.h"
#endif


//////////////////////////////////////////////////////////////////////
// Assertions checking - MFC-like ASSERT macro

#ifdef _DEBUG

BOOL AssertFailedLine(LPCSTR lpszFileName, int nLine);
#define ASSERT(f)          (void) ((f) || !AssertFailedLine(__FILE__, __LINE__) || (DebugBreak(), 0))
#define VERIFY(f)          ASSERT(f)

#else   // _DEBUG

#define ASSERT(f)          ((void)0)
#define VERIFY(f)          ((void)f)

#endif // !_DEBUG


//////////////////////////////////////////////////////////////////////
// Alerts

void AlertInfo(LPCTSTR sMessage);
void AlertWarning(LPCTSTR sMessage);
void AlertWarningFormat(LPCTSTR sFormat, ...);
BOOL AlertOkCancel(LPCTSTR sMessage);


//////////////////////////////////////////////////////////////////////
// DebugPrint

void DebugPrint(LPCTSTR message);
void DebugPrintFormat(LPCTSTR pszFormat, ...);
void DebugLogClear();
void DebugLogCloseFile();
void DebugLog(LPCTSTR message);
void DebugLogFormat(LPCTSTR pszFormat, ...);


//////////////////////////////////////////////////////////////////////


// Processor register names
const TCHAR* REGISTER_NAME[];

const int NEON_SCREEN_WIDTH  = 832;
const int NEON_SCREEN_HEIGHT = 300;


HFONT CreateMonospacedFont();
HFONT CreateDialogFont();

void GetFontWidthAndHeight(HDC hdc, int* pWidth, int* pHeight);

void PrintDecValue(TCHAR* buffer, WORD value);
void PrintOctalValue(TCHAR* buffer, WORD value);
void PrintHexValue(TCHAR* buffer, WORD value);
void PrintBinaryValue(TCHAR* buffer, WORD value);

BOOL ParseOctalValue(LPCTSTR text, WORD* pValue);

void DrawDecValue(HDC hdc, int x, int y, WORD value);
void DrawOctalValue(HDC hdc, int x, int y, WORD value);
void DrawHexValue(HDC hdc, int x, int y, WORD value);
void DrawBinaryValue(HDC hdc, int x, int y, WORD value);

TCHAR TranslateDeviceCharToUnicode(BYTE ch);

LPCTSTR GetFileNameFromFilePath(LPCTSTR lpfilepath);


//////////////////////////////////////////////////////////////////////
