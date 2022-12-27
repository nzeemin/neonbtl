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
#include <share.h>
#include "Main.h"
#include "Emulator.h"

#include "Views.h"
#include "emubase\Emubase.h"
#include "SoundGen.h"


//////////////////////////////////////////////////////////////////////


CMotherboard* g_pBoard = nullptr;
NeonConfiguration g_nEmulatorConfiguration;  // Current configuration
bool g_okEmulatorRunning = false;

int m_wEmulatorCPUBpsCount = 0;
uint16_t m_EmulatorCPUBps[MAX_BREAKPOINTCOUNT + 1];
uint16_t m_wEmulatorTempCPUBreakpoint = 0177777;
int m_wEmulatorWatchesCount = 0;
uint16_t m_EmulatorWatches[MAX_BREAKPOINTCOUNT];

bool m_okEmulatorSound = false;
bool m_okEmulatorCovox = false;

bool m_okEmulatorSerial = false;
FILE* m_fpEmulatorSerialOut = nullptr;

long m_nFrameCount = 0;
uint32_t m_dwTickCount = 0;
uint32_t m_dwEmulatorUptime = 0;  // Machine uptime, seconds, from turn on or reset, increments every 25 frames
long m_nUptimeFrameCount = 0;

uint8_t* g_pEmulatorRam = nullptr;  // RAM values - for change tracking
uint8_t* g_pEmulatorChangedRam = nullptr;  // RAM change flags
uint16_t g_wEmulatorCpuPC = 0;      // Current PC value
uint16_t g_wEmulatorPrevCpuPC = 0;  // Previous PC value

void CALLBACK Emulator_SoundGenCallback(unsigned short L, unsigned short R);


//////////////////////////////////////////////////////////////////////


const LPCTSTR NEON_ROM_FILENAME = _T("pk11.rom");
const size_t NEON_ROM_SIZE = 16384;

bool Emulator_LoadNeonRom()
{
    void * pData = ::calloc(NEON_ROM_SIZE, 1);
    if (pData == nullptr)
        return false;

    FILE* fpFile = ::_tfsopen(NEON_ROM_FILENAME, _T("rb"), _SH_DENYWR);
    if (fpFile != nullptr)
    {
        size_t dwBytesRead = ::fread(pData, 1, NEON_ROM_SIZE, fpFile);
        ::fclose(fpFile);
        if (dwBytesRead != NEON_ROM_SIZE)
        {
            ::free(pData);
            return false;
        }
    }
    else
    {
        HRSRC hRes = NULL;
        DWORD dwDataSize = 0;
        HGLOBAL hResLoaded = NULL;
        void * pResData = nullptr;
        if ((hRes = ::FindResource(NULL, MAKEINTRESOURCE(IDR_NEON_ROM), _T("BIN"))) == NULL ||
            (dwDataSize = ::SizeofResource(NULL, hRes)) < NEON_ROM_SIZE ||
            (hResLoaded = ::LoadResource(NULL, hRes)) == NULL ||
            (pResData = ::LockResource(hResLoaded)) == NULL)
        {
            ::free(pData);
            return false;
        }
        ::memcpy(pData, pResData, NEON_ROM_SIZE);
    }

    g_pBoard->LoadROM((const uint8_t *)pData);
    ::free(pData);
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
    m_wEmulatorWatchesCount = 0;
    for (int i = 0; i < MAX_WATCHESCOUNT; i++)
    {
        m_EmulatorWatches[i] = 0177777;
    }

    g_pBoard = new CMotherboard();

    // Allocate memory for old RAM values
    g_pEmulatorRam = static_cast<uint8_t*>(::calloc(65536, 1));
    g_pEmulatorChangedRam = static_cast<uint8_t*>(::calloc(65536, 1));

    g_pBoard->Reset();

    if (m_okEmulatorSound)
    {
        SoundGen_Initialize(Settings_GetSoundVolume());
        g_pBoard->SetSoundGenCallback(Emulator_SoundGenCallback);
    }

    Emulator_SetSerial(true);  //DEBUG

    return true;
}

void Emulator_Done()
{
    ASSERT(g_pBoard != nullptr);

    CProcessor::Done();

    g_pBoard->SetSoundGenCallback(nullptr);
    SoundGen_Finalize();

    delete g_pBoard;
    g_pBoard = nullptr;

    // Free memory used for old RAM values
    ::free(g_pEmulatorRam);
    ::free(g_pEmulatorChangedRam);
}

bool Emulator_InitConfiguration(NeonConfiguration configuration)
{
    g_pBoard->SetConfiguration((uint16_t)configuration);

    if (!Emulator_LoadNeonRom())
    {
        AlertWarning(_T("Failed to load ROM file."));
        return false;
    }

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

    if (m_fpEmulatorSerialOut != nullptr)
        ::fflush(m_fpEmulatorSerialOut);

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
        if (m_EmulatorCPUBps[i] > address)  // found the place
        {
            memcpy(m_EmulatorCPUBps + i + 1, m_EmulatorCPUBps + i, sizeof(uint16_t) * (m_wEmulatorCPUBpsCount - i));
            m_EmulatorCPUBps[i] = address;
            break;
        }
        if (m_EmulatorCPUBps[i] == 0177777)  // found empty place
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
                memcpy(m_EmulatorCPUBps + i, m_EmulatorCPUBps + i + 1, sizeof(uint16_t) * (m_wEmulatorCPUBpsCount - i));
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

const uint16_t* Emulator_GetWatchList() { return m_EmulatorWatches; }
bool Emulator_AddWatch(uint16_t address)
{
    if (m_wEmulatorWatchesCount == MAX_WATCHESCOUNT - 1 || address == 0177777)
        return false;
    for (int i = 0; i < m_wEmulatorWatchesCount; i++)  // Check if the BP exists
    {
        if (m_EmulatorWatches[i] == address)
            return false;  // Already in the list
    }
    for (int i = 0; i < MAX_BREAKPOINTCOUNT; i++)  // Put in the first empty cell
    {
        if (m_EmulatorWatches[i] == 0177777)
        {
            m_EmulatorWatches[i] = address;
            break;
        }
    }
    m_wEmulatorWatchesCount++;
    return true;
}
bool Emulator_RemoveWatch(uint16_t address)
{
    if (m_wEmulatorWatchesCount == 0 || address == 0177777)
        return false;
    for (int i = 0; i < MAX_WATCHESCOUNT; i++)
    {
        if (m_EmulatorWatches[i] == address)
        {
            m_EmulatorWatches[i] = 0177777;
            m_wEmulatorWatchesCount--;
            if (m_wEmulatorWatchesCount > i)  // fill the hole
            {
                m_EmulatorWatches[i] = m_EmulatorWatches[m_wEmulatorWatchesCount];
                m_EmulatorWatches[m_wEmulatorWatchesCount] = 0177777;
            }
            return true;
        }
    }
    return false;
}
void Emulator_RemoveAllWatches()
{
    for (int i = 0; i < MAX_WATCHESCOUNT; i++)
        m_EmulatorWatches[i] = 0177777;
    m_wEmulatorWatchesCount = 0;
}

void Emulator_SetSound(bool soundOnOff)
{
    if (m_okEmulatorSound != soundOnOff)
    {
        if (soundOnOff)
        {
            SoundGen_Initialize(Settings_GetSoundVolume());
            g_pBoard->SetSoundGenCallback(Emulator_SoundGenCallback);
        }
        else
        {
            g_pBoard->SetSoundGenCallback(nullptr);
            SoundGen_Finalize();
        }
    }

    m_okEmulatorSound = soundOnOff;
}

void Emulator_SetCovox(bool covoxOnOff)
{
    m_okEmulatorCovox = covoxOnOff;
}

void CALLBACK Emulator_SerialOut_Callback(uint8_t byte)
{
    if (m_fpEmulatorSerialOut != nullptr)
    {
        ::fwrite(&byte, 1, 1, m_fpEmulatorSerialOut);
    }
}

void Emulator_SetSerial(bool onOff)
{
    if (m_okEmulatorSerial == onOff)
        return;

    if (!onOff)
    {
        g_pBoard->SetSerialOutCallback(nullptr);
        if (m_fpEmulatorSerialOut != nullptr)
        {
            ::fflush(m_fpEmulatorSerialOut);
            ::fclose(m_fpEmulatorSerialOut);
        }
    }
    else
    {
        g_pBoard->SetSerialOutCallback(Emulator_SerialOut_Callback);
        m_fpEmulatorSerialOut = ::_tfopen(_T("serial.log"), _T("wb"));
    }

    m_okEmulatorSerial = onOff;
}

bool Emulator_SystemFrame()
{
    g_pBoard->SetCPUBreakpoints(m_wEmulatorCPUBpsCount > 0 ? m_EmulatorCPUBps : nullptr);

    ScreenView_ScanKeyboard();
    ScreenView_ProcessKeyboard();
    //Emulator_ProcessJoystick();

    if (!g_pBoard->SystemFrame())
    {
        uint16_t pc = g_pBoard->GetCPU()->GetPC();
        if (pc != m_wEmulatorTempCPUBreakpoint)
            DebugPrintFormat(_T("Breakpoint hit at %06ho\r\n"), pc);
        return false;
    }

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

    return true;
}

void CALLBACK Emulator_SoundGenCallback(unsigned short L, unsigned short R)
{
    SoundGen_FeedDAC(L, R);
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

// �������� �������, ���������� ��� ������ �������������� ������ ������
typedef void (CALLBACK* SCREEN_LINE_CALLBACK)(uint32_t* pImageBits, const uint32_t* pLineBits, int line);

void CALLBACK PrepareScreenLine416x300(uint32_t* pImageBits, const uint32_t* pLineBits, int line);
void CALLBACK PrepareScreenLine624x450(uint32_t* pImageBits, const uint32_t* pLineBits, int line);
void CALLBACK PrepareScreenLine832x600(uint32_t* pImageBits, const uint32_t* pLineBits, int line);
void CALLBACK PrepareScreenLine1040x750(uint32_t* pImageBits, const uint32_t* pLineBits, int line);
void CALLBACK PrepareScreenLine1248x900(uint32_t* pImageBits, const uint32_t* pLineBits, int line);

struct ScreenModeStruct
{
    int width;
    int height;
    SCREEN_LINE_CALLBACK lineCallback;
}
static ScreenModeReference[] =
{
    // wid  hei  callback                           size      scaleX scaleY   notes
    {  416, 300, PrepareScreenLine416x300  },  //  416 x 300   0.5     1      Debug mode
    {  624, 450, PrepareScreenLine624x450  },  //  624 x 450   0.75    1.5
    {  832, 600, PrepareScreenLine832x600  },  //  832 x 600   1       2
    { 1040, 750, PrepareScreenLine1040x750 },  // 1040 x 750   1.25    2.5
    { 1248, 900, PrepareScreenLine1248x900 },  // 1248 x 900   1.5     3
};

void Emulator_GetScreenSize(int scrmode, int* pwid, int* phei)
{
    if (scrmode < 0 || scrmode >= sizeof(ScreenModeReference) / sizeof(ScreenModeStruct))
        return;
    ScreenModeStruct* pinfo = ScreenModeReference + scrmode;
    *pwid = pinfo->width;
    *phei = pinfo->height;
}

void Emulator_PrepareScreenLines(void* pImageBits, SCREEN_LINE_CALLBACK lineCallback);

void Emulator_PrepareScreenRGB32(void* pImageBits, int screenMode)
{
    if (pImageBits == nullptr) return;

    // Render to bitmap
    SCREEN_LINE_CALLBACK lineCallback = ScreenModeReference[screenMode].lineCallback;
    Emulator_PrepareScreenLines(pImageBits, lineCallback);
}

uint32_t Color16Convert(uint16_t color)
{
    return RGB(
            (color & 0x0300) >> 2 | (color & 0x0007) << 3 | (color & 0x0300) >> 7,
            (color & 0xe000) >> 8 | (color & 0x00e0) >> 3 | (color & 0xC000) >> 14,
            (color & 0x1C00) >> 5 | (color & 0x0018) | (color & 0x1C00) >> 10);
}

#define FILL1PIXEL(color) { *plinebits++ = color; }
#define FILL2PIXELS(color) { *plinebits++ = color; *plinebits++ = color; }
#define FILL4PIXELS(color) { *plinebits++ = color; *plinebits++ = color; *plinebits++ = color; *plinebits++ = color; }
#define FILL8PIXELS(color) { \
    *plinebits++ = color; *plinebits++ = color; *plinebits++ = color; *plinebits++ = color; \
    *plinebits++ = color; *plinebits++ = color; *plinebits++ = color; *plinebits++ = color; \
}
// ��������� ��� ��������� 16-���������� ����� �� �������; pala = ����� �������� �����
#define GETPALETTEHILO(pala) ((uint16_t)(pBoard->GetRAMByteView(pala) << 8) | pBoard->GetRAMByteView((pala) + 256))

// ��������� 300 ����� ������; ��� ������ �������������� ������ �������� ������� lineCallback
void Emulator_PrepareScreenLines(void* pImageBits, SCREEN_LINE_CALLBACK lineCallback)
{
    if (pImageBits == nullptr || lineCallback == nullptr || g_pBoard == nullptr) return;

    uint32_t linebits[NEON_SCREEN_WIDTH];  // ����� ��� ������

    const CMotherboard* pBoard = g_pBoard;

    uint16_t vdptaslo = pBoard->GetRAMWordView(0000010);  // VDPTAS
    uint16_t vdptashi = pBoard->GetRAMWordView(0000012);  // VDPTAS
    uint16_t vdptaplo = pBoard->GetRAMWordView(0000004);  // VDPTAP
    uint16_t vdptaphi = pBoard->GetRAMWordView(0000006);  // VDPTAP

    uint32_t tasaddr = (((uint32_t)vdptaslo) << 2) | (((uint32_t)(vdptashi & 0x000f)) << 18);
    uint32_t tapaddr = (((uint32_t)vdptaplo) << 2) | (((uint32_t)(vdptaphi & 0x000f)) << 18);
    uint16_t pal0 = GETPALETTEHILO(tapaddr);
    uint32_t colorBorder = Color16Convert(pal0);  // ���������� ���� �������

    for (int line = 0; line < NEON_SCREEN_HEIGHT; line++)  // ���� �� ������� 0..299
    {
        uint16_t linelo = pBoard->GetRAMWordView(tasaddr);
        uint16_t linehi = pBoard->GetRAMWordView(tasaddr + 2);
        tasaddr += 4;

        uint32_t* plinebits = linebits;
        uint32_t lineaddr = (((uint32_t)linelo) << 2) | (((uint32_t)(linehi & 0x000f)) << 18);
        bool firstOtr = true;  // ������� ������� ������� � ������
        uint32_t colorbprev = 0;  // ���� ������� ����������� �������
        int bar = 52;  // ������� ������� �� 52 � 0
        for (;;)  // ���� �� ������������� ������, �� ������� ���������� ������
        {
            uint16_t otrlo = pBoard->GetRAMWordView(lineaddr);
            uint16_t otrhi = pBoard->GetRAMWordView(lineaddr + 2);
            lineaddr += 4;
            // �������� ��������� �������
            int otrcount = 32 - (otrhi >> 10) & 037;  // ����� ������� � 32-��������� ������
            if (otrcount == 0) otrcount = 32;
            uint32_t otraddr = (((uint32_t)otrlo) << 2) | (((uint32_t)otrhi & 0x000f) << 18);
            uint16_t otrvn = (otrhi >> 6) & 3;  // VN1 VN0 - ���/�����
            bool otrpb = (otrhi & 0x8000) != 0;
            uint16_t vmode = (otrhi >> 6) & 0x0f;  // ���� VD1 VD0 VN1 VN0
            // �������� ����� �������
            uint32_t paladdr = tapaddr;
            if (otrvn == 3 && otrpb)  // ������������ �����
            {
                paladdr += (otrhi & 0x10) ? 1024 + 512 : 1024;
            }
            else
            {
                paladdr += (otrpb ? 512 : 0) + (otrvn * 64);
                uint32_t otrpn = (otrhi >> 4) & 3;  // PN1 PN0 - ����� �������
                paladdr += otrpn * 16;
            }
            // ������
            uint16_t palbhi = pBoard->GetRAMWordView(paladdr);
            uint16_t palblo = pBoard->GetRAMWordView(paladdr + 256);
            uint32_t colorb = Color16Convert((uint16_t)((palbhi & 0xff) << 8 | (palblo & 0xff)));
            if (!firstOtr)  // ��� �� ������ ������� - ����� ������, ����� �� ��������: AAAAAAAAABBCCCCC
            {
                FILL8PIXELS(colorbprev)  FILL1PIXEL(colorbprev)
                FILL2PIXELS(colorBorder)
                FILL4PIXELS(colorb)  FILL1PIXEL(colorb)
                bar--;  if (bar == 0) break;
            }
            colorbprev = colorb;  // ���������� ���� �������
            // ����������, ������� 16-���������� ������� ����� ���������
            int barcount = otrcount * 2;
            if (!firstOtr) barcount--;
            if (barcount > bar) barcount = bar;
            bar -= barcount;
            // ��������� �������
            if (vmode == 0)  // VM1, ��������� �����-������ 52 �����, �� ������� ����� �� 2 �����
            {
                uint16_t pal14hi = pBoard->GetRAMWordView(paladdr + 14);
                uint16_t pal14lo = pBoard->GetRAMWordView(paladdr + 14 + 256);
                uint32_t color0 = Color16Convert((uint16_t)((pal14hi & 0xff) << 8 | (pal14lo & 0xff)));
                uint32_t color1 = Color16Convert((uint16_t)((pal14hi & 0xff00) | (pal14lo & 0xff00) >> 8));
                while (barcount > 0)
                {
                    uint16_t bits = pBoard->GetRAMByteView(otraddr);
                    otraddr++;
                    for (uint16_t k = 0; k < 8; k++)
                    {
                        uint32_t color = (bits & 1) ? color1 : color0;
                        FILL2PIXELS(color)
                        bits = bits >> 1;
                    }
                    barcount--;
                }
            }
            else if (vmode == 1)  // VM2, ��������� �����-������ 52 �����
            {
                while (barcount > 0)
                {
                    uint8_t bits = pBoard->GetRAMByteView(otraddr);  // ������ ���� - ������� 16 ��������
                    otraddr++;
                    uint32_t palc = paladdr + (bits & 3);
                    uint16_t c = GETPALETTEHILO(palc);
                    uint32_t color = Color16Convert(c);
                    FILL4PIXELS(color)
                    palc = paladdr + ((bits >> 2) & 3);
                    c = GETPALETTEHILO(palc);
                    color = Color16Convert(c);
                    FILL4PIXELS(color)
                    palc = paladdr + ((bits >> 4) & 3);
                    c = GETPALETTEHILO(palc);
                    color = Color16Convert(c);
                    FILL4PIXELS(color)
                    palc = paladdr + (bits >> 6);
                    c = GETPALETTEHILO(palc);
                    color = Color16Convert(c);
                    FILL4PIXELS(color)
                    barcount--;
                }
            }
            else if (vmode == 2 || vmode == 6 ||
                    vmode == 3 && !otrpb ||
                    vmode == 7 && !otrpb)  // VM4, ��������� �����-������ 52 �����
            {
                while (barcount > 0)
                {
                    uint8_t bits = pBoard->GetRAMByteView(otraddr);  // ������ ���� - ������� 16 ��������
                    otraddr++;
                    uint32_t palc = paladdr + (bits & 15);
                    uint16_t c = GETPALETTEHILO(palc);
                    uint32_t color = Color16Convert(c);
                    FILL8PIXELS(color)
                    palc = paladdr + (bits >> 4);
                    c = GETPALETTEHILO(palc);
                    color = Color16Convert(c);
                    FILL8PIXELS(color)
                    barcount--;
                }
            }
            else if (vmode == 3 && otrpb ||
                    vmode == 7 && otrpb)  // VM8, ��������� �����-������ 52 �����
            {
                while (barcount > 0)
                {
                    uint8_t bits = pBoard->GetRAMByteView(otraddr);  // ������ ���� - ������� 16 ��������
                    otraddr++;
                    uint32_t palc = paladdr + bits;
                    uint16_t c = GETPALETTEHILO(palc);
                    uint32_t color = Color16Convert(c);
                    FILL8PIXELS(color)
                    FILL8PIXELS(color)
                    barcount--;
                }
            }
            else if (vmode == 4)  // VM1, ��������� �����-������ 52 �����
            {
                uint16_t pal14hi = pBoard->GetRAMWordView(paladdr + 14);
                uint16_t pal14lo = pBoard->GetRAMWordView(paladdr + 14 + 256);
                uint32_t color0 = Color16Convert((uint16_t)((pal14hi & 0xff) << 8 | (pal14lo & 0xff)));
                uint32_t color1 = Color16Convert((uint16_t)((pal14hi & 0xff00) | (pal14lo & 0xff00) >> 8));
                while (barcount > 0)
                {
                    uint16_t bits = pBoard->GetRAMWordView(otraddr & ~1);
                    if (otraddr & 1) bits = bits >> 8;
                    otraddr++;
                    for (uint16_t k = 0; k < 8; k++)
                    {
                        uint32_t color = (bits & 1) ? color1 : color0;
                        FILL2PIXELS(color)
                        bits = bits >> 1;
                    }
                    barcount--;
                }
            }
            else if (vmode == 5)  // VM2, ��������� �����-������ 52 �����
            {
                while (barcount > 0)
                {
                    uint8_t bits = pBoard->GetRAMByteView(otraddr);  // ������ ���� - ������� 16 ��������
                    otraddr++;
                    uint32_t palc0 = (paladdr + 12 + (bits & 3));
                    uint16_t c0 = GETPALETTEHILO(palc0);
                    uint32_t color0 = Color16Convert(c0);
                    FILL4PIXELS(color0)
                    uint32_t palc1 = (paladdr + 12 + ((bits >> 2) & 3));
                    uint16_t c1 = GETPALETTEHILO(palc1);
                    uint32_t color1 = Color16Convert(c1);
                    FILL4PIXELS(color1)
                    uint32_t palc2 = (paladdr + 12 + ((bits >> 4) & 3));
                    uint16_t c2 = GETPALETTEHILO(palc2);
                    uint32_t color2 = Color16Convert(c2);
                    FILL4PIXELS(color2)
                    uint32_t palc3 = (paladdr + 12 + ((bits >> 6) & 3));
                    uint16_t c3 = GETPALETTEHILO(palc3);
                    uint32_t color3 = Color16Convert(c3);
                    FILL4PIXELS(color3)
                    barcount--;
                }
            }
            else if (vmode == 8)  // VM1, ��������� �����-������ 104 �����
            {
                uint16_t pal14hi = pBoard->GetRAMWordView(paladdr + 14);
                uint16_t pal14lo = pBoard->GetRAMWordView(paladdr + 14 + 256);
                uint32_t color0 = Color16Convert((uint16_t)((pal14hi & 0xff) << 8 | (pal14lo & 0xff)));
                uint32_t color1 = Color16Convert((uint16_t)((pal14hi & 0xff00) | (pal14lo & 0xff00) >> 8));
                while (barcount > 0)
                {
                    uint16_t bits = pBoard->GetRAMWordView(otraddr);
                    otraddr += 2;
                    for (uint16_t k = 0; k < 16; k++)
                    {
                        uint32_t color = (bits & 1) ? color1 : color0;
                        FILL1PIXEL(color)
                        bits = bits >> 1;
                    }
                    barcount--;
                }
            }
            else if (vmode == 9)
            {
                while (barcount > 0)
                {
                    uint16_t bits = pBoard->GetRAMWordView(otraddr);  // ������ ����� - ������� 16 ��������
                    otraddr += 2;
                    uint32_t palc0 = (paladdr + 12 + (bits & 3));
                    uint16_t c0 = GETPALETTEHILO(palc0);
                    uint32_t color0 = Color16Convert(c0);
                    FILL2PIXELS(color0)
                    uint32_t palc1 = (paladdr + 12 + ((bits >> 2) & 3));
                    uint16_t c1 = GETPALETTEHILO(palc1);
                    uint32_t color1 = Color16Convert(c1);
                    FILL2PIXELS(color1)
                    uint32_t palc2 = (paladdr + 12 + ((bits >> 4) & 3));
                    uint16_t c2 = GETPALETTEHILO(palc2);
                    uint32_t color2 = Color16Convert(c2);
                    FILL2PIXELS(color2)
                    uint32_t palc3 = (paladdr + 12 + ((bits >> 6) & 3));
                    uint16_t c3 = GETPALETTEHILO(palc3);
                    uint32_t color3 = Color16Convert(c3);
                    FILL2PIXELS(color3)
                    uint32_t palc4 = (paladdr + 12 + ((bits >> 8) & 3));
                    uint16_t c4 = GETPALETTEHILO(palc4);
                    uint32_t color4 = Color16Convert(c4);
                    FILL2PIXELS(color4)
                    uint32_t palc5 = (paladdr + 12 + ((bits >> 10) & 3));
                    uint16_t c5 = GETPALETTEHILO(palc5);
                    uint32_t color5 = Color16Convert(c5);
                    FILL2PIXELS(color5)
                    uint32_t palc6 = (paladdr + 12 + ((bits >> 12) & 3));
                    uint16_t c6 = GETPALETTEHILO(palc6);
                    uint32_t color6 = Color16Convert(c6);
                    FILL2PIXELS(color6)
                    uint32_t palc7 = (paladdr + 12 + ((bits >> 14) & 3));
                    uint16_t c7 = GETPALETTEHILO(palc7);
                    uint32_t color7 = Color16Convert(c7);
                    FILL2PIXELS(color7)
                    barcount--;
                }
            }
            else if (vmode == 10)  // VM4, ��������� �����-������ 104 �����
            {
                while (barcount > 0)
                {
                    uint16_t bits = pBoard->GetRAMWordView(otraddr);  // ������ ����� - ������� 16 ��������
                    otraddr += 2;
                    uint32_t palc = paladdr + (bits & 15);
                    uint16_t c = GETPALETTEHILO(palc);
                    uint32_t color = Color16Convert(c);
                    FILL4PIXELS(color)
                    palc = paladdr + ((bits >> 4) & 15);
                    c = GETPALETTEHILO(palc);
                    color = Color16Convert(c);
                    FILL4PIXELS(color)
                    palc = paladdr + ((bits >> 8) & 15);
                    c = GETPALETTEHILO(palc);
                    color = Color16Convert(c);
                    FILL4PIXELS(color)
                    palc = paladdr + ((bits >> 12) & 15);
                    c = GETPALETTEHILO(palc);
                    color = Color16Convert(c);
                    FILL4PIXELS(color)
                    barcount--;
                }
            }
            else if (vmode == 11 && !otrpb)  // VM41, ��������� �����-������ 104 �����
            {
                while (barcount > 0)
                {
                    uint16_t bits = pBoard->GetRAMWordView(otraddr);  // ������ ����� - ������� 16 ��������
                    otraddr += 2;
                    uint32_t palc0 = (paladdr + (bits & 15));
                    uint16_t c0 = GETPALETTEHILO(palc0);
                    uint32_t color0 = Color16Convert(c0);
                    FILL4PIXELS(color0)
                    uint32_t palc1 = (paladdr + ((bits >> 4) & 15));
                    uint16_t c1 = GETPALETTEHILO(palc1);
                    uint32_t color1 = Color16Convert(c1);
                    FILL4PIXELS(color1)
                    uint32_t palc2 = (paladdr + ((bits >> 8) & 15));
                    uint16_t c2 = GETPALETTEHILO(palc2);
                    uint32_t color2 = Color16Convert(c2);
                    FILL4PIXELS(color2)
                    uint32_t palc3 = (paladdr + ((bits >> 12) & 15));
                    uint16_t c3 = GETPALETTEHILO(palc3);
                    uint32_t color3 = Color16Convert(c3);
                    FILL4PIXELS(color3)
                    barcount--;
                }
            }
            else if (vmode == 11 && otrpb)  // VM8, ��������� �����-������ 104 �����
            {
                while (barcount > 0)
                {
                    uint16_t bits = pBoard->GetRAMWordView(otraddr);  // ������ ����� - ������� 16 ��������
                    otraddr += 2;
                    uint32_t palc0 = (paladdr + (bits & 0xff));
                    uint16_t c0 = GETPALETTEHILO(palc0);
                    uint32_t color0 = Color16Convert(c0);
                    FILL8PIXELS(color0)
                    uint32_t palc1 = (paladdr + (bits >> 8));
                    uint16_t c1 = GETPALETTEHILO(palc1);
                    uint32_t color1 = Color16Convert(c1);
                    FILL8PIXELS(color1)
                    barcount--;
                }
            }
            else if (vmode == 13)  // VM2, ��������� �����-������ 208 ����
            {
                for (int j = 0; j < barcount * 2; j++)
                {
                    uint16_t bits = pBoard->GetRAMWordView(otraddr);  // ������ ����� - ������� 8 ��������
                    otraddr += 2;
                    uint32_t palc0 = (paladdr + 12 + (bits & 3));
                    uint16_t c0 = GETPALETTEHILO(palc0);
                    uint32_t color0 = Color16Convert(c0);
                    FILL1PIXEL(color0)
                    uint32_t palc1 = (paladdr + 12 + ((bits >> 2) & 3));
                    uint16_t c1 = GETPALETTEHILO(palc1);
                    uint32_t color1 = Color16Convert(c1);
                    FILL1PIXEL(color1)
                    uint32_t palc2 = (paladdr + 12 + ((bits >> 4) & 3));
                    uint16_t c2 = GETPALETTEHILO(palc2);
                    uint32_t color2 = Color16Convert(c2);
                    FILL1PIXEL(color2)
                    uint32_t palc3 = (paladdr + 12 + ((bits >> 6) & 3));
                    uint16_t c3 = GETPALETTEHILO(palc3);
                    uint32_t color3 = Color16Convert(c3);
                    FILL1PIXEL(color3)
                    uint32_t palc4 = (paladdr + 12 + ((bits >> 8) & 3));
                    uint16_t c4 = GETPALETTEHILO(palc4);
                    uint32_t color4 = Color16Convert(c4);
                    FILL1PIXEL(color4)
                    uint32_t palc5 = (paladdr + 12 + ((bits >> 10) & 3));
                    uint16_t c5 = GETPALETTEHILO(palc5);
                    uint32_t color5 = Color16Convert(c5);
                    FILL1PIXEL(color5)
                    uint32_t palc6 = (paladdr + 12 + ((bits >> 12) & 3));
                    uint16_t c6 = GETPALETTEHILO(palc6);
                    uint32_t color6 = Color16Convert(c6);
                    FILL1PIXEL(color6)
                    uint32_t palc7 = (paladdr + 12 + ((bits >> 14) & 3));
                    uint16_t c7 = GETPALETTEHILO(palc7);
                    uint32_t color7 = Color16Convert(c7);
                    FILL1PIXEL(color7)
                }
            }
            else if ((vmode == 14) ||  // VM4, ��������� �����-������ 208 ����
                    vmode == 15 && !otrpb)  // VM41, ��������� �����-������ 208 ����
            {
                for (int j = 0; j < barcount * 2; j++)
                {
                    uint16_t bits = pBoard->GetRAMWordView(otraddr);  // ������ ����� - ������� 8 ��������
                    otraddr += 2;
                    uint32_t palc0 = (paladdr + (bits & 15));
                    uint16_t c0 = GETPALETTEHILO(palc0);
                    uint32_t color0 = Color16Convert(c0);
                    FILL2PIXELS(color0)
                    uint32_t palc1 = (paladdr + ((bits >> 4) & 15));
                    uint16_t c1 = GETPALETTEHILO(palc1);
                    uint32_t color1 = Color16Convert(c1);
                    FILL2PIXELS(color1)
                    uint32_t palc2 = (paladdr + ((bits >> 8) & 15));
                    uint16_t c2 = GETPALETTEHILO(palc2);
                    uint32_t color2 = Color16Convert(c2);
                    FILL2PIXELS(color2)
                    uint32_t palc3 = (paladdr + ((bits >> 12) & 15));
                    uint16_t c3 = GETPALETTEHILO(palc3);
                    uint32_t color3 = Color16Convert(c3);
                    FILL2PIXELS(color3)
                }
            }
            else if (vmode == 15 && otrpb)
            {
                while (barcount > 0)
                {
                    uint16_t bits0 = pBoard->GetRAMWordView(otraddr);  // ������ ����� - ������� 8 ��������
                    otraddr += 2;
                    uint32_t palc0 = (paladdr + (bits0 & 15));
                    uint16_t c0 = GETPALETTEHILO(palc0);
                    uint32_t color0 = Color16Convert(c0);
                    FILL4PIXELS(color0)
                    uint32_t palc1 = (paladdr + ((bits0 >> 4) & 15));
                    uint16_t c1 = GETPALETTEHILO(palc1);
                    uint32_t color1 = Color16Convert(c1);
                    FILL4PIXELS(color1)
                    uint16_t bits1 = pBoard->GetRAMWordView(otraddr);  // ������ ����� - ������� 8 ��������
                    otraddr += 2;
                    uint32_t palc2 = (paladdr + (bits1 & 15));
                    uint16_t c2 = GETPALETTEHILO(palc2);
                    uint32_t color2 = Color16Convert(c2);
                    FILL4PIXELS(color2)
                    uint32_t palc3 = (paladdr + ((bits1 >> 4) & 15));
                    uint16_t c3 = GETPALETTEHILO(palc3);
                    uint32_t color3 = Color16Convert(c3);
                    FILL4PIXELS(color3)
                    barcount--;
                }
            }
            else //if (vmode == 12)  // VM1, ��������� �����-������ 208 ���� - ����������� �����
            {
                while (barcount > 0)
                {
                    FILL8PIXELS(colorBorder)
                    FILL8PIXELS(colorBorder)
                    barcount--;
                }
            }

            if (bar <= 0) break;
            firstOtr = false;
        }

        (*lineCallback)((uint32_t*)pImageBits, linebits, line);
    }
}

// 1/2 part of "a" plus 1/2 part of "b"
#define AVERAGERGB(a, b)  ( (((a) & 0xfefefeffUL) + ((b) & 0xfefefeffUL)) >> 1 )

// 1/4 part of "a" plus 3/4 parts of "b"
#define AVERAGERGB13(a, b)  ( ((a) == (b)) ? a : (((a) & 0xfcfcfcffUL) >> 2) + ((b) - (((b) & 0xfcfcfcffUL) >> 2)) )

void CALLBACK PrepareScreenLine416x300(uint32_t* pImageBits, const uint32_t* pLineBits, int line)
{
    uint32_t* pBits = pImageBits + (300 - 1 - line) * 416;
    for (int x = 0; x < 832; x += 2)
    {
        uint32_t color1 = *pLineBits++;
        uint32_t color2 = *pLineBits++;
        uint32_t color = AVERAGERGB(color1, color2);
        *pBits++ = color;
    }
}

void CALLBACK PrepareScreenLine624x450(uint32_t* pImageBits, const uint32_t* pLineBits, int line)
{
    bool even = (line & 1) == 0;
    uint32_t* pBits = pImageBits + (450 - 1 - line / 2 * 3) * 624;
    if (!even)
        pBits -= 624 * 2;

    uint32_t* p = pBits;
    for (int x = 0; x < 832; x += 4)  // x0.75 - mapping every 4 pixels into 3 pixels
    {
        uint32_t color1 = *pLineBits++;
        uint32_t color2 = *pLineBits++;
        uint32_t color3 = *pLineBits++;
        uint32_t color4 = *pLineBits++;
        *p++ = AVERAGERGB13(color2, color1);
        *p++ = AVERAGERGB(color2, color3);
        *p++ = AVERAGERGB13(color3, color4);
    }

    if (!even)  // odd line
    {
        uint32_t* pBits1 = pBits;
        uint32_t* pBits12 = pBits1 + 624;
        uint32_t* pBits2 = pBits12 + 624;
        for (int x = 0; x < 624; x++)
        {
            uint32_t color1 = *pBits1++;
            uint32_t color2 = *pBits2++;
            uint32_t color12 = AVERAGERGB(color1, color2);
            *pBits12++ = color12;
        }
    }
}

void CALLBACK PrepareScreenLine832x600(uint32_t* pImageBits, const uint32_t* pLineBits, int line)
{
    uint32_t* pBits = pImageBits + (300 - 1 - line) * 832 * 2;
    memcpy(pBits, pLineBits, sizeof(uint32_t) * 832);
    pBits += 832;
    memcpy(pBits, pLineBits, sizeof(uint32_t) * 832);
}

void CALLBACK PrepareScreenLine1040x750(uint32_t* pImageBits, const uint32_t* pLineBits, int line)
{
    bool even = (line & 1) == 0;
    uint32_t* pBits = pImageBits + (750 - 1 - line / 2 * 5) * 1040;
    if (!even)
        pBits -= 1040 * 3;

    uint32_t* p = pBits;
    for (int x = 0; x < 832; x += 4)  // x1.25 - mapping every 4 pixels into 5 pixels
    {
        uint32_t color1 = *pLineBits++;
        uint32_t color2 = *pLineBits++;
        uint32_t color3 = *pLineBits++;
        uint32_t color4 = *pLineBits++;
        *p++ = color1;
        *p++ = AVERAGERGB13(color1, color2);
        *p++ = AVERAGERGB(color2, color3);
        *p++ = AVERAGERGB13(color4, color3);
        *p++ = color4;
    }

    memcpy(pBits - 1040, pBits, sizeof(uint32_t) * 1040);

    if (!even)  // odd line
    {
        uint32_t* pBits1 = pBits;
        uint32_t* pBits12 = pBits1 + 1040;
        uint32_t* pBits2 = pBits12 + 1040;
        for (int x = 0; x < 1040; x++)
        {
            uint32_t color1 = *pBits1++;
            uint32_t color2 = *pBits2++;
            uint32_t color12 = AVERAGERGB(color1, color2);
            *pBits12++ = color12;
        }
    }
}

void CALLBACK PrepareScreenLine1248x900(uint32_t* pImageBits, const uint32_t* pLineBits, int line)
{
    uint32_t* pBits = pImageBits + (300 - 1 - line) * 1248 * 3;
    uint32_t* p = pBits;
    for (int x = 0; x < 832 / 2; x++)
    {
        uint32_t color1 = *pLineBits++;
        uint32_t color2 = *pLineBits++;
        uint32_t color12 = AVERAGERGB(color1, color2);
        *p++ = color1;
        *p++ = color12;
        *p++ = color2;
    }

    uint32_t* pBits2 = pBits + 1248;
    memcpy(pBits2, pBits, sizeof(uint32_t) * 1248);
    uint32_t* pBits3 = pBits2 + 1248;
    memcpy(pBits3, pBits, sizeof(uint32_t) * 1248);
}


//////////////////////////////////////////////////////////////////////
//
// Emulator image format - see CMotherboard::SaveToImage()
// Image header format (32 bytes):
//   4 bytes        NEON_IMAGE_HEADER1
//   4 bytes        NEON_IMAGE_HEADER2
//   4 bytes        NEON_IMAGE_VERSION
//   4 bytes        NEON_IMAGE_SIZE = 18K + 512/1024/2048/4096 KB
//   4 bytes        NEON uptime
//   12 bytes       Not used

bool Emulator_SaveImage(LPCTSTR sFilePath)
{
    // Create file
    HANDLE hFile = CreateFile(sFilePath,
            GENERIC_WRITE, FILE_SHARE_READ, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        AlertWarning(_T("Failed to save image file."));
        return false;
    }

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
    CloseHandle(hFile);

    return true;
}

bool Emulator_LoadImage(LPCTSTR sFilePath)
{
    // Open file
    HANDLE hFile = CreateFile(sFilePath,
            GENERIC_READ, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        AlertWarning(_T("Failed to load image file."));
        return false;
    }

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
    CloseHandle(hFile);

    g_okEmulatorRunning = false;

    MainWindow_UpdateAllViews();

    return true;
}

void Emulator_LoadMemory()
{
    HANDLE hFile = CreateFile(_T("PK11Memory.bin"),
            GENERIC_READ, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        AlertWarning(_T("Failed to load memory file."));
        return;
    }

    uint8_t buffer[8192];

    for (int bank = 0; bank < 512; bank++)
    {
        DWORD dwBytesRead = 0;
        ReadFile(hFile, buffer, 8192, &dwBytesRead, nullptr);
        g_pBoard->LoadRAMBank(bank, buffer);
    }

    CloseHandle(hFile);
}


//////////////////////////////////////////////////////////////////////
