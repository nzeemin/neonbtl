﻿/*  This file is part of NEONBTL.
    NEONBTL is free software: you can redistribute it and/or modify it under the terms
of the GNU Lesser General Public License as published by the Free Software Foundation,
either version 3 of the License, or (at your option) any later version.
    NEONBTL is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU Lesser General Public License for more details.
    You should have received a copy of the GNU Lesser General Public License along with
NEONBTL. If not, see <http://www.gnu.org/licenses/>. */

// Dialogs.cpp

#include "stdafx.h"
#include <commdlg.h>
#include <CommCtrl.h>
#include <shellapi.h>
#include "Dialogs.h"
#include "Emulator.h"
#include "Main.h"
#include "Views.h"

//////////////////////////////////////////////////////////////////////


INT_PTR CALLBACK AboutBoxProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK CommandLineHelpBoxProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK InputBoxProc(HWND, UINT, WPARAM, LPARAM);
BOOL InputBoxValidate(HWND hDlg);
INT_PTR CALLBACK SettingsProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK SettingsColorsProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

LPCTSTR m_strInputBoxTitle = NULL;
WORD* m_pInputBoxValueOctal = NULL;

// Show the standard Choose Color dialog box
BOOL ShowColorDialog(COLORREF& color);

COLORREF m_DialogSettings_acrCustClr[16];  // array of custom colors to use in ChooseColor()
COLORREF m_DialogSettings_OsdLineColor = RGB(120, 0, 0);


//////////////////////////////////////////////////////////////////////
// About Box

void ShowAboutBox()
{
    DialogBox(g_hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), g_hwnd, AboutBoxProc);
}

INT_PTR CALLBACK AboutBoxProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        {
            TCHAR buf[64];
            wsprintf(buf, _T("%S %S"), __DATE__, __TIME__);
            ::SetWindowText(::GetDlgItem(hDlg, IDC_BUILDDATE), buf);
            return (INT_PTR)TRUE;
        }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}


//////////////////////////////////////////////////////////////////////
// Command Line Help Box

void ShowCommandLineHelpBox()
{
    DialogBox(g_hInst, MAKEINTRESOURCE(IDD_COMMANDLINEHELP), g_hwnd, CommandLineHelpBoxProc);
}


INT_PTR CALLBACK CommandLineHelpBoxProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        {
            HWND hwndText = ::GetDlgItem(hDlg, IDC_EDIT1);
            HFONT hfont = CreateMonospacedFont();
            SendMessage(hwndText, WM_SETFONT, (WPARAM)hfont, 0);
            ::SetDlgItemText(hDlg, IDC_EDIT1, g_CommandLineHelp);
            return (INT_PTR)TRUE;
        }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}


//////////////////////////////////////////////////////////////////////


BOOL InputBoxOctal(HWND hwndOwner, LPCTSTR strTitle, WORD* pValue)
{
    m_strInputBoxTitle = strTitle;
    m_pInputBoxValueOctal = pValue;
    INT_PTR result = DialogBox(g_hInst, MAKEINTRESOURCE(IDD_INPUTBOX), hwndOwner, InputBoxProc);
    if (result != IDOK)
        return FALSE;

    return TRUE;
}

INT_PTR CALLBACK InputBoxProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM /*lParam*/)
{
    switch (message)
    {
    case WM_INITDIALOG:
        {
            SetWindowText(hDlg, m_strInputBoxTitle);
            HWND hEdit = GetDlgItem(hDlg, IDC_EDIT1);

            TCHAR buffer[8];
            _sntprintf(buffer, sizeof(buffer) / sizeof(TCHAR) - 1, _T("%06ho"), *m_pInputBoxValueOctal);
            SetWindowText(hEdit, buffer);
            SendMessage(hEdit, EM_SETSEL, 0, -1);

            SetFocus(hEdit);
            return (INT_PTR)FALSE;
        }
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_EDIT1:
            {
                const size_t buffersize = 8;
                TCHAR buffer[buffersize];
                GetDlgItemText(hDlg, IDC_EDIT1, buffer, buffersize);
                if (_sntscanf_s(buffer, buffersize, _T("%ho"), m_pInputBoxValueOctal) > 0)
                {
                    GetDlgItemText(hDlg, IDC_EDIT2, buffer, buffersize);
                    WORD otherValue;
                    if (_sntscanf_s(buffer, buffersize, _T("%hx"), &otherValue) <= 0 || *m_pInputBoxValueOctal != otherValue)
                    {
                        _sntprintf(buffer, buffersize - 1, _T("%04hx"), *m_pInputBoxValueOctal);
                        SetDlgItemText(hDlg, IDC_EDIT2, buffer);
                    }
                }
            }
            return (INT_PTR)TRUE;
        case IDC_EDIT2:
            {
                const size_t buffersize = 8;
                TCHAR buffer[buffersize];
                GetDlgItemText(hDlg, IDC_EDIT2, buffer, buffersize);
                if (_sntscanf_s(buffer, buffersize, _T("%hx"), m_pInputBoxValueOctal) > 0)
                {
                    GetDlgItemText(hDlg, IDC_EDIT1, buffer, buffersize);
                    WORD otherValue;
                    if (_sntscanf_s(buffer, buffersize, _T("%ho"), &otherValue) <= 0 || *m_pInputBoxValueOctal != otherValue)
                    {
                        _sntprintf(buffer, buffersize - 1, _T("%06ho"), *m_pInputBoxValueOctal);
                        SetDlgItemText(hDlg, IDC_EDIT1, buffer);
                    }
                }
            }
            return (INT_PTR)TRUE;
        case IDOK:
            if (! InputBoxValidate(hDlg))
                return (INT_PTR)FALSE;
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        case IDCANCEL:
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        default:
            return (INT_PTR)FALSE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

BOOL InputBoxValidate(HWND hDlg)
{
    HWND hEdit = GetDlgItem(hDlg, IDC_EDIT1);
    TCHAR buffer[8];
    GetWindowText(hEdit, buffer, 8);

    WORD value;
    if (! ParseOctalValue(buffer, &value))
    {
        MessageBox(NULL, _T("Please enter correct octal value."), _T("Input Box Validation"),
                MB_OK | MB_ICONEXCLAMATION | MB_TASKMODAL);
        return FALSE;
    }

    *m_pInputBoxValueOctal = value;

    return TRUE;
}


//////////////////////////////////////////////////////////////////////


BOOL ShowSaveDialog(HWND hwndOwner, LPCTSTR strTitle, LPCTSTR strFilter, LPCTSTR strDefExt, TCHAR* bufFileName)
{
    *bufFileName = 0;
    OPENFILENAME ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwndOwner;
    ofn.hInstance = g_hInst;
    ofn.lpstrTitle = strTitle;
    ofn.lpstrFilter = strFilter;
    ofn.lpstrDefExt = strDefExt;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    ofn.lpstrFile = bufFileName;
    ofn.nMaxFile = MAX_PATH;

    BOOL okResult = GetSaveFileName(&ofn);
    return okResult;
}

BOOL ShowOpenDialog(HWND hwndOwner, LPCTSTR strTitle, LPCTSTR strFilter, TCHAR* bufFileName)
{
    *bufFileName = 0;
    OPENFILENAME ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwndOwner;
    ofn.hInstance = g_hInst;
    ofn.lpstrTitle = strTitle;
    ofn.lpstrFilter = strFilter;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrFile = bufFileName;
    ofn.nMaxFile = MAX_PATH;

    BOOL okResult = GetOpenFileName(&ofn);
    return okResult;
}


//////////////////////////////////////////////////////////////////////
// Color Dialog

BOOL ShowColorDialog(COLORREF& color, HWND hWndOwner)
{
    CHOOSECOLOR cc;  memset(&cc, 0, sizeof(cc));
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = hWndOwner;
    cc.lpCustColors = (LPDWORD)m_DialogSettings_acrCustClr;
    cc.rgbResult = color;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;

    if (!::ChooseColor(&cc))
        return FALSE;

    color = cc.rgbResult;
    return TRUE;
}


//////////////////////////////////////////////////////////////////////
// Settings Dialog

void ShowSettingsDialog()
{
    DialogBox(g_hInst, MAKEINTRESOURCE(IDD_SETTINGS), g_hwnd, SettingsProc);
}

INT_PTR CALLBACK SettingsProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM /*lParam*/)
{
    switch (message)
    {
    case WM_INITDIALOG:
        {
            HWND hVolume = GetDlgItem(hDlg, IDC_VOLUME);
            SendMessage(hVolume, TBM_SETRANGEMIN, 0, (LPARAM)0);
            SendMessage(hVolume, TBM_SETRANGEMAX, 0, (LPARAM)0xffff);
            SendMessage(hVolume, TBM_SETTICFREQ, 0x1000, 0);
            SendMessage(hVolume, TBM_SETPOS, TRUE, (LPARAM)Settings_GetSoundVolume());

            int timerCtlId = Settings_GetTimer64or50() ? IDC_TIMER50 : IDC_TIMER64;
            CheckRadioButton(hDlg, IDC_TIMER64, IDC_TIMER50, timerCtlId);

            return (INT_PTR)FALSE;
        }
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDOK:
            {
                HWND hVolume = GetDlgItem(hDlg, IDC_VOLUME);
                DWORD volume = SendMessage(hVolume, TBM_GETPOS, 0, 0);
                Settings_SetSoundVolume((WORD)volume);

                bool timer64or50 = IsDlgButtonChecked(hDlg, IDC_TIMER50) == BST_CHECKED;
                Settings_SetTimer64or50(timer64or50);
                Emulator_SetTimer64or50(timer64or50);
            }

            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        case IDCANCEL:
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        default:
            return (INT_PTR)FALSE;
        }
        break;
    }
    return (INT_PTR) FALSE;
}


//////////////////////////////////////////////////////////////////////
// Settings Colors Dialog

BOOL ShowSettingsColorsDialog()
{
    return IDOK == DialogBox(g_hInst, MAKEINTRESOURCE(IDD_SETTINGS_COLORS), g_hwnd, SettingsColorsProc);
}

int CALLBACK SettingsDialog_EnumFontProc(const LOGFONT* lpelfe, const TEXTMETRIC* /*lpntme*/, DWORD /*FontType*/, LPARAM lParam)
{
    if ((lpelfe->lfPitchAndFamily & FIXED_PITCH) == 0)
        return TRUE;
    if (lpelfe->lfFaceName[0] == _T('@'))  // Skip vertical fonts
        return TRUE;

    HWND hCombo = (HWND)lParam;

    int item = ::SendMessage(hCombo, CB_FINDSTRING, 0, (LPARAM)lpelfe->lfFaceName);
    if (item < 0)
        ::SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)lpelfe->lfFaceName);

    return TRUE;
}

void SettingsDialog_FillDebugFontCombo(HWND hCombo)
{
    LOGFONT logfont;  ZeroMemory(&logfont, sizeof logfont);
    logfont.lfCharSet = DEFAULT_CHARSET;
    logfont.lfWeight = FW_NORMAL;
    logfont.lfPitchAndFamily = FIXED_PITCH | FF_DONTCARE;

    HDC hdc = ::GetDC(NULL);
    EnumFontFamiliesEx(hdc, &logfont, (FONTENUMPROC)SettingsDialog_EnumFontProc, (LPARAM)hCombo, 0);
    VERIFY(::ReleaseDC(NULL, hdc));

    Settings_GetDebugFontName(logfont.lfFaceName);
    ::SendMessage(hCombo, CB_SELECTSTRING, 0, (LPARAM)logfont.lfFaceName);
}

void SettingsDialog_FillColorsList(HWND hList)
{
    for (int itemIndex = 0; itemIndex < ColorIndicesCount; itemIndex++)
    {
        LPCTSTR colorName = Settings_GetColorFriendlyName((ColorIndices)itemIndex);
        ::SendMessage(hList, LB_ADDSTRING, 0, (LPARAM)colorName);
        ::SendMessage(hList, LB_SETITEMDATA, itemIndex, (LPARAM)Settings_GetColor((ColorIndices)itemIndex));
    }

    ::SendMessage(hList, LB_SETCURSEL, 0, 0);
}

void SettingsDialog_InitColorDialog(HWND hDlg)
{
    HWND hDebugFont = GetDlgItem(hDlg, IDC_DEBUGFONT);
    SettingsDialog_FillDebugFontCombo(hDebugFont);

    HWND hColorList = GetDlgItem(hDlg, IDC_LIST1);
    SettingsDialog_FillColorsList(hColorList);
}

void SettingsDialog_OnColorListDrawItem(PDRAWITEMSTRUCT lpDrawItem)
{
    if (lpDrawItem->itemID == -1) return;

    HDC hdc = lpDrawItem->hDC;
    switch (lpDrawItem->itemAction)
    {
    case ODA_DRAWENTIRE:
    case ODA_SELECT:
        {
            HBRUSH hBrushBk = ::GetSysColorBrush((lpDrawItem->itemState & ODS_SELECTED) ? COLOR_HIGHLIGHT : COLOR_WINDOW);
            ::FillRect(hdc, &lpDrawItem->rcItem, hBrushBk);

            int colorIndex = lpDrawItem->itemID;
            COLORREF color = (COLORREF)(lpDrawItem->itemData);

            HBRUSH hBrush = ::CreateSolidBrush(color);
            RECT rcFill;  ::CopyRect(&rcFill, &lpDrawItem->rcItem);
            ::InflateRect(&rcFill, -1, -1);
            rcFill.left = rcFill.right - 50;
            ::FillRect(hdc, &rcFill, hBrush);

            ::SetTextColor(hdc, ::GetSysColor((lpDrawItem->itemState & ODS_SELECTED) ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
            RECT rcText;  ::CopyRect(&rcText, &lpDrawItem->rcItem);
            ::InflateRect(&rcText, -2, 0);
            LPCTSTR colorName = Settings_GetColorFriendlyName((ColorIndices)colorIndex);
            ::DrawText(hdc, colorName, _tcslen(colorName), &rcText, DT_LEFT | DT_NOPREFIX);
        }
        break;
    case ODA_FOCUS:
        break;
    }
}

void SettingsDialog_OnChooseColor(HWND hDlg)
{
    HWND hList = GetDlgItem(hDlg, IDC_LIST1);
    int itemIndex = ::SendMessage(hList, LB_GETCURSEL, 0, 0);
    COLORREF color = ::SendMessage(hList, LB_GETITEMDATA, itemIndex, 0);
    if (ShowColorDialog(color, hDlg))
    {
        ::SendMessage(hList, LB_SETITEMDATA, itemIndex, (LPARAM)color);
        ::InvalidateRect(hList, NULL, TRUE);
    }
}

void SettingsDialog_OnResetColor(HWND hDlg)
{
    HWND hList = GetDlgItem(hDlg, IDC_LIST1);
    int itemIndex = ::SendMessage(hList, LB_GETCURSEL, 0, 0);
    COLORREF color = Settings_GetDefaultColor((ColorIndices)itemIndex);

    ::SendMessage(hList, LB_SETITEMDATA, itemIndex, color);
    ::InvalidateRect(hList, NULL, TRUE);
}

void SettingsDialog_SaveFontsAndColors(HWND hDlg)
{
    TCHAR buffer[32];
    GetDlgItemText(hDlg, IDC_DEBUGFONT, buffer, 32);
    Settings_SetDebugFontName(buffer);

    HWND hList = GetDlgItem(hDlg, IDC_LIST1);
    for (int itemIndex = 0; itemIndex < ColorIndicesCount; itemIndex++)
    {
        COLORREF color = ::SendMessage(hList, LB_GETITEMDATA, itemIndex, 0);
        Settings_SetColor((ColorIndices)itemIndex, color);
    }
}

INT_PTR CALLBACK SettingsColorsProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_INITDIALOG:
        SettingsDialog_InitColorDialog(hDlg);
        return (INT_PTR)FALSE;
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_BUTTON1:
            SettingsDialog_OnChooseColor(hDlg);
            break;
        case IDC_BUTTON2:
            SettingsDialog_OnResetColor(hDlg);
            break;
        case IDOK:
            SettingsDialog_SaveFontsAndColors(hDlg);
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        case IDCANCEL:
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        default:
            return (INT_PTR)FALSE;
        }
        break;
    case WM_CTLCOLORLISTBOX:
        return (LRESULT)CreateSolidBrush(GetSysColor(COLOR_WINDOW));
    case WM_DRAWITEM:
        SettingsDialog_OnColorListDrawItem((PDRAWITEMSTRUCT)lParam);
        break;
    }
    return (INT_PTR)FALSE;
}


//////////////////////////////////////////////////////////////////////
