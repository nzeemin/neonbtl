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
#include "emubase\Emubase.h"

//////////////////////////////////////////////////////////////////////


CMotherboard* g_pBoard = nullptr;
NeonConfiguration g_nEmulatorConfiguration;  // Current configuration
bool g_okEmulatorRunning = false;

int m_wEmulatorCPUBpsCount = 0;
uint16_t m_EmulatorCPUBps[MAX_BREAKPOINTCOUNT + 1];
uint16_t m_wEmulatorTempCPUBreakpoint = 0177777;

bool m_okEmulatorSound = false;
bool m_okEmulatorCovox = false;

long m_nFrameCount = 0;
uint32_t m_dwTickCount = 0;
uint32_t m_dwEmulatorUptime = 0;  // Machine uptime, seconds, from turn on or reset, increments every 25 frames
long m_nUptimeFrameCount = 0;

uint8_t* g_pEmulatorRam = nullptr;  // RAM values - for change tracking
uint8_t* g_pEmulatorChangedRam = nullptr;  // RAM change flags
uint16_t g_wEmulatorCpuPC = 0177777;      // Current PC value
uint16_t g_wEmulatorPrevCpuPC = 0177777;  // Previous PC value

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

bool Emulator_LoadRomFile(LPCTSTR strFileName, uint8_t* buffer, uint32_t fileOffset, uint32_t bytesToRead)
{
    FILE* fpRomFile = ::_tfsopen(strFileName, _T("rb"), _SH_DENYWR);
    if (fpRomFile == nullptr)
        return false;

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
        return false;
    }

    ::fclose(fpRomFile);

    return true;
}

bool Emulator_Init()
{
    ASSERT(g_pBoard == nullptr);

    CProcessor::Init();

    m_wEmulatorCPUBpsCount = 0;
    for (int i = 0; i <= MAX_BREAKPOINTCOUNT; i++)
    {
        m_EmulatorCPUBps[i] = 0177777;
    }

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

    return true;
}

void Emulator_Done()
{
    ASSERT(g_pBoard != nullptr);

    CProcessor::Done();

    g_pBoard->SetSoundGenCallback(nullptr);
    //SoundGen_Finalize();

    delete g_pBoard;
    g_pBoard = nullptr;

    // Free memory used for old RAM values
    ::free(g_pEmulatorRam);
    ::free(g_pEmulatorChangedRam);
}

bool Emulator_InitConfiguration(NeonConfiguration configuration)
{
    g_pBoard->SetConfiguration(configuration);

    uint8_t buffer[8192];

    // Load ROM file
    if (!Emulator_LoadRomFile(FILENAME_ROM0, buffer, 0, 8192))
    {
        AlertWarning(_T("Failed to load ROM0 file."));
        return false;
    }
    g_pBoard->LoadROM(0, buffer);

    if (!Emulator_LoadRomFile(FILENAME_ROM1, buffer, 0, 8192))
    {
        AlertWarning(_T("Failed to load ROM1 file."));
        return false;
    }
    g_pBoard->LoadROM(1, buffer);

    g_nEmulatorConfiguration = configuration;

    g_pBoard->Reset();

    m_nUptimeFrameCount = 0;
    m_dwEmulatorUptime = 0;

    return true;
}

void Emulator_Start()
{
    g_okEmulatorRunning = true;

    // Set title bar text
    MainWindow_UpdateWindowTitle();
    MainWindow_UpdateMenu();

    m_nFrameCount = 0;
    m_dwTickCount = GetTickCount();

    // For proper breakpoint processing
    if (m_wEmulatorCPUBpsCount != 0)
    {
        g_pBoard->GetCPU()->ClearInternalTick();
    }
}
void Emulator_Stop()
{
    g_okEmulatorRunning = false;

    Emulator_SetTempCPUBreakpoint(0177777);

    // Reset title bar message
    MainWindow_UpdateWindowTitle();
    MainWindow_UpdateMenu();

    // Reset FPS indicator
    MainWindow_SetStatusbarText(StatusbarPartFPS, nullptr);

    MainWindow_UpdateAllViews();
}

void Emulator_Reset()
{
    ASSERT(g_pBoard != nullptr);

    g_pBoard->Reset();

    m_nUptimeFrameCount = 0;
    m_dwEmulatorUptime = 0;

    m_EmulatorTapeMode = TAPEMODE_STOPPED;

    MainWindow_UpdateAllViews();
}

bool Emulator_AddCPUBreakpoint(uint16_t address)
{
    if (m_wEmulatorCPUBpsCount == MAX_BREAKPOINTCOUNT - 1 || address == 0177777)
        return false;
    for (int i = 0; i < m_wEmulatorCPUBpsCount; i++)  // Check if the BP exists
    {
        if (m_EmulatorCPUBps[i] == address)
            return false;  // Already in the list
    }
    for (int i = 0; i < MAX_BREAKPOINTCOUNT; i++)  // Put in the first empty cell
    {
        if (m_EmulatorCPUBps[i] == 0177777)
        {
            m_EmulatorCPUBps[i] = address;
            break;
        }
    }
    m_wEmulatorCPUBpsCount++;
    return true;
}
bool Emulator_RemoveCPUBreakpoint(uint16_t address)
{
    if (m_wEmulatorCPUBpsCount == 0 || address == 0177777)
        return false;
    for (int i = 0; i < MAX_BREAKPOINTCOUNT; i++)
    {
        if (m_EmulatorCPUBps[i] == address)
        {
            m_EmulatorCPUBps[i] = 0177777;
            m_wEmulatorCPUBpsCount--;
            if (m_wEmulatorCPUBpsCount > i)  // fill the hole
            {
                m_EmulatorCPUBps[i] = m_EmulatorCPUBps[m_wEmulatorCPUBpsCount];
                m_EmulatorCPUBps[m_wEmulatorCPUBpsCount] = 0177777;
            }
            return true;
        }
    }
    return false;
}
void Emulator_SetTempCPUBreakpoint(uint16_t address)
{
    if (m_wEmulatorTempCPUBreakpoint != 0177777)
        Emulator_RemoveCPUBreakpoint(m_wEmulatorTempCPUBreakpoint);
    if (address == 0177777)
    {
        m_wEmulatorTempCPUBreakpoint = 0177777;
        return;
    }
    for (int i = 0; i < MAX_BREAKPOINTCOUNT; i++)
    {
        if (m_EmulatorCPUBps[i] == address)
            return;  // We have regular breakpoint with the same address
    }
    m_wEmulatorTempCPUBreakpoint = address;
    m_EmulatorCPUBps[m_wEmulatorCPUBpsCount] = address;
    m_wEmulatorCPUBpsCount++;
}
const uint16_t* Emulator_GetCPUBreakpointList() { return m_EmulatorCPUBps; }
bool Emulator_IsBreakpoint()
{
    uint16_t address = g_pBoard->GetCPU()->GetPC();
    if (m_wEmulatorCPUBpsCount > 0)
    {
        for (int i = 0; i < m_wEmulatorCPUBpsCount; i++)
        {
            if (address == m_EmulatorCPUBps[i])
                return true;
        }
    }
    return false;
}
bool Emulator_IsBreakpoint(uint16_t address)
{
    if (m_wEmulatorCPUBpsCount == 0)
        return false;
    for (int i = 0; i < m_wEmulatorCPUBpsCount; i++)
    {
        if (address == m_EmulatorCPUBps[i])
            return true;
    }
    return false;
}
void Emulator_RemoveAllBreakpoints()
{
    for (int i = 0; i < MAX_BREAKPOINTCOUNT; i++)
        m_EmulatorCPUBps[i] = 0177777;
    m_wEmulatorCPUBpsCount = 0;
}

void Emulator_SetSound(bool soundOnOff)
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
            g_pBoard->SetSoundGenCallback(nullptr);
            //SoundGen_Finalize();
        }
    }

    m_okEmulatorSound = soundOnOff;
}

void Emulator_SetCovox(bool covoxOnOff)
{
    m_okEmulatorCovox = covoxOnOff;
}

int Emulator_SystemFrame()
{
    g_pBoard->SetCPUBreakpoints(m_wEmulatorCPUBpsCount > 0 ? m_EmulatorCPUBps : nullptr);

    ScreenView_ScanKeyboard();
    ScreenView_ProcessKeyboard();
    //Emulator_ProcessJoystick();

    if (!g_pBoard->SystemFrame())
        return 0;

    // Calculate frames per second
    m_nFrameCount++;
    uint32_t dwCurrentTicks = GetTickCount();
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

    bool okTapeMotor = g_pBoard->IsTapeMotorOn();
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
                    uint16_t pc = g_pBoard->GetCPU()->GetPC();
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
        uint8_t* pOld = g_pEmulatorRam;
        uint8_t* pChanged = g_pEmulatorChangedRam;
        uint16_t addr = 0;
        do
        {
            uint8_t newvalue = g_pBoard->GetRAMByte(addr);
            uint8_t oldvalue = *pOld;
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
uint16_t Emulator_GetChangeRamStatus(uint16_t address)
{
    return *((uint16_t*)(g_pEmulatorChangedRam + address));
}

void Emulator_PrepareScreenRGB32(void* pImageBits, int screenMode)
{
    if (pImageBits == nullptr) return;

    const CMotherboard* pBoard = g_pBoard;

    uint16_t vdptaslo = pBoard->GetRAMWordView(0170010);  // VDPTAS
    uint16_t vdptashi = pBoard->GetRAMWordView(0170012);  // VDPTAS
    uint16_t vdptaplo = pBoard->GetRAMWordView(0170004);  // VDPTAP
    uint16_t vdptaphi = pBoard->GetRAMWordView(0170006);  // VDPTAP

    uint32_t tasaddr = (((uint32_t)vdptaslo) << 2) | (((uint32_t)(vdptashi & 017)) << 18);
    //tasaddr += 4 * 16;  //DEBUG: Skip first lines
    for (int line = 0; line < NEON_SCREEN_HEIGHT; line++)  // ÷икл по строкам
    {
        uint32_t* plinebits = ((uint32_t*)pImageBits + NEON_SCREEN_WIDTH * (NEON_SCREEN_HEIGHT - 1 - line));

        uint16_t linelo = pBoard->GetRAMWordView(tasaddr);
        uint16_t linehi = pBoard->GetRAMWordView(tasaddr + 2);
        tasaddr += 4;
        if (linelo == 0 && linehi == 0)
        {
            ::memset(plinebits, 0, NEON_SCREEN_WIDTH * 4);
            continue;
        }

        uint32_t lineaddr = (((uint32_t)linelo) << 2) | (((uint32_t)(linehi & 017)) << 18);
        int x = 0;
        while (true)  // ÷икл по видеоотрезкам строки, до полного заполнени€ строки
        {
            uint16_t otrlo = pBoard->GetRAMWordView(lineaddr);
            uint16_t otrhi = pBoard->GetRAMWordView(lineaddr + 2);
            lineaddr += 4;
            int otrcount = (otrhi >> 10) & 037;  // ƒлина отрезка в 32-разр€дных словах
            if (otrcount == 0) otrcount = 32;
            uint32_t otraddr = (((uint32_t)otrlo) << 2) | (((uint32_t)otrhi & 017) << 18);
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
            uint16_t otrvn = (otrhi >> 6) & 03;  // VN1 и VN0 определ€ют бит/точку
            uint16_t otrvd = (otrhi >> 8) & 03;  // VD1 и VD0 определ€ют инф.плотность
            for (int i = 0; i < otrcount; i++)  // ÷икл по 32-разр€дным словам отрезка
            {
                uint16_t bitslo = pBoard->GetRAMWordView(otraddr);
                uint16_t bitshi = pBoard->GetRAMWordView(otraddr + 2);
                uint32_t bits = MAKELONG(bitslo, bitshi);

                for (int i = 0; i < 32; i++)
                {
                    uint32_t color = (bits & 1) ? 0x00ffffff : 0/*0x00222222*/;
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
//   4 bytes        NEON_IMAGE_HEADER1
//   4 bytes        NEON_IMAGE_HEADER2
//   4 bytes        NEON_IMAGE_VERSION
//   4 bytes        NEON_IMAGE_SIZE
//   4 bytes        NEON uptime
//   12 bytes       Not used

bool Emulator_SaveImage(LPCTSTR sFilePath)
{
    //// Create file
    //HANDLE hFile = CreateFile(sFilePath,
    //        GENERIC_WRITE, FILE_SHARE_READ, nullptr,
    //        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    //if (hFile == INVALID_HANDLE_VALUE)
    //{
    //    AlertWarning(_T("Failed to save image file."));
    //    return;
    //}

    //// Allocate memory
    //BYTE* pImage = (BYTE*) ::malloc(BKIMAGE_SIZE);  memset(pImage, 0, BKIMAGE_SIZE);
    //::memset(pImage, 0, BKIMAGE_SIZE);
    //// Prepare header
    //uint32_t* pHeader = (uint32_t*) pImage;
    //*pHeader++ = BKIMAGE_HEADER1;
    //*pHeader++ = BKIMAGE_HEADER2;
    //*pHeader++ = BKIMAGE_VERSION;
    //*pHeader++ = BKIMAGE_SIZE;
    //// Store emulator state to the image
    ////g_pBoard->SaveToImage(pImage);
    //*(uint32_t*)(pImage + 16) = m_dwEmulatorUptime;

    //// Save image to the file
    //uint32_t dwBytesWritten = 0;
    //WriteFile(hFile, pImage, BKIMAGE_SIZE, &dwBytesWritten, nullptr);
    ////TODO: Check if dwBytesWritten != BKIMAGE_SIZE

    //// Free memory, close file
    //::free(pImage);
    //CloseHandle(hFile);

    return true;
}

bool Emulator_LoadImage(LPCTSTR sFilePath)
{
    //// Open file
    //HANDLE hFile = CreateFile(sFilePath,
    //        GENERIC_READ, FILE_SHARE_READ, nullptr,
    //        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    //if (hFile == INVALID_HANDLE_VALUE)
    //{
    //    AlertWarning(_T("Failed to load image file."));
    //    return;
    //}

    //// Read header
    //uint32_t bufHeader[BKIMAGE_HEADER_SIZE / sizeof(uint32_t)];
    //uint32_t dwBytesRead = 0;
    //ReadFile(hFile, bufHeader, BKIMAGE_HEADER_SIZE, &dwBytesRead, nullptr);
    ////TODO: Check if dwBytesRead != BKIMAGE_HEADER_SIZE

    ////TODO: Check version and size

    //// Allocate memory
    //BYTE* pImage = (BYTE*) ::malloc(BKIMAGE_SIZE);  ::memset(pImage, 0, BKIMAGE_SIZE);

    //// Read image
    //SetFilePointer(hFile, 0, nullptr, FILE_BEGIN);
    //dwBytesRead = 0;
    //ReadFile(hFile, pImage, BKIMAGE_SIZE, &dwBytesRead, nullptr);
    ////TODO: Check if dwBytesRead != BKIMAGE_SIZE

    //// Restore emulator state from the image
    ////g_pBoard->LoadFromImage(pImage);

    //m_dwEmulatorUptime = *(uint32_t*)(pImage + 16);

    //// Free memory, close file
    //::free(pImage);
    //CloseHandle(hFile);

    //g_okEmulatorRunning = false;

    //MainWindow_UpdateAllViews();

    return true;
}


//////////////////////////////////////////////////////////////////////
