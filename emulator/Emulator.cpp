/*  This file is part of NEONBTL.
    NEONBTL is free software: you can redistribute it and/or modify it under the terms
of the GNU Lesser General Public License as published by the Free Software Foundation,
either version 3 of the License, or (at your option) any later version.
    NEONBTL is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU Lesser General Public License for more details.
    You should have received a copy of the GNU Lesser General Public License along with
NEONBTL. If not, see <http://www.gnu.org/licenses/>. */

// Emulator.cpp

#include "stdafx.h"
#include <stdio.h>
#include <Share.h>
#include "Main.h"
#include "Emulator.h"
#include "Views.h"
#include "Dialogs.h"
#include "emubase\Emubase.h"


//////////////////////////////////////////////////////////////////////


CMotherboard* g_pBoard = NULL;
NeonConfiguration g_nEmulatorConfiguration;  // Current configuration
BOOL g_okEmulatorRunning = FALSE;

WORD m_wEmulatorCPUBreakpoint = 0177777;

BOOL m_okEmulatorSound = FALSE;
BOOL m_okEmulatorCovox = FALSE;

long m_nFrameCount = 0;
DWORD m_dwTickCount = 0;
DWORD m_dwEmulatorUptime = 0;  // Machine uptime, seconds, from turn on or reset, increments every 25 frames
long m_nUptimeFrameCount = 0;

BYTE* g_pEmulatorRam;  // RAM values - for change tracking
BYTE* g_pEmulatorChangedRam;  // RAM change flags
WORD g_wEmulatorCpuPC = 0177777;      // Current PC value
WORD g_wEmulatorPrevCpuPC = 0177777;  // Previous PC value

void CALLBACK Emulator_SoundGenCallback(unsigned short L, unsigned short R);

enum
{
    TAPEMODE_STOPPED = 0,
    TAPEMODE_STARTED = 1,
    TAPEMODE_READING = 2,
    TAPEMODE_FINISHED = -1
} m_EmulatorTapeMode = TAPEMODE_STOPPED;
int m_EmulatorTapeCount = 0;

//////////////////////////////////////////////////////////////////////


const LPCTSTR FILENAME_ROM0 = _T("rom0.rr1");
const LPCTSTR FILENAME_ROM1 = _T("rom1.rr1");


//////////////////////////////////////////////////////////////////////

BOOL Emulator_LoadRomFile(LPCTSTR strFileName, BYTE* buffer, DWORD fileOffset, DWORD bytesToRead)
{
    FILE* fpRomFile = ::_tfsopen(strFileName, _T("rb"), _SH_DENYWR);
    if (fpRomFile == NULL)
        return FALSE;

    ASSERT(bytesToRead <= 8192);
    ::memset(buffer, 0, 8192);

    if (fileOffset > 0)
    {
        ::fseek(fpRomFile, fileOffset, SEEK_SET);
    }

    size_t dwBytesRead = ::fread(buffer, 1, bytesToRead, fpRomFile);
    if (dwBytesRead != bytesToRead)
    {
        ::fclose(fpRomFile);
        return FALSE;
    }

    ::fclose(fpRomFile);

    return TRUE;
}

BOOL Emulator_Init()
{
    ASSERT(g_pBoard == NULL);

    CProcessor::Init();

    g_pBoard = new CMotherboard();

    // Allocate memory for old RAM values
    g_pEmulatorRam = (BYTE*) ::malloc(65536);  ::memset(g_pEmulatorRam, 0, 65536);
    g_pEmulatorChangedRam = (BYTE*) ::malloc(65536);  ::memset(g_pEmulatorChangedRam, 0, 65536);

    g_pBoard->Reset();

    if (m_okEmulatorSound)
    {
        //SoundGen_Initialize(Settings_GetSoundVolume());
        g_pBoard->SetSoundGenCallback(Emulator_SoundGenCallback);
    }

    //g_pBoard->SetTeletypeCallback(Emulator_TeletypeCallback);

    m_EmulatorTapeMode = TAPEMODE_STOPPED;

    return TRUE;
}

void Emulator_Done()
{
    ASSERT(g_pBoard != NULL);

    CProcessor::Done();

    g_pBoard->SetSoundGenCallback(NULL);
    //SoundGen_Finalize();

    delete g_pBoard;
    g_pBoard = NULL;

    // Free memory used for old RAM values
    ::free(g_pEmulatorRam);
    ::free(g_pEmulatorChangedRam);
}

BOOL Emulator_InitConfiguration(NeonConfiguration configuration)
{
    g_pBoard->SetConfiguration(configuration);

    BYTE buffer[8192];

    // Load ROM file
    if (!Emulator_LoadRomFile(FILENAME_ROM0, buffer, 0, 8192))
    {
        AlertWarning(_T("Failed to load ROM0 file."));
        return FALSE;
    }
    g_pBoard->LoadROM(0, buffer);

    if (!Emulator_LoadRomFile(FILENAME_ROM1, buffer, 0, 8192))
    {
        AlertWarning(_T("Failed to load ROM1 file."));
        return FALSE;
    }
    g_pBoard->LoadROM(1, buffer);

    g_nEmulatorConfiguration = configuration;

    g_pBoard->Reset();

    m_nUptimeFrameCount = 0;
    m_dwEmulatorUptime = 0;

    return TRUE;
}

void Emulator_Start()
{
    g_okEmulatorRunning = TRUE;

    // Set title bar text
    SetWindowText(g_hwnd, _T("NEON Back to Life [run]"));
    MainWindow_UpdateMenu();

    m_nFrameCount = 0;
    m_dwTickCount = GetTickCount();
}
void Emulator_Stop()
{
    g_okEmulatorRunning = FALSE;
    m_wEmulatorCPUBreakpoint = 0177777;

    // Reset title bar message
    SetWindowText(g_hwnd, _T("NEON Back to Life [stop]"));
    MainWindow_UpdateMenu();
    // Reset FPS indicator
    MainWindow_SetStatusbarText(StatusbarPartFPS, _T(""));

    MainWindow_UpdateAllViews();
}

void Emulator_Reset()
{
    ASSERT(g_pBoard != NULL);

    g_pBoard->Reset();

    m_nUptimeFrameCount = 0;
    m_dwEmulatorUptime = 0;

    m_EmulatorTapeMode = TAPEMODE_STOPPED;

    MainWindow_UpdateAllViews();
}

void Emulator_SetCPUBreakpoint(WORD address)
{
    m_wEmulatorCPUBreakpoint = address;
}

BOOL Emulator_IsBreakpoint()
{
    WORD wCPUAddr = g_pBoard->GetCPU()->GetPC();
    if (wCPUAddr == m_wEmulatorCPUBreakpoint)
        return TRUE;
    return FALSE;
}

void Emulator_SetSound(BOOL soundOnOff)
{
    if (m_okEmulatorSound != soundOnOff)
    {
        if (soundOnOff)
        {
            //SoundGen_Initialize(Settings_GetSoundVolume());
            g_pBoard->SetSoundGenCallback(Emulator_SoundGenCallback);
        }
        else
        {
            g_pBoard->SetSoundGenCallback(NULL);
            //SoundGen_Finalize();
        }
    }

    m_okEmulatorSound = soundOnOff;
}

void Emulator_SetCovox(BOOL covoxOnOff)
{
    m_okEmulatorCovox = covoxOnOff;
}

int Emulator_SystemFrame()
{
    g_pBoard->SetCPUBreakpoint(m_wEmulatorCPUBreakpoint);

    ScreenView_ScanKeyboard();
    ScreenView_ProcessKeyboard();
    Emulator_ProcessJoystick();

    if (!g_pBoard->SystemFrame())
        return 0;

    // Calculate frames per second
    m_nFrameCount++;
    DWORD dwCurrentTicks = GetTickCount();
    long nTicksElapsed = dwCurrentTicks - m_dwTickCount;
    if (nTicksElapsed >= 1200)
    {
        double dFramesPerSecond = m_nFrameCount * 1000.0 / nTicksElapsed;
        TCHAR buffer[16];
        swprintf_s(buffer, 16, _T("FPS: %05.2f"), dFramesPerSecond);
        MainWindow_SetStatusbarText(StatusbarPartFPS, buffer);

        m_nFrameCount = 0;
        m_dwTickCount = dwCurrentTicks;
    }

    // Calculate emulator uptime (25 frames per second)
    m_nUptimeFrameCount++;
    if (m_nUptimeFrameCount >= 25)
    {
        m_dwEmulatorUptime++;
        m_nUptimeFrameCount = 0;

        int seconds = (int) (m_dwEmulatorUptime % 60);
        int minutes = (int) (m_dwEmulatorUptime / 60 % 60);
        int hours   = (int) (m_dwEmulatorUptime / 3600 % 60);

        TCHAR buffer[20];
        swprintf_s(buffer, 20, _T("Uptime: %02d:%02d:%02d"), hours, minutes, seconds);
        MainWindow_SetStatusbarText(StatusbarPartUptime, buffer);
    }

    BOOL okTapeMotor = g_pBoard->IsTapeMotorOn();
    if (Settings_GetTape())
    {
        m_EmulatorTapeMode = okTapeMotor ? TAPEMODE_FINISHED : TAPEMODE_STOPPED;
    }
    else  // Fake tape mode
    {
        switch (m_EmulatorTapeMode)
        {
        case TAPEMODE_STOPPED:
            if (okTapeMotor)
            {
                m_EmulatorTapeMode = TAPEMODE_STARTED;
                m_EmulatorTapeCount = 10;  // wait 2/5 sec
            }
            break;
        case TAPEMODE_STARTED:
            if (!okTapeMotor)
                m_EmulatorTapeMode = TAPEMODE_STOPPED;
            else
            {
                m_EmulatorTapeCount--;
                if (m_EmulatorTapeCount <= 0)
                {
                    WORD pc = g_pBoard->GetCPU()->GetPC();
                    //
                }
            }
            break;
        case TAPEMODE_FINISHED:
            if (!okTapeMotor)
                m_EmulatorTapeMode = TAPEMODE_STOPPED;
            break;
        }
    }

    return 1;
}

void Emulator_ProcessJoystick()
{
    //if (Settings_GetJoystick() == 0)
    //    return;  // NumPad joystick processing is inside ScreenView_ScanKeyboard() function

    //UINT joystate = Joystick_GetJoystickState();
    //g_pBoard->SetPrinterInPort(joystate);
}

void CALLBACK Emulator_SoundGenCallback(unsigned short L, unsigned short R)
{
    if (m_okEmulatorCovox)
    {
        // Get lower byte from printer port output register
        unsigned short data = g_pBoard->GetPrinterOutPort() & 0xff;
        // Merge with channel data
        L += (data << 7);
        R += (data << 7);
    }

    //SoundGen_FeedDAC(L, R);
}

// Update cached values after Run or Step
void Emulator_OnUpdate()
{
    // Update stored PC value
    g_wEmulatorPrevCpuPC = g_wEmulatorCpuPC;
    g_wEmulatorCpuPC = g_pBoard->GetCPU()->GetPC();

    // Update memory change flags
    {
        BYTE* pOld = g_pEmulatorRam;
        BYTE* pChanged = g_pEmulatorChangedRam;
        WORD addr = 0;
        do
        {
            BYTE newvalue = g_pBoard->GetRAMByte(addr);
            BYTE oldvalue = *pOld;
            *pChanged = (newvalue != oldvalue) ? 255 : 0;
            *pOld = newvalue;
            addr++;
            pOld++;  pChanged++;
        }
        while (addr < 65535);
    }
}

// Get RAM change flag
//   addrtype - address mode - see ADDRTYPE_XXX constants
WORD Emulator_GetChangeRamStatus(WORD address)
{
    return *((WORD*)(g_pEmulatorChangedRam + address));
}

void Emulator_PrepareScreenRGB32(void* pImageBits, int screenMode)
{
    if (pImageBits == NULL) return;

    const CMotherboard* pBoard = g_pBoard;

    WORD vdptaslo = pBoard->GetRAMWordView(0170010);  // VDPTAS
    WORD vdptashi = pBoard->GetRAMWordView(0170012);  // VDPTAS
    WORD vdptaplo = pBoard->GetRAMWordView(0170004);  // VDPTAP
    WORD vdptaphi = pBoard->GetRAMWordView(0170006);  // VDPTAP

    DWORD tasaddr = (((DWORD)vdptaslo) << 2) | (((DWORD)(vdptashi & 017)) << 18);
    //tasaddr += 4 * 16;  //DEBUG: Skip first lines
    for (int line = 0; line < NEON_SCREEN_HEIGHT; line++)  // ÷икл по строкам
    {
        DWORD* plinebits = ((DWORD*)pImageBits + NEON_SCREEN_WIDTH * (NEON_SCREEN_HEIGHT - 1 - line));

        WORD linelo = pBoard->GetRAMWordView(tasaddr);
        WORD linehi = pBoard->GetRAMWordView(tasaddr + 2);
        tasaddr += 4;
        if (linelo == 0 && linehi == 0)
        {
            ::memset(plinebits, 0, NEON_SCREEN_WIDTH * 4);
            continue;
        }

        DWORD lineaddr = (((DWORD)linelo) << 2) | (((DWORD)(linehi & 017)) << 18);
        int x = 0;
        while (true)  // ÷икл по видеоотрезкам строки, до полного заполнени€ строки
        {
            WORD otrlo = pBoard->GetRAMWordView(lineaddr);
            WORD otrhi = pBoard->GetRAMWordView(lineaddr + 2);
            lineaddr += 4;
            int otrcount = (otrhi >> 10) & 037;  // ƒлина отрезка в 32-разр€дных словах
            if (otrcount == 0) otrcount = 32;
            DWORD otraddr = (((DWORD)otrlo) << 2) | (((DWORD)otrhi & 017) << 18);
            if (otraddr == 0)
            {
                int otrlen = otrcount * 16 * 2;
                if (832 - x - otrlen < 0) otrlen = 832 - x;
                ::memset(plinebits, 0, otrlen * 4);
                plinebits += otrlen;
                x += otrlen;
                if (x >= 832) break;
                continue;
            }
            WORD otrvn = (otrhi >> 6) & 03;  // VN1 и VN0 определ€ют бит/точку
            WORD otrvd = (otrhi >> 8) & 03;  // VD1 и VD0 определ€ют инф.плотность
            for (int i = 0; i < otrcount; i++)  // ÷икл по 32-разр€дным словам отрезка
            {
                WORD bitslo = pBoard->GetRAMWordView(otraddr);
                WORD bitshi = pBoard->GetRAMWordView(otraddr + 2);
                DWORD bits = MAKELONG(bitslo, bitshi);

                for (int i = 0; i < 32; i++)
                {
                    DWORD color = (bits & 1) ? 0x00ffffff : 0/*0x00222222*/;
                    *plinebits = color;  plinebits++;
                    x++;
                    if (x >= 832) break;
                    *plinebits = color;  plinebits++;
                    x++;
                    if (x >= 832) break;
                    bits = bits >> 1;
                }
                if (x >= 832) break;

                otraddr += 4;
            }
            if (x >= 832) break;
        }
    }
}

//////////////////////////////////////////////////////////////////////
//
// Emulator image format - see CMotherboard::SaveToImage()
// Image header format (32 bytes):
//   4 bytes        BK_IMAGE_HEADER1
//   4 bytes        BK_IMAGE_HEADER2
//   4 bytes        BK_IMAGE_VERSION
//   4 bytes        BK_IMAGE_SIZE
//   4 bytes        BK uptime
//   12 bytes       Not used

void Emulator_SaveImage(LPCTSTR sFilePath)
{
    //// Create file
    //HANDLE hFile = CreateFile(sFilePath,
    //        GENERIC_WRITE, FILE_SHARE_READ, NULL,
    //        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    //if (hFile == INVALID_HANDLE_VALUE)
    //{
    //    AlertWarning(_T("Failed to save image file."));
    //    return;
    //}

    //// Allocate memory
    //BYTE* pImage = (BYTE*) ::malloc(BKIMAGE_SIZE);  memset(pImage, 0, BKIMAGE_SIZE);
    //::memset(pImage, 0, BKIMAGE_SIZE);
    //// Prepare header
    //DWORD* pHeader = (DWORD*) pImage;
    //*pHeader++ = BKIMAGE_HEADER1;
    //*pHeader++ = BKIMAGE_HEADER2;
    //*pHeader++ = BKIMAGE_VERSION;
    //*pHeader++ = BKIMAGE_SIZE;
    //// Store emulator state to the image
    ////g_pBoard->SaveToImage(pImage);
    //*(DWORD*)(pImage + 16) = m_dwEmulatorUptime;

    //// Save image to the file
    //DWORD dwBytesWritten = 0;
    //WriteFile(hFile, pImage, BKIMAGE_SIZE, &dwBytesWritten, NULL);
    ////TODO: Check if dwBytesWritten != BKIMAGE_SIZE

    //// Free memory, close file
    //::free(pImage);
    //CloseHandle(hFile);
}

void Emulator_LoadImage(LPCTSTR sFilePath)
{
    //// Open file
    //HANDLE hFile = CreateFile(sFilePath,
    //        GENERIC_READ, FILE_SHARE_READ, NULL,
    //        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    //if (hFile == INVALID_HANDLE_VALUE)
    //{
    //    AlertWarning(_T("Failed to load image file."));
    //    return;
    //}

    //// Read header
    //DWORD bufHeader[BKIMAGE_HEADER_SIZE / sizeof(DWORD)];
    //DWORD dwBytesRead = 0;
    //ReadFile(hFile, bufHeader, BKIMAGE_HEADER_SIZE, &dwBytesRead, NULL);
    ////TODO: Check if dwBytesRead != BKIMAGE_HEADER_SIZE

    ////TODO: Check version and size

    //// Allocate memory
    //BYTE* pImage = (BYTE*) ::malloc(BKIMAGE_SIZE);  ::memset(pImage, 0, BKIMAGE_SIZE);

    //// Read image
    //SetFilePointer(hFile, 0, NULL, FILE_BEGIN);
    //dwBytesRead = 0;
    //ReadFile(hFile, pImage, BKIMAGE_SIZE, &dwBytesRead, NULL);
    ////TODO: Check if dwBytesRead != BKIMAGE_SIZE

    //// Restore emulator state from the image
    ////g_pBoard->LoadFromImage(pImage);

    //m_dwEmulatorUptime = *(DWORD*)(pImage + 16);

    //// Free memory, close file
    //::free(pImage);
    //CloseHandle(hFile);

    //g_okEmulatorRunning = FALSE;

    //MainWindow_UpdateAllViews();
}


//////////////////////////////////////////////////////////////////////
