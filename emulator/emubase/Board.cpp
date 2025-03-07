/*  This file is part of NEONBTL.
    NEONBTL is free software: you can redistribute it and/or modify it under the terms
of the GNU Lesser General Public License as published by the Free Software Foundation,
either version 3 of the License, or (at your option) any later version.
    NEONBTL is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU Lesser General Public License for more details.
    You should have received a copy of the GNU Lesser General Public License along with
NEONBTL. If not, see <http://www.gnu.org/licenses/>. */

// Board.cpp
//

#include "stdafx.h"
#include "Emubase.h"
#include "Board.h"
#include <ctime>

void TraceInstruction(const CProcessor* pProc, const CMotherboard* pBoard, uint16_t address);


// Macro to printf current instruction address along with H/U flag
#define HU_INSTRUCTION_PC (m_pCPU->IsHaltMode() ? _T('H') : _T('U')), m_pCPU->GetInstructionPC()


//////////////////////////////////////////////////////////////////////

CMotherboard::CMotherboard()
{
    // Create devices
    m_pCPU = new CProcessor(this);
    m_pFloppyCtl = new CFloppyController(this);
    m_pHardDrive = nullptr;

    m_dwTrace = 0;
    m_SoundGenCallback = nullptr;
    m_SerialOutCallback = nullptr;
    m_ParallelOutCallback = nullptr;

    ::memset(m_HR, 0, sizeof(m_HR));
    ::memset(m_UR, 0, sizeof(m_UR));

    // Allocate memory
    m_nRamSizeBytes = 0;
    m_pRAM = static_cast<uint8_t*>(::calloc(4096 * 1024, 1));  // 4MB
    m_pROM = static_cast<uint8_t*>(::calloc(16 * 1024, 1));  // 16K
    m_pHDbuff = static_cast<uint8_t*>(::calloc(4 * 512, 1));  // 2K

    m_PPIAwr = m_PPIArd = m_PPIBwr = 0;
    m_PPIBrd = 11;  // IHLT EF1 EF0 - инверсные
    m_PPIC = 14;  // IHLT VIRQ - инверсные
    m_hdsdh = 0;

    m_nHDbuff = 0;
    m_nHDbuffpos = 0;
    m_HDbuffdir = false;
    m_hdint = false;

    m_PICRR = m_PICMR = 0;
    m_PICflags = PIC_MODE_ICW1;

    ::memset(m_keymatrix, 0, sizeof(m_keymatrix));
    m_keyint = false;
    m_keypos = 0;
    m_mousest = m_mousedx = m_mousedy = 0;

    m_rtcticks = 0;

    SetConfiguration(0);  // Default configuration

#ifdef _DEBUG
    m_pFloppyCtl->SetTrace(true);
#endif

    Reset();
}

CMotherboard::~CMotherboard()
{
    // Delete devices
    delete m_pCPU;
    delete m_pFloppyCtl;
    delete m_pHardDrive;

    // Free memory
    ::free(m_pRAM);
    ::free(m_pROM);
    ::free(m_pHDbuff);
}

void CMotherboard::SetConfiguration(uint16_t conf)
{
    // Check RAM bits and set them properly, if not
    uint32_t nRamSizeKbytes = conf & NEON_COPT_RAMSIZE_MASK;
    if (nRamSizeKbytes == 0)
        nRamSizeKbytes = 512;
    if ((conf & NEON_COPT_RAMBANK0_MASK) == 0)
    {
        conf &= ~(NEON_COPT_RAMBANK0_MASK | NEON_COPT_RAMBANK1_MASK);
        switch (nRamSizeKbytes)
        {
        default:
        case 512:
            conf |= 1 << 4;  // 2 x 256K = 512K
            break;
        case 1024:
            conf |= (1 << 4) | (1 << 6);  // 2 x 256K + 2 x 256K = 1024K
            break;
        case 2048:
            conf |= 2 << 4;  // 2 x 1024K = 2048K
            break;
        case 4096:
            conf |= (2 << 4) | (2 << 6);  // 2 x 1024K + 2 x 1024K = 4096K
            break;
        }
    }

    m_Configuration = conf;
    m_nRamSizeBytes = nRamSizeKbytes * 1024;

    // Allocate RAM; clean RAM/ROM
    ::memset(m_pROM, 0, 16 * 1024);

    //// Pre-fill RAM with "uninitialized" values
    //uint16_t * pMemory = (uint16_t *) m_pRAM;
    //uint16_t val = 0;
    //uint8_t flag = 0;
    //for (uint32_t i = 0; i < 128 * 1024; i += 2, flag--)
    //{
    //    *pMemory = val;  pMemory++;
    //    if (flag == 192)
    //        flag = 0;
    //    else
    //        val = ~val;
    //}
}

void CMotherboard::SetTrace(uint32_t dwTrace)
{
    m_dwTrace = dwTrace;
    m_pFloppyCtl->SetTrace((dwTrace & TRACE_FLOPPY) != 0);
}

void CMotherboard::Reset()
{
    m_pCPU->SetDCLOPin(true);
    m_pCPU->SetACLOPin(true);

    m_PPIAwr = m_PPIArd = m_PPIBwr = 0;
    m_PPIBrd = 11;  // IHLT EF1 EF0 - инверсные
    m_PPIC = 14;  // IHLT VIRQ - инверсные
    m_hdsdh = 0;

    m_nHDbuff = 0;
    m_nHDbuffpos = 0;
    m_hdint = false;

    ::memset(m_keymatrix, 0, sizeof(m_keymatrix));
    m_keyint = false;
    m_keypos = 0;

    m_rtcalarmsec = m_rtcalarmmin = m_rtcalarmhour = 0;

    ResetDevices();

    m_pCPU->SetDCLOPin(false);
    m_pCPU->SetACLOPin(false);
}

// Load 16 KB ROM image from the buffer
void CMotherboard::LoadROM(const uint8_t* pBuffer)
{
    ::memcpy(m_pROM, pBuffer, 16384);
}


// Floppy ////////////////////////////////////////////////////////////

bool CMotherboard::IsFloppyImageAttached(int slot) const
{
    ASSERT(slot >= 0 && slot < 2);
    return m_pFloppyCtl->IsAttached(slot);
}

bool CMotherboard::IsFloppyReadOnly(int slot) const
{
    ASSERT(slot >= 0 && slot < 2);
    return m_pFloppyCtl->IsReadOnly(slot);
}

bool CMotherboard::IsFloppyEngineOn() const
{
    return m_pFloppyCtl->IsEngineOn();
}

bool CMotherboard::AttachFloppyImage(int slot, LPCTSTR sFileName)
{
    ASSERT(slot >= 0 && slot < 2);
    return m_pFloppyCtl->AttachImage(slot, sFileName);
}

void CMotherboard::DetachFloppyImage(int slot)
{
    ASSERT(slot >= 0 && slot < 2);
    m_pFloppyCtl->DetachImage(slot);
}

// data = 512 bytes
// result: true - continue reading; false - stop reading
bool CMotherboard::FillHDBuffer(const uint8_t* data)
{
    ASSERT(data != nullptr);

    uint8_t* pBuffer = m_pHDbuff + 512 * m_nHDbuff;
    memcpy(pBuffer, data, 512);

    if (m_nHDbuff >= 3)
    {
        m_nHDbuff = 0;
        m_nHDbuffpos = 0;
        return false;
    }

    m_nHDbuff++;
    return true;
}

const uint8_t* CMotherboard::GetHDBuffer()
{
    if (m_hdscnt == 0)
        return nullptr;  // Nothing to write

    m_hdscnt--;
    const uint8_t* pBuffer = m_pHDbuff + (3 - (m_hdscnt & 3)) * 512;
    return pBuffer;
}


// IDE Hard Drive ////////////////////////////////////////////////////

bool CMotherboard::IsHardImageAttached() const
{
    return (m_pHardDrive != nullptr);
}

bool CMotherboard::IsHardImageReadOnly() const
{
    CHardDrive* pHardDrive = m_pHardDrive;
    if (pHardDrive == nullptr) return false;
    return pHardDrive->IsReadOnly();
}

bool CMotherboard::AttachHardImage(LPCTSTR sFileName)
{
    m_pHardDrive = new CHardDrive();
    bool success = m_pHardDrive->AttachImage(sFileName);
    if (success)
    {
        m_pHardDrive->Reset();
    }
    else
    {
        delete m_pHardDrive;
        m_pHardDrive = nullptr;
    }

    return success;
}
void CMotherboard::DetachHardImage()
{
    delete m_pHardDrive;
    m_pHardDrive = nullptr;
}

uint16_t CMotherboard::GetHardPortWord(uint16_t port)
{
    if (m_pHardDrive == nullptr) return 0;
    port = (uint16_t)((port >> 1) & 7) | 0x1f0;
    uint16_t data = m_pHardDrive->ReadPort(port);
    DebugLogFormat(_T("%c%06ho\tIDE GET %03hx -> 0x%04hx\n"), HU_INSTRUCTION_PC, port, data);
    return data;
}
void CMotherboard::SetHardPortWord(uint16_t port, uint16_t data)
{
    if (m_pHardDrive == nullptr) return;
    port = (uint16_t)((port >> 1) & 7) | 0x1f0;
    DebugLogFormat(_T("%c%06ho\tIDE SET 0x%04hx -> %03hx\n"), HU_INSTRUCTION_PC, data, port);
    m_pHardDrive->WritePort(port, data);
}


// Работа с памятью //////////////////////////////////////////////////

uint16_t CMotherboard::GetRAMWord(uint32_t offset) const
{
    ASSERT(offset < 4096 * 1024);
    return *((uint16_t*)(m_pRAM + offset));
}
uint8_t CMotherboard::GetRAMByte(uint32_t offset) const
{
    return m_pRAM[offset];
}
void CMotherboard::SetRAMWord2(uint32_t offset, uint16_t word)
{
    uint16_t* p = (uint16_t*)(m_pRAM + offset);
    uint16_t mask =
        ((word & 0x0003) == 0 ? 0 : 0x0003) | ((word & 0x000C) == 0 ? 0 : 0x000C) |
        ((word & 0x0030) == 0 ? 0 : 0x0030) | ((word & 0x00C0) == 0 ? 0 : 0x00C0) |
        ((word & 0x0300) == 0 ? 0 : 0x0300) | ((word & 0x0C00) == 0 ? 0 : 0x0C00) |
        ((word & 0x3000) == 0 ? 0 : 0x3000) | ((word & 0xC000) == 0 ? 0 : 0xC000);
    *p = (word & mask) | (*p & ~mask);
}
void CMotherboard::SetRAMWord4(uint32_t offset, uint16_t word)
{
    uint16_t* p = (uint16_t*)(m_pRAM + offset);
    uint16_t mask =
        ((word & 0x000F) == 0 ? 0 : 0x000F) | ((word & 0x00F0) == 0 ? 0 : 0x00F0) |
        ((word & 0x0F00) == 0 ? 0 : 0x0F00) | ((word & 0xF000) == 0 ? 0 : 0xF000);
    *p = (word & mask) | (*p & ~mask);
}
void CMotherboard::SetRAMByte2(uint32_t offset, uint8_t byte)
{
    uint8_t mask =
        ((byte & 0x03) == 0 ? 0 : 0x03) | ((byte & 0x0C) == 0 ? 0 : 0x0C) |
        ((byte & 0x30) == 0 ? 0 : 0x30) | ((byte & 0xC0) == 0 ? 0 : 0xC0);
    m_pRAM[offset] = (byte & mask) | (m_pRAM[offset] & ~mask);
}
void CMotherboard::SetRAMByte4(uint32_t offset, uint8_t byte)
{
    uint8_t mask = ((byte & 0x0F) == 0 ? 0 : 0x0F) | ((byte & 0xF0) == 0 ? 0 : 0xF0);
    m_pRAM[offset] = (byte & mask) | (m_pRAM[offset] & ~mask);
}

uint16_t CMotherboard::GetROMWord(uint16_t offset) const
{
    ASSERT(offset < 1024 * 16);
    return *((uint16_t*)(m_pROM + offset));
}
uint8_t CMotherboard::GetROMByte(uint16_t offset) const
{
    ASSERT(offset < 1024 * 16);
    return m_pROM[offset];
}


//////////////////////////////////////////////////////////////////////


void CMotherboard::ResetDevices()
{
    DebugLogFormat(_T("%c%06ho\tRESET\n"), HU_INSTRUCTION_PC);

    m_pFloppyCtl->Reset();

    if (m_pHardDrive != nullptr)
        m_pHardDrive->Reset();

    // Reset PIC 8259A
    //m_PICRR = m_PICMR = 0;
    //m_PICflags = PIC_MODE_ICW1;  // Waiting for ICW1
    SetPICInterrupt(0);  // Сигнал INIT или команда RESET приводит к прерыванию 0
    UpdateInterrupts();

    // Reset timer
    //TODO

    m_rtcticks = 0;
}

void CMotherboard::Tick50()  // 64 Hz / 50 Hz timer
{
    //NOTE: На разных платах на INT5 ВН59 идет либо сигнал от RTC 64 Гц либо кадровая синхронизация 50 Гц
    SetPICInterrupt(5);  // Сигнал 50 Гц приводит к прерыванию 5
    UpdateInterrupts();
}

void CMotherboard::TimerTick() // Timer Tick - 2 MHz
{
    m_snd.SetGate(0, true);
    m_snd.SetGate(1, true);
    m_snd.SetGate(2, true);
    m_snd.Tick();

    m_snl.SetGate(0, m_snd.GetOutput(0));
    m_snl.SetGate(1, m_snd.GetOutput(1));
    m_snl.SetGate(2, m_snd.GetOutput(2));
    m_snl.Tick();
}
// address = 0161010..0161026
void CMotherboard::ProcessTimerWrite(uint16_t address, uint8_t byte)
{
    PIT8253& pit = (address & 020) != 0 ? m_snl : m_snd;
    pit.Write((address >> 1) & 3, byte);
}
// address = 0161010..0161026
uint8_t CMotherboard::ProcessTimerRead(uint16_t address)
{
    PIT8253& pit = (address & 020) != 0 ? m_snl : m_snd;
    return pit.Read((address >> 1) & 3);
}

// Keyboard controller, Intel 8279
void CMotherboard::ProcessKeyboardWrite(uint8_t byte)
{
    switch (byte & 0xe0)
    {
    case 0x00:  // Mode set, ignored
    case 0x02:  // Clock set, ignored
        break;
    case 0x40:  // Read FIFO command
        m_keypos = 0;
        break;
    case 0xc0:  // Clear command
        m_keypos = 0;
        m_keyint = false;
        break;
    case 0xe0:  // End interrupt command
        m_keyint = false;
        break;
    }
}

void CMotherboard::UpdateKeyboardMatrix(const uint8_t matrix[8])
{
    bool hasChanges = false;
    for (int i = 0; i < 8; i++)
    {
        if (m_keymatrix[i] != matrix[i])
        {
            hasChanges = true;
            break;
        }
    }

    ::memcpy(m_keymatrix, matrix, sizeof(m_keymatrix));

    if (hasChanges && !m_keyint)
        m_keyint = true;
}

void CMotherboard::ProcessMouseWrite(uint8_t byte)
{
    m_PPIC = m_PPIC & 0x0f;
    if ((byte & 0x80) == 0)
        m_mousest = 0;
    else
        switch (m_mousest)
        {
        case 0:
            m_PPIC |= m_mousedx & 0xf0;
            m_mousest++;
            break;
        case 1:
            m_PPIC |= (m_mousedx << 4) & 0xf0;
            m_mousedx = 0;
            m_mousest++;
            break;
        case 2:
            m_PPIC |= m_mousedy & 0xf0;
            m_mousest++;
            break;
        case 3:
            m_PPIC |= (m_mousedy << 4) & 0xf0;
            m_mousest++;
            m_mousedy = 0;
            break;
        }
}

void CMotherboard::MouseMove(short dx, short dy, bool btnLeft, bool btnRight)
{
    m_mousedx = (signed char)(-dx);
    m_mousedy = (signed char)(-dy);

    m_PPIArd = (m_PPIArd & ~0xe0) | (btnLeft ? 0 : 0x20) | (btnRight ? 0 : 0x40);
}

void CMotherboard::DebugTicks()
{
    m_pCPU->ClearInternalTick();

#if !defined(PRODUCT)
    if (m_dwTrace & TRACE_CPU)
        TraceInstruction(m_pCPU, this, m_pCPU->GetPC() & ~1);
#endif

    m_pCPU->Execute();

    UpdateInterrupts();

    m_pFloppyCtl->Periodic();
}

/*
Каждый фрейм равен 1/25 секунды = 40 мс = 40000 тиков, 1 тик = 1 мкс.
В каждый фрейм происходит:
* 320000 тиков ЦП - 8 раз за тик - 8 МГц
* программируемый таймер - на каждый 4-й тик процессора - 2 МГц
* 2 тика 50 Гц
* 2.56 тика 64 Гц
* 625 тиков FDD - каждый 64-й тик (300 RPM = 5 оборотов в секунду)
* 882 тиков звука (для частоты 22050 Гц)
*/
bool CMotherboard::SystemFrame()
{
    const int soundSamplesPerFrame = SOUNDSAMPLERATE / 25;
    int soundBrasErr = 0;
    int snl0 = 0, snl1 = 0, snl2 = 0, soundTicks = 0, snd0 = 0, snd1 = 0, snd2 = 0;

    for (int frameticks = 0; frameticks < 40000; frameticks++)
    {
        for (int procticks = 0; procticks < 8; procticks++)  // CPU ticks
        {
#if !defined(PRODUCT)
            if ((m_dwTrace & TRACE_CPU) != 0 && m_pCPU->GetInternalTick() == 0)
                TraceInstruction(m_pCPU, this, m_pCPU->GetPC() & ~1);
#endif

            m_pCPU->Execute();

            UpdateInterrupts();

            if (m_CPUbps != nullptr)  // Check for breakpoints
            {
                uint32_t cpucurrent = (m_pCPU->GetPC() & 0xffff) | (m_pCPU->IsHaltMode() ? BREAKPOINT_HALT : 0);
                const uint32_t* pbps = m_CPUbps;
                while (*pbps != NOBREAKPOINT) { if (cpucurrent == *pbps++) return false; }
            }

            if ((procticks & 3) == 3)  // Every 4th tick
            {
                TimerTick();
                snd0 += m_snd.GetOutput(0) ? 1 : 0;
                snd1 += m_snd.GetOutput(1) ? 1 : 0;
                snd2 += m_snd.GetOutput(2) ? 1 : 0;
                snl0 += m_snl.GetOutput(0) ? 1 : 0;
                snl1 += m_snl.GetOutput(1) ? 1 : 0;
                snl2 += m_snl.GetOutput(2) ? 1 : 0;
                soundTicks++;
            }
        }

        //if (frameticks % 20000 == 10000)
        //    Tick50();  // 1/50 timer event

        m_rtcticks++;
        if (m_rtcticks >= 15625)  // 64 Hz RTC tick
        {
            m_rtcticks = 0;
            Tick50();
        }

        if (frameticks % 64 == 0)  // FDD tick
            m_pFloppyCtl->Periodic();

        if (m_pHardDrive != nullptr)
            m_pHardDrive->Periodic();

        soundBrasErr += soundSamplesPerFrame;
        if (2 * soundBrasErr >= 40000)
        {
            soundBrasErr -= 40000;
            //DebugLogFormat(_T("SoundSNL %02d  %2d %2d %2d  %2d %2d %2d\r\n"), soundTicks, snd0, snd1, snd2, snl0, snl1, snl2);
            uint16_t s0 = (uint16_t)((snd0 * 255 / soundTicks) * (snl0 * 255 / soundTicks));
            uint16_t s1 = (uint16_t)((snd1 * 255 / soundTicks) * (snl1 * 255 / soundTicks));
            uint16_t s2 = (uint16_t)((snd2 * 255 / soundTicks) * (snl1 * 255 / soundTicks));
            DoSound(s0, s1, s2);
            soundTicks = 0; snl0 = snl1 = snl2 = 0; snd0 = snd1 = snd2 = 0;
        }

        //if (m_ParallelOutCallback != nullptr)
        //{
        //    if ((m_PPIAwr & 2) != 0 && (m_PPIBrd & 0x40) != 0)
        //    {
        //        // Strobe set, Printer Ack set => reset Printer Ack
        //        m_PPIBrd &= ~0x40;
        //        // Now printer waits for a next byte
        //    }
        //    else if ((m_PPIAwr & 2) == 0 && (m_PPIBrd & 0x40) == 0)
        //    {
        //        // Strobe reset, Printer Ack reset => byte is ready, print it
        //        (*m_ParallelOutCallback)(m_PPIBwr);
        //        // Set Printer Acknowledge
        //        m_PPIBrd |= 0x40;
        //        // Now the printer waits for Strobe
        //    }
        //}
    }

    return true;
}


//////////////////////////////////////////////////////////////////////
// Motherboard: memory management

// Read word from memory for debugger
uint8_t CMotherboard::GetRAMByteView(uint32_t offset) const
{
    bool okBank = offset >= 2048 * 1024;  // false for BANK 0, true for BANK 1
    uint16_t bankbits = (okBank ? m_Configuration >> 6 : m_Configuration >> 4) & 3;  // 00 ничего, 01 256K планки, 10 1024К планки
    if (bankbits == 0)
        return 0;  // No RAM in this bank
    if (bankbits == 1)  // 2 x 256K
        offset &= ~0x180000;  // limit to 512K
    if (offset >= 4096 * 1024)
        return 0;
    return m_pRAM[offset];
}
uint16_t CMotherboard::GetRAMWordView(uint32_t offset) const
{
    bool okBank = offset >= 2048 * 1024;  // false for BANK 0, true for BANK 1
    uint16_t bankbits = (okBank ? m_Configuration >> 6 : m_Configuration >> 4) & 3;  // 00 ничего, 01 256K планки, 10 1024К планки
    if (bankbits == 0)
        return 0;  // No RAM in this bank
    if (bankbits == 1)  // 2 x 256K
        offset &= ~0x180000;  // limit to 512K
    if (offset >= 4096 * 1024)
        return 0;
    return *(uint16_t*)(m_pRAM + offset);
}
uint16_t CMotherboard::GetWordView(uint16_t address, bool okHaltMode, bool okExec, int* pAddrType) const
{
    uint32_t offset;
    int addrtype = TranslateAddress(address, okHaltMode, okExec, &offset);

    *pAddrType = addrtype;

    switch (addrtype)
    {
    case ADDRTYPE_RAM:
    case ADDRTYPE_RAM2:
    case ADDRTYPE_RAM4:
        return GetRAMWord(offset & ~1);
    case ADDRTYPE_ROM:
        return GetROMWord(offset & 0xfffe);
    case ADDRTYPE_EMUL:
        return GetRAMWord(offset & 07776);  // I/O port emulation
    case ADDRTYPE_IO:    // I/O port, not memory
    case ADDRTYPE_DENY:  // No RAM here
    case ADDRTYPE_NULL:  // This memory is inaccessible for reading
        return 0;
    }

    ASSERT(false);  // If we are here - then addrtype has invalid value
    return 0;
}
uint32_t CMotherboard::GetRAMFullAddress(uint16_t address, bool okHaltMode) const
{
    int memregno = address >> 13;
    uint16_t memreg = okHaltMode ? m_HR[memregno] : m_UR[memregno];
    if (memreg & 8)  // Запрет доступа к ОЗУ
        return 0xffffffff;
    return ((uint32_t)(address & 017777)) + (((uint32_t)(memreg & 037760)) << 8);
}

uint16_t CMotherboard::GetWord(uint16_t address, bool okHaltMode, bool okExec)
{
    uint32_t offset;
    int addrtype = TranslateAddress(address, okHaltMode, okExec, &offset);
    uint16_t res;

    switch (addrtype)
    {
    case ADDRTYPE_RAM:
    case ADDRTYPE_RAM2:
    case ADDRTYPE_RAM4:
        return GetRAMWord(offset & ~1);
    case ADDRTYPE_ROM:
        return GetROMWord(offset & 0xfffe);
    case ADDRTYPE_IO:
        //TODO: What to do if okExec == true ?
        return GetPortWord(address);
    case ADDRTYPE_EMUL:
        if ((m_PPIBrd & 1) == 1)  // EF0 inactive?
            m_HR[0] = address;
        else
            m_HR[1] = address;
        m_PPIBrd &= ~1;  // set EF0 active
        m_pCPU->SetHALTPin(true);
        res = GetRAMWord(offset & 07776);
        DebugLogFormat(_T("%c%06ho\tGETWORD %06ho EMUL -> %06ho\n"), HU_INSTRUCTION_PC, address, res);
        return res;
    case ADDRTYPE_NULL:
        return 0;
    case ADDRTYPE_DENY:
        DebugLogFormat(_T("%c%06ho\tGETWORD DENY %06ho\n"), HU_INSTRUCTION_PC, address);
        m_pCPU->MemoryError();
        return 0;
    }

    ASSERT(false);  // If we are here - then addrtype has invalid value
    return 0;
}

uint8_t CMotherboard::GetByte(uint16_t address, bool okHaltMode)
{
    uint32_t offset;
    int addrtype = TranslateAddress(address, okHaltMode, false, &offset);
    uint8_t resb;

    switch (addrtype)
    {
    case ADDRTYPE_RAM:
    case ADDRTYPE_RAM2:
    case ADDRTYPE_RAM4:
        return GetRAMByte(offset);
    case ADDRTYPE_ROM:
        return GetROMByte(offset & 0xffff);
    case ADDRTYPE_IO:
        //TODO: What to do if okExec == true ?
        return GetPortByte(address);
    case ADDRTYPE_EMUL:
        if ((m_PPIBrd & 1) == 1)  // EF0 inactive?
            m_HR[0] = address;
        else
            m_HR[1] = address;
        m_PPIBrd &= ~1;  // set EF0 active
        m_pCPU->SetHALTPin(true);
        resb = GetRAMByte(offset & 07777);
        DebugLogFormat(_T("%c%06ho\tGETBYTE %06ho EMUL %03ho\n"), HU_INSTRUCTION_PC, address, resb);
        return resb;
    case ADDRTYPE_NULL:
        return 0;
    case ADDRTYPE_DENY:
        DebugLogFormat(_T("%c%06ho\tGETBYTE DENY (%06ho)\n"), HU_INSTRUCTION_PC, address);
        m_pCPU->MemoryError();
        return 0;
    }

    ASSERT(false);  // If we are here - then addrtype has invalid value
    return 0;
}

void CMotherboard::SetWord(uint16_t address, bool okHaltMode, uint16_t word, bool isRMW)
{
    address &= ~1;

    uint32_t offset;
    int addrtype = TranslateAddress(address, okHaltMode, false, &offset);

    switch (addrtype)
    {
    case ADDRTYPE_RAM:
        SetRAMWord(offset, word);
        return;
    case ADDRTYPE_RAM2:
        SetRAMWord2(offset, word);
        return;
    case ADDRTYPE_RAM4:
        SetRAMWord4(offset, word);
        return;
    case ADDRTYPE_ROM:  // Writing to ROM
        //DebugLogFormat(_T("%c%06ho\tSETWORD ROM (%06ho)\n"), HU_INSTRUCTION_PC, address);
        //m_pCPU->MemoryError();
        return;
    case ADDRTYPE_IO:
        SetPortWord(address, word);
        return;
    case ADDRTYPE_EMUL:
        DebugLogFormat(_T("%c%06ho\tSETWORD %06ho -> (%06ho) EMUL\n"), HU_INSTRUCTION_PC, word, address);
        SetRAMWord(offset & 07777, word);
        if (!isRMW)
        {
            if ((m_PPIBrd & 1) == 1)  // EF0 inactive?
                m_HR[0] = address;
            else
                m_HR[1] = address;
        }
        m_PPIBrd &= ~3;  // set EF1,EF0 active
        m_pCPU->SetHALTPin(true);
        return;
    case ADDRTYPE_NULL:
        return;
    case ADDRTYPE_DENY:
        DebugLogFormat(_T("%c%06ho\tSETWORD DENY (%06ho)\n"), HU_INSTRUCTION_PC, address);
        m_pCPU->MemoryError();
        return;
    }

    ASSERT(false);  // If we are here - then addrtype has invalid value
}

void CMotherboard::SetByte(uint16_t address, bool okHaltMode, uint8_t byte, bool isRMW)
{
    uint32_t offset;
    int addrtype = TranslateAddress(address, okHaltMode, false, &offset);

    switch (addrtype)
    {
    case ADDRTYPE_RAM:
        SetRAMByte(offset, byte);
        return;
    case ADDRTYPE_RAM2:
        SetRAMByte2(offset, byte);
        return;
    case ADDRTYPE_RAM4:
        SetRAMByte4(offset, byte);
        return;
    case ADDRTYPE_ROM:  // Writing to ROM
        //DebugLogFormat(_T("%c%06ho\tSETBYTE ROM (%06ho)\n"), HU_INSTRUCTION_PC, address);
        //m_pCPU->MemoryError();
        return;
    case ADDRTYPE_IO:
        SetPortByte(address, byte);
        return;
    case ADDRTYPE_EMUL:
        DebugLogFormat(_T("%c%06ho\tSETBYTE %03o -> (%06ho) EMUL\n"), HU_INSTRUCTION_PC, byte, address);
        SetRAMByte(offset & 07777, byte);
        if (!isRMW)
        {
            if ((m_PPIBrd & 1) == 1)  // EF0 inactive?
                m_HR[0] = address;
            else
                m_HR[1] = address;
        }
        m_PPIBrd &= ~3;  // set EF1,EF0 active
        m_pCPU->SetHALTPin(true);
        return;
    case ADDRTYPE_NULL:
        return;
    case ADDRTYPE_DENY:
        DebugLogFormat(_T("%c%06ho\tSETBYTE DENY (%06ho)\n"), HU_INSTRUCTION_PC, address);
        m_pCPU->MemoryError();
        return;
    }

    ASSERT(false);  // If we are here - then addrtype has invalid value
}

int CMotherboard::TranslateAddress(uint16_t address, bool okHaltMode, bool /*okExec*/, uint32_t* pOffset) const
{
    if (okHaltMode && address < 040000)
    {
        *pOffset = address;
        return ADDRTYPE_ROM;
    }

    if (address >= 0160000)
    {
        if (address < 0170000)
        {
            *pOffset = address;
            return ADDRTYPE_IO;
        }

        // Область памяти эмулируемых регистров, только для режима USER
        if (!okHaltMode && address >= 0174000 && address < 0177700)
        {
            *pOffset = address & 0007777;
            return ADDRTYPE_EMUL;
        }

        if (!okHaltMode && address >= 0177700)
        {
            *pOffset = 0;
            return ADDRTYPE_DENY;
        }

        *pOffset = address & 0007777;
        return ADDRTYPE_RAM;
    }

    // Логика диспетчера памяти
    int memregno = address >> 13;
    uint16_t memreg = okHaltMode ? m_HR[memregno] : m_UR[memregno];
    if (memreg & 8)  // Запрет доступа к ОЗУ
    {
        *pOffset = 0;
        return ADDRTYPE_DENY;
    }
    uint32_t longaddr = ((uint32_t)(address & 017777)) + (((uint32_t)(memreg & 037760)) << 8);

    // RAM banks logic
    bool okBank = longaddr >= 2048 * 1024;  // false for BANK 0, true for BANK 1
    uint16_t bankbits = (okBank ? m_Configuration >> 6 : m_Configuration >> 4) & 3;  // 00 ничего, 01 256K планки, 10 1024К планки
    if (bankbits == 0)  // no RAM in this bank
    {
        *pOffset = 0;
        return ADDRTYPE_NULL;
    }
    if (bankbits == 1)
        longaddr &= ~0x180000;  // limit to 512K

    *pOffset = longaddr;
    uint16_t maskmode = memreg & 3;
    if (maskmode == 0)
        return ADDRTYPE_RAM;
    return (maskmode & 2) == 0 ? ADDRTYPE_RAM2 : ADDRTYPE_RAM4;
}

uint8_t CMotherboard::GetPortByte(uint16_t address)
{
    if (address & 1)
        return GetPortWord(address & 0xfffe) >> 8;

    return (uint8_t)GetPortWord(address);
}

uint16_t CMotherboard::GetPortWord(uint16_t address)
{
    uint16_t result;
    uint8_t resb;
    int chunk;

    switch (address)
    {
    case 0161000:  // PICCSR
        resb = ProcessPICRead(false);
        DebugLogFormat(_T("%c%06ho\tGETPORT PICCSR -> 0x%02hx\n"), HU_INSTRUCTION_PC, (uint16_t)resb);
        return resb;

    case 0161002:  // PICMR
        resb = ProcessPICRead(true);
        DebugLogFormat(_T("%c%06ho\tGETPORT PICMR -> 0x%02hx\n"), HU_INSTRUCTION_PC, (uint16_t)resb);
        return resb;

    case 0161010: case 0161012: case 0161014: case 0161016:
        resb = ProcessTimerRead(address);
        DebugLogFormat(_T("%c%06ho\tGETPORT %06ho SND -> 0x%02hx\n"), HU_INSTRUCTION_PC, address, (uint16_t)resb);
        return resb;
    case 0161020: case 0161022: case 0161024: case 0161026:
        resb = ProcessTimerRead(address);
        DebugLogFormat(_T("%c%06ho\tGETPORT %06ho SNL -> 0x%02hx\n"), HU_INSTRUCTION_PC, address, (uint16_t)resb);
        return resb;

    case 0161030:  // PPIA
        result = m_PPIArd;
        DebugLogFormat(_T("%c%06ho\tGETPORT %06ho PPIA -> %06ho\n"), HU_INSTRUCTION_PC, address, result);
        return result;

    case 0161032:  // PPIB
        result = m_PPIBrd;
        DebugLogFormat(_T("%c%06ho\tGETPORT %06ho PPIB -> %06ho\n"), HU_INSTRUCTION_PC, address, result);
        return result;

    case 0161034:  // PPIC
        DebugLogFormat(_T("%c%06ho\tGETPORT %06ho PPIC -> %06ho\n"), HU_INSTRUCTION_PC, address, m_PPIC);
        return m_PPIC;

    case 0161040:
        if (m_HDbuffdir)  // Buffer in write mode
            result = 0;
        else
        {
            result = m_pHDbuff[m_nHDbuff * 512 + m_nHDbuffpos % 512];
            m_nHDbuffpos++;
            if (m_nHDbuffpos >= 512)
            {
                m_nHDbuffpos = 0;
                m_nHDbuff = (m_nHDbuff + 1) & 3;
            }
        }
        DebugLogFormat(_T("%c%06ho\tGETPORT %06ho HD.BUFF -> 0x%02hx buf%d %03x %s\n"), HU_INSTRUCTION_PC, address, result, m_nHDbuff, m_nHDbuffpos, m_HDbuffdir ? _T("wr") : _T("rd"));
        return result;
    case 0161042:
        DebugLogFormat(_T("%c%06ho\tGETPORT %06ho HD.ERR\n"), HU_INSTRUCTION_PC, address);
        return 0xff;
    case 0161044:
        DebugLogFormat(_T("%c%06ho\tGETPORT %06ho HD.SCNT\n"), HU_INSTRUCTION_PC, address);
        return m_hdscnt;
    case 0161046:
        DebugLogFormat(_T("%c%06ho\tGETPORT %06ho HD.SNUM\n"), HU_INSTRUCTION_PC, address);
        return m_hdsnum;
    case 0161050:
        DebugLogFormat(_T("%c%06ho\tGETPORT %06ho HD.CNLO\n"), HU_INSTRUCTION_PC, address);
        return m_hdcnum & 0xff;
    case 0161052:
        DebugLogFormat(_T("%c%06ho\tGETPORT %06ho HD.CNHI\n"), HU_INSTRUCTION_PC, address);
        return m_hdcnum >> 8;
    case 0161054:  // HD.SDH
        DebugLogFormat(_T("%c%06ho\tGETPORT %06ho HD.SDH\n"), HU_INSTRUCTION_PC, address);
        m_HDbuffdir = true;  // Обращение к HD.SDH переводит буфер в режим записи
        return m_hdsdh;
    case 0161056:  // HD.CSR
        DebugLogFormat(_T("%c%06ho\tGETPORT %06ho HD.CSR\n"), HU_INSTRUCTION_PC, address);
        m_HDbuffdir = false;  // Обращение к HD.CSR переводит буфер в режим чтения
        m_hdint = false;
        return 0x41;

    case 0161060:  // DLBUF
        DebugLogFormat(_T("%c%06ho\tGETPORT %06ho DLBUF\n"), HU_INSTRUCTION_PC, address);
        return 0;
    case 0161062:  // DLCSR
        DebugLogFormat(_T("%c%06ho\tGETPORT %06ho DLCSR\n"), HU_INSTRUCTION_PC, address);
        return (m_SerialOutCallback == nullptr) ? 0 : 1;

    case 0161064:  // KBDCSR
        resb = m_keymatrix[m_keypos & 7];
        DebugLogFormat(_T("%c%06ho\tGETPORT %06ho KBDCSR -> 0x%02x pos%d\n"), HU_INSTRUCTION_PC, address, resb, m_keypos);
        m_keypos = (m_keypos + 1) & 7;
        return resb;
    case 0161066:  // KBDBUF
        DebugLogFormat(_T("%c%06ho\tGETPORT %06ho KBDBUF\n"), HU_INSTRUCTION_PC, address);
        return 0;

    case 0161070:  // FD.CSR
        resb = m_pFloppyCtl->GetState();
        DebugLogFormat(_T("%c%06ho\tGETPORT %06ho FD.CSR -> 0x%02hx\n"), HU_INSTRUCTION_PC, address, (uint16_t)resb);
        return resb;
    case 0161072:  // FD.BUF
        if ((m_hdsdh & 010) == 0)
            resb = m_pFloppyCtl->FifoRead();
        else
            resb = 0;
        DebugLogFormat(_T("%c%06ho\tGETPORT %06ho FD.BUF -> 0x%02hx\n"), HU_INSTRUCTION_PC, address, (uint16_t)resb);
        return resb;
    case 0161076:  // FD.CNT
        DebugLogFormat(_T("%c%06ho\tGETPORT %06ho FD.CNT\n"), HU_INSTRUCTION_PC, address);
        return 0;

    case 0161120: case 0161122: case 0161124: case 0161126: case 0161130: case 0161132: case 0161134: case 0161136:
        result = GetHardPortWord(address);
        //DebugLogFormat(_T("%c%06ho\tGETPORT %06ho IDE %03hx -> 0x%04hx\n"), HU_INSTRUCTION_PC, address, (uint16_t)((address >> 1) & 7) | 0x1f0, result);
        return result;

    case 0161200:
    case 0161202:
    case 0161204:
    case 0161206:
    case 0161210:
    case 0161212:
    case 0161214:
    case 0161216:
        chunk = (address >> 1) & 7;
        DebugLogFormat(_T("%c%06ho\tGETPORT %06ho HR%d -> %06ho\n"), HU_INSTRUCTION_PC, address, chunk, m_HR[chunk]);
        return m_HR[chunk];

    case 0161220:
    case 0161222:
    case 0161224:
    case 0161226:
    case 0161230:
    case 0161232:
    case 0161234:
    case 0161236:
        chunk = (address >> 1) & 7;
        DebugLogFormat(_T("%c%06ho\tGETPORT %06ho UR%d -> %06ho\n"), HU_INSTRUCTION_PC, address, chunk, m_UR[chunk]);
        return m_UR[chunk];

        // RTC ports
    case 0161400: case 0161401: case 0161402: case 0161403: case 0161404: case 0161405: case 0161406: case 0161407:
    case 0161410: case 0161411: case 0161412: case 0161413: case 0161414: case 0161415: case 0161416: case 0161417:
    case 0161420: case 0161421: case 0161422: case 0161423: case 0161424: case 0161425: case 0161426: case 0161427:
    case 0161430: case 0161431: case 0161432: case 0161433: case 0161434: case 0161435: case 0161436: case 0161437:
    case 0161440: case 0161441: case 0161442: case 0161443: case 0161444: case 0161445: case 0161446: case 0161447:
    case 0161450: case 0161451: case 0161452: case 0161453: case 0161454: case 0161455: case 0161456: case 0161457:
    case 0161460: case 0161461: case 0161462: case 0161463: case 0161464: case 0161465: case 0161466: case 0161467:
    case 0161470: case 0161471: case 0161472: case 0161473: case 0161474: case 0161475: case 0161476: case 0161477:
        result = ProcessRtcRead(address);
        DebugLogFormat(_T("%c%06ho\tGETPORT %06ho RTC -> %06ho\n"), HU_INSTRUCTION_PC, address, result);
        return result;

    default:
        DebugLogFormat(_T("%c%06ho\tGETPORT Unknown (%06ho)\n"), HU_INSTRUCTION_PC, address);
        // "Неиспользуемые" регистры в диапазоне 161000-161776 при запросе отдают младший байт адреса
        if (address >= 0161000 && address < 0162000)
            return address & 0x00ff;
        m_pCPU->MemoryError();
        return 0;
    }
}

// Read word from port for debugger
uint16_t CMotherboard::GetPortView(uint16_t address) const
{
    switch (address)
    {
    case 0161000:  // PICCSR, но мы здесь будем отдавать PICRR
        return m_PICRR;
    case 0161002:  // PICMR
        return m_PICMR;

    case 0161032:  // PPIB
        return m_PPIBrd;
    case 0161034:  // PPIC
        return m_PPIC;

    case 0161070:
        return m_pFloppyCtl->GetStateView();
        //case 0161072:
        //    return m_pFloppyCtl->GetDataView();

    case 0161200:
    case 0161202:
    case 0161204:
    case 0161206:
    case 0161210:
    case 0161212:
    case 0161214:
    case 0161216:
        {
            int chunk = (address >> 1) & 7;
            return m_HR[chunk];
        }

    case 0161220:
    case 0161222:
    case 0161224:
    case 0161226:
    case 0161230:
    case 0161232:
    case 0161234:
    case 0161236:
        {
            int chunk = (address >> 1) & 7;
            return m_UR[chunk];
        }

        // RTC ports
    case 0161400: case 0161401: case 0161402: case 0161403: case 0161404: case 0161405: case 0161406: case 0161407:
    case 0161410: case 0161411: case 0161412: case 0161413: case 0161414: case 0161415: case 0161416: case 0161417:
    case 0161420: case 0161421: case 0161422: case 0161423: case 0161424: case 0161425: case 0161426: case 0161427:
    case 0161430: case 0161431: case 0161432: case 0161433: case 0161434: case 0161435: case 0161436: case 0161437:
    case 0161440: case 0161441: case 0161442: case 0161443: case 0161444: case 0161445: case 0161446: case 0161447:
    case 0161450: case 0161451: case 0161452: case 0161453: case 0161454: case 0161455: case 0161456: case 0161457:
    case 0161460: case 0161461: case 0161462: case 0161463: case 0161464: case 0161465: case 0161466: case 0161467:
    case 0161470: case 0161471: case 0161472: case 0161473: case 0161474: case 0161475: case 0161476: case 0161477:
        return ProcessRtcRead(address);

    default:
        return 0;
    }
}

void CMotherboard::SetPortByte(uint16_t address, uint8_t byte)
{
    uint16_t word;
    if (address & 1)
    {
        word = GetPortWord(address & 0xfffe);
        word &= 0xff;
        word |= byte << 8;
        SetPortWord(address & 0xfffe, word);
    }
    else
    {
        word = GetPortWord(address);
        word &= 0xff00;
        SetPortWord(address, word | byte);
    }
}

void CMotherboard::SetPortWord(uint16_t address, uint16_t word)
{
#if !defined(PRODUCT)
    TCHAR buffer[17];
#endif

    switch (address)
    {
    case 0161000:  // PICCSR
        DebugLogFormat(_T("%c%06ho\tSETPORT %06ho -> (%06ho) PICCSR\n"), HU_INSTRUCTION_PC, word, address);
        ProcessPICWrite(false, word & 0xff);
        break;
    case 0161002:  // PICMR
        DebugLogFormat(_T("%c%06ho\tSETPORT 0x%04hx -> (%06ho) PICMR 0x%02hx PICRR=0x%02hx\n"), HU_INSTRUCTION_PC, word, address, word & 0xff, m_PICRR);
        ProcessPICWrite(true, word & 0xff);
        break;

    case 0161010:
        DebugLogFormat(_T("%c%06ho\tSETPORT %06ho -> (%06ho) SNDC0R\n"), HU_INSTRUCTION_PC, word, address);
        ProcessTimerWrite(address, word & 0xff);
        break;
    case 0161012:
        DebugLogFormat(_T("%c%06ho\tSETPORT %06ho -> (%06ho) SNDC1R\n"), HU_INSTRUCTION_PC, word, address);
        ProcessTimerWrite(address, word & 0xff);
        break;
    case 0161014:
        DebugLogFormat(_T("%c%06ho\tSETPORT %06ho -> (%06ho) SNDC2R\n"), HU_INSTRUCTION_PC, word, address);
        ProcessTimerWrite(address, word & 0xff);
        break;
    case 0161016:
        DebugLogFormat(_T("%c%06ho\tSETPORT %06ho -> (%06ho) SNDCSR\n"), HU_INSTRUCTION_PC, word, address);
        ProcessTimerWrite(address, word & 0xff);
        break;
    case 0161020:
        DebugLogFormat(_T("%c%06ho\tSETPORT %06ho -> (%06ho) SNLC0R\n"), HU_INSTRUCTION_PC, word, address);
        ProcessTimerWrite(address, word & 0xff);
        break;
    case 0161022:
        DebugLogFormat(_T("%c%06ho\tSETPORT %06ho -> (%06ho) SNLC1R\n"), HU_INSTRUCTION_PC, word, address);
        ProcessTimerWrite(address, word & 0xff);
        break;
    case 0161024:
        DebugLogFormat(_T("%c%06ho\tSETPORT %06ho -> (%06ho) SNLC2R\n"), HU_INSTRUCTION_PC, word, address);
        ProcessTimerWrite(address, word & 0xff);
        break;
    case 0161026:
        DebugLogFormat(_T("%c%06ho\tSETPORT %06ho -> (%06ho) SNLCSR\n"), HU_INSTRUCTION_PC, word, address);
        ProcessTimerWrite(address, word & 0xff);
        break;

    case 0161030:  // PPIA
#if !defined(PRODUCT)
        PrintBinaryValue(buffer, word);
        DebugLogFormat(_T("%c%06ho\tSETPORT %06ho -> (%06ho) PPIA %s\n"), HU_INSTRUCTION_PC, word, address, buffer + 12);
#endif
        m_PPIAwr = word & 0xff;
        ProcessMouseWrite(word & 0x00f0);
        break;
    case 0161032:  // PPIB
        DebugLogFormat(_T("%c%06ho\tSETPORT %06ho -> (%06ho) PPIB\n"), HU_INSTRUCTION_PC, word, address);
        m_PPIBwr = word & 0xff;
        break;
    case 0161034:  // PPIC
#if !defined(PRODUCT)
        PrintBinaryValue(buffer, word);
        DebugLogFormat(_T("%c%06ho\tSETPORT %06ho -> (%06ho) PPIC %s%s%s\n"), HU_INSTRUCTION_PC, word, address, buffer + 12,
                (word & 010) ? _T("") : _T(" VIRQ"),
                (word & 4) ? _T("") : _T(" IHLT"));
#endif
        m_PPIC = word & 0xff;
        m_PPIBrd = (m_PPIBrd & ~8) | ((m_PPIC & 4) == 0 ? 0 : 8);  // PC2(IHLT) -> PB3
        m_pCPU->SetVIRQ((m_PPIC & 010) == 0);
        break;
    case 0161036:  // PPIP -- Parallel port mode control
        DebugLogFormat(_T("%c%06ho\tSETPORT %06ho -> (%06ho) PPIP\n"), HU_INSTRUCTION_PC, word, address);
        break;

    case 0161040:  // HD.BUFF
        DebugLogFormat(_T("%c%06ho\tSETPORT %06ho -> (%06ho) HD.BUFF buf%d %03x %s\n"), HU_INSTRUCTION_PC, word, address, m_nHDbuff, m_nHDbuffpos, m_HDbuffdir ? _T("wr") : _T("rd"));
        if (m_HDbuffdir)  // Buffer in write mode
        {
            m_pHDbuff[m_nHDbuff * 512 + m_nHDbuffpos % 512] = word & 0xff;
            m_nHDbuffpos++;
            if (m_nHDbuffpos >= 512)
            {
                m_nHDbuffpos = 0;
                m_nHDbuff = (m_nHDbuff + 1) & 3;
            }
        }
        break;
    case 0161042:  // HD.ERR
        DebugLogFormat(_T("%c%06ho\tSETPORT %06ho -> (%06ho) HD.ERR\n"), HU_INSTRUCTION_PC, word, address);
        break;
    case 0161044:  // HD.SCNT
        DebugLogFormat(_T("%c%06ho\tSETPORT %06ho -> (%06ho) HD.SCNT\n"), HU_INSTRUCTION_PC, word, address);
        m_hdscnt = word & 0xff;
        break;
    case 0161046:
        DebugLogFormat(_T("%c%06ho\tSETPORT %06ho -> (%06ho) HD.SNUM\n"), HU_INSTRUCTION_PC, word, address);
        m_hdsnum = word & 0xff;
        break;
    case 0161050:
        DebugLogFormat(_T("%c%06ho\tSETPORT %06ho -> (%06ho) HD.CNLO\n"), HU_INSTRUCTION_PC, word, address);
        m_hdcnum = (m_hdcnum & 0xff00) | (word & 0xff);
        break;
    case 0161052:
        DebugLogFormat(_T("%c%06ho\tSETPORT %06ho -> (%06ho) HD.CNHI\n"), HU_INSTRUCTION_PC, word, address);
        m_hdcnum = (uint16_t)((m_hdcnum & 0x00ff) | ((word & 0xff) << 8));
        break;
    case 0161054:  // HD.SDH
        DebugLogFormat(_T("%c%06ho\tSETPORT %06ho -> (%06ho) HD.SDH\n"), HU_INSTRUCTION_PC, word, address);
        m_HDbuffdir = true;  // Обращение к HD.SDH переводит буфер в режим записи
        m_hdsdh = word;
        if ((m_hdsdh & 010) == 0)
            m_pFloppyCtl->SetParams(m_hdsdh & 1, (m_hdsdh >> 1) & 1, (m_hdsdh >> 2) & 1, (m_hdsdh >> 4) & 1);
        break;
    case 0161056:  // HD.CSR
        DebugLogFormat(_T("%c%06ho\tSETPORT %06ho -> (%06ho) HD.CSR\n"), HU_INSTRUCTION_PC, word, address);
        m_HDbuffdir = false;  // Обращение к HD.CSR переводит буфер в режим чтения
        //NOTE: Контроллер винчестера не реализован, но он должен отдать сигнал на прерывание в ответ на команду RESTORE
        if (word == 020)  // RESTORE
            m_hdint = true;
        break;

    case 0161060:  // DLBUF
        DebugLogFormat(_T("%c%06ho\tSETPORT %06ho -> (%06ho) DLBUF\n"), HU_INSTRUCTION_PC, word, address);
        if (m_SerialOutCallback != nullptr)
            (*m_SerialOutCallback)(word & 0xff);
        break;
    case 0161062:  // DLCSR
        DebugLogFormat(_T("%c%06ho\tSETPORT %06ho -> (%06ho) DLCSR\n"), HU_INSTRUCTION_PC, word, address);
        break;

    case 0161066:  // KBDBUF -- Keyboard controller, Intel 8279
        DebugLogFormat(_T("%c%06ho\tSETPORT %06ho -> (%06ho) KBDBUF\n"), HU_INSTRUCTION_PC, word, address);
        ProcessKeyboardWrite(word & 0xff);
        break;

    case 0161070:  // FD.CSR
        DebugLogFormat(_T("%c%06ho\tSETPORT %06ho -> (%06ho) FD.CSR\n"), HU_INSTRUCTION_PC, word, address);
        break;
    case 0161072:  // FD.BUF
        DebugLogFormat(_T("%c%06ho\tSETPORT %06ho -> (%06ho) FD.BUF\n"), HU_INSTRUCTION_PC, word, address);
        if ((m_hdsdh & 010) == 0)
            m_pFloppyCtl->FifoWrite(word & 0xff);
        break;
    case 0161076:  // FD.CNT
        DebugLogFormat(_T("%c%06ho\tSETPORT %06ho -> (%06ho) FD.CNT\n"), HU_INSTRUCTION_PC, word, address);
        m_nHDbuff = (word & 3);
        m_nHDbuffpos = 0;
        if (word & 020) // reset floppy controller
            m_pFloppyCtl->Reset();
        break;

    case 0161120: case 0161122: case 0161124: case 0161126: case 0161130: case 0161132: case 0161134: case 0161136:
        //DebugLogFormat(_T("%c%06ho\tSETPORT %06ho %03hx -> (%06ho) IDE\n"), HU_INSTRUCTION_PC, word, (uint16_t)((address >> 1) & 7) | 0x1f0, address);
        SetHardPortWord(address, word);
        break;

    case 0161200: case 0161202: case 0161204: case 0161206:
    case 0161210: case 0161212: case 0161214: case 0161216:
        {
            DebugLogFormat(_T("%c%06ho\tSETPORT HR %06ho -> (%06ho)\n"), HU_INSTRUCTION_PC, word, address);
            if (!m_pCPU->IsHaltMode())
                m_pCPU->MemoryError();  // Запись HR в режиме USER запрещена
            int chunk = (address >> 1) & 7;
            m_HR[chunk] = word;
            if (m_pCPU->IsHaltMode() && (chunk == 0 || chunk == 1))  // Запись HR0 или HR1 в режиме HALT
                m_PPIBrd |= 3;  // Снимаем EF0 и EF1
            break;
        }

    case 0161220: case 0161222: case 0161224: case 0161226:
    case 0161230: case 0161232: case 0161234: case 0161236:
        {
            DebugLogFormat(_T("%c%06ho\tSETPORT UR %06ho -> (%06ho)\n"), HU_INSTRUCTION_PC, word, address);
            int chunk = (address >> 1) & 7;
            m_UR[chunk] = word;
            break;
        }

    case 0161400: case 0161401: case 0161402: case 0161403: case 0161404: case 0161405: case 0161406: case 0161407:
    case 0161410: case 0161411: case 0161412: case 0161413: case 0161414: case 0161415: case 0161416: case 0161417:
    case 0161420: case 0161421: case 0161422: case 0161423: case 0161424: case 0161425: case 0161426: case 0161427:
    case 0161430: case 0161431: case 0161432: case 0161433: case 0161434: case 0161435: case 0161436: case 0161437:
    case 0161440: case 0161441: case 0161442: case 0161443: case 0161444: case 0161445: case 0161446: case 0161447:
    case 0161450: case 0161451: case 0161452: case 0161453: case 0161454: case 0161455: case 0161456: case 0161457:
    case 0161460: case 0161461: case 0161462: case 0161463: case 0161464: case 0161465: case 0161466: case 0161467:
    case 0161470: case 0161471: case 0161472: case 0161473: case 0161474: case 0161475: case 0161476: case 0161477:
        DebugLogFormat(_T("%c%06ho\tSETPORT RTC %06ho -> (%06ho)\n"), HU_INSTRUCTION_PC, word, address);
        ProcessRtcWrite(address, word & 0xff);
        break;

    default:
        DebugLogFormat(_T("SETPORT Unknown %06ho = %06ho @ %c%06ho\n"), address, word, HU_INSTRUCTION_PC);
        if (address >= 0161000 && address < 0162000)
            break;
        m_pCPU->MemoryError();
        break;
    }
}

void CMotherboard::ProcessPICWrite(bool a, uint8_t byte)
{
    uint16_t mode = m_PICflags & PIC_MODE_MASK;
    if (!a)
    {
        if (mode == PIC_MODE_ICW1)
        {
            //NOTE: Мы знаем что для Союз-Неон ICW1 = 022
            m_PICflags = PIC_MODE_ICW2;  // Wait for ICW2
        }
        else if (mode == 0)  // READY - set command
        {
            if (byte == 014)
                m_PICflags |= PIC_CMD_POLL;
            else if (byte == 040)  // End of Interrupt command
            {
                m_PICRR = 0;
                UpdateInterrupts();
            }
            else
                DebugLogFormat(_T("PIC Unknown command %03ho\n"), byte);
        }
    }
    else
    {
        if (mode == PIC_MODE_ICW2)
        {
            //NOTE: Мы знаем что для Союз-Неон ICW2 = 000
            m_PICflags = 0;  // READY now
        }
        else if (mode == 0)  // READY - set mask
        {
            m_PICMR = byte;
            UpdateInterrupts();
        }
    }
}
uint8_t CMotherboard::ProcessPICRead(bool a)
{
    if (!a)
    {
        uint16_t mode = m_PICflags & PIC_MODE_MASK;
        if (mode != 0)  // not READY
            return 0;

        if (m_PICflags & PIC_CMD_POLL)
        {
            if (m_PICRR == 0)
                return 0;  // No interrupts
            for (uint8_t i = 0; i < 8; i++)
            {
                if (m_PICRR & (1 << i))
                    return 0x80 | i;
            }
        }

        return 0;
    }
    else
        return m_PICMR;
}

// Set interrupt on PIC
void CMotherboard::SetPICInterrupt(int signal, bool set)
{
    if (signal < 0 || signal > 7)
        return;
    uint16_t mode = m_PICflags & PIC_MODE_MASK;
    if (mode != 0)  // not READY
        return;

    int s = (1 << signal);
    if (set)
    {
        if ((m_PICRR & s) == 0)
        {
            m_PICRR |= s;
            DebugLogFormat(_T("%c%06ho\tSET PIC INT%d, PICRR 0x%02hx PICMR 0x%02hx\n"), HU_INSTRUCTION_PC, signal, m_PICRR, m_PICMR);
        }
    }
    else
    {
        if ((m_PICRR & s) != 0)
            m_PICRR &= ~s;
    }
}

void CMotherboard::UpdateInterrupts()
{
    SetPICInterrupt(1, m_pFloppyCtl->CheckInterrupt() || m_hdint);
    SetPICInterrupt(4, m_keyint);
    bool ioint = ((m_PICRR & ~m_PICMR) != 0);
    m_PPIBrd = (m_PPIBrd & ~4) | (ioint ? 4 : 0);  // Update PB2(IOINT) signal
    m_pCPU->SetHALTPin((m_PPIBrd & 11) != 11 || ioint);  // EF0 EF1, IHLT or IOINT
}

// Get port value for Real Time Clock - ports 0161400..0161476 - КР512ВИ1 == MC146818
uint8_t CMotherboard::ProcessRtcRead(uint16_t address) const
{
    address = address & 0377;

    if (address >= 14 && address < 64)
        return m_rtcmemory[address - 14];

    time_t tnow = time(0);
    struct tm* lnow = localtime(&tnow);

    switch (address)
    {
    case 0:  // Seconds 0..59
        return (uint8_t)lnow->tm_sec;
    case 1:  // Seconds alarm
        return m_rtcalarmsec;
    case 2:  // Minutes 0..59
        return (uint8_t)lnow->tm_min;
    case 3:  // Minutes alarm
        return m_rtcalarmmin;
    case 4:  // Hours 0..23
        return (uint8_t)lnow->tm_hour;
    case 5:  // Hours alarm
        return m_rtcalarmhour;
    case 6:  // Day of week 1..7 Su..Sa
        return (uint8_t)(lnow->tm_wday + 1);
    case 7:  // Day of month 1..31
        return (uint8_t)lnow->tm_mday;
    case 8:  // Month 1..12
        return (uint8_t)(lnow->tm_mon + 1);
    case 9:  // Year 0..99
        return (uint8_t)(lnow->tm_year % 100);
    default:
        return 0;
    }
}

void CMotherboard::ProcessRtcWrite(uint16_t address, uint8_t byte)
{
    address = address & 0377;

    if (address >= 14 && address < 64)
        m_rtcmemory[address - 14] = byte;
}


//////////////////////////////////////////////////////////////////////
// Emulator image
//  Offset Length
//       0     32 bytes  - Header
//      32    400 bytes  - Board status
//     432     80 bytes  - CPU status
//     512   2048 bytes  - HD buffers 2K
//    2560    512 bytes  - RESERVED
//    3072  16384 bytes  - ROM image 16K
//   19456   1024 bytes  - RESERVED
//   20480               - RAM image 4096 KB
//
//  Board status (400 bytes):
//      32      2 bytes  - configuration
//      34      4 bytes  - RAM size bytes
//      38      6 bytes  - PIC
//      44      4 bytes  - RESERVED
//      48     10 bytes  - PPI
//      58      8 bytes  - RESERVED
//      66     32 bytes  - HR[8]
//      98     32 bytes  - UR[8]
//     130      8 bytes  - RESERVED
//     138     16 bytes  - HDD controller
//     154      6 bytes  - RESERVED
//     160     12 bytes  - Keyboard
//     172      4 bytes  - RESERVED
//     176      6 bytes  - Mouse
//     182     10 bytes  - RESERVED
//     192     60 bytes  - PIT8253 x 2
//     252      4 bytes  - RESERVED
//     256     64 bytes  - Timer
//     320     80 bytes  - RESERVED
//
void CMotherboard::SaveToImage(uint8_t* pImage)
{
    // Board data
    uint16_t* pwImage = reinterpret_cast<uint16_t*>(pImage + 32);
    *pwImage++ = m_Configuration;
    memcpy(pwImage, &m_nRamSizeBytes, sizeof(m_nRamSizeBytes));  // 4 bytes
    pwImage += sizeof(m_nRamSizeBytes) / 2;
    *pwImage++ = m_PICflags;
    *pwImage++ = m_PICRR;
    *pwImage++ = m_PICMR;
    pwImage += 4 / 2;  // RESERVED
    *pwImage++ = m_PPIAwr;  *pwImage++ = m_PPIArd;
    *pwImage++ = m_PPIBwr;  *pwImage++ = m_PPIBrd;
    *pwImage++ = m_PPIC;
    pwImage += 8 / 2;  // RESERVED
    // HR / UR
    memcpy(pwImage, m_HR, sizeof(m_HR));  // 32 bytes
    pwImage += sizeof(m_HR) / 2;
    memcpy(pwImage, m_UR, sizeof(m_UR));  // 32 bytes
    pwImage += sizeof(m_UR) / 2;
    pwImage += 8 / 2;  // RESERVED
    // HDD controller
    *pwImage++ = m_hdsdh;
    *pwImage++ = m_hdscnt;
    *pwImage++ = m_hdsnum;
    *pwImage++ = m_hdcnum;
    *pwImage++ = m_hdint;
    *pwImage++ = m_nHDbuff;
    *pwImage++ = m_nHDbuffpos;
    *pwImage   = m_HDbuffdir;
    // Keyboard, mouse
    pwImage = reinterpret_cast<uint16_t*>(pImage + 160);
    memcpy(pwImage, m_keymatrix, sizeof(m_keymatrix));  // 8 bytes
    pwImage += 8 / 2;
    *pwImage++ = m_keypos;
    *pwImage++ = m_keyint;
    pwImage += 4 / 2;  // RESERVED
    *pwImage++ = m_mousedx;
    *pwImage++ = m_mousedy;
    *pwImage++ = m_mousest;
    pwImage += 10 / 2;  // RESERVED
    // PIT8253 x 2
    memcpy(pwImage, m_snl.m_chan, sizeof(m_snl.m_chan));  // 30 bytes
    pwImage += 30 / 2;
    memcpy(pwImage, m_snd.m_chan, sizeof(m_snd.m_chan));  // 30 bytes
    // Timer
    time_t tnow = time(0);
    struct tm* lnow = localtime(&tnow);
    uint8_t* pImageTimer = pImage + 256;
    *pImageTimer++ = (uint8_t)lnow->tm_sec;  // Seconds
    *pImageTimer++ = m_rtcalarmsec;
    *pImageTimer++ = (uint8_t)lnow->tm_min;  // Minutes
    *pImageTimer++ = m_rtcalarmmin;
    *pImageTimer++ = (uint8_t)lnow->tm_hour;  // Hours
    *pImageTimer++ = m_rtcalarmhour;
    *pImageTimer++ = (uint8_t)(lnow->tm_wday + 1);  // Day of week
    *pImageTimer++ = (uint8_t)lnow->tm_mday;  // Day of month
    *pImageTimer++ = (uint8_t)lnow->tm_mon;  // Month
    *pImageTimer++ = (uint8_t)(lnow->tm_year % 100);  // Year
    *pImageTimer++ = 0;  // RESERVED
    *pImageTimer++ = 0;  // RESERVED
    *(uint16_t*)pImageTimer = m_rtcticks;
    pImageTimer += 2;
    memcpy(pImageTimer, m_rtcmemory, sizeof(m_rtcmemory));  // 50 bytes

    // CPU status
    uint8_t* pImageCPU = pImage + 432;
    m_pCPU->SaveToImage(pImageCPU);
    // HD buffers 2K
    uint8_t* pImageBuffer2K = pImage + 512;
    memcpy(pImageBuffer2K, m_pHDbuff, 2048);
    // ROM
    uint8_t* pImageRom = pImage + 3072;
    memcpy(pImageRom, m_pROM, 16 * 1024);
    // RAM
    uint8_t* pImageRam = pImage + 20480;
    memcpy(pImageRam, m_pRAM, 4096 * 1024);
}
void CMotherboard::LoadFromImage(const uint8_t* pImage)
{
    // Board data
    const uint16_t* pwImage = reinterpret_cast<const uint16_t*>(pImage + 32);

    // If the new configuration has different memory size, re-allocate the memory
    m_Configuration = *pwImage++;
    uint32_t nRamSizeKbytes = m_Configuration & NEON_COPT_RAMSIZE_MASK;
    if (nRamSizeKbytes == 0)
        nRamSizeKbytes = 512;
    uint32_t newramsize = nRamSizeKbytes * 1024;
    //memcpy(&newramsize, pwImage, sizeof(newramsize));  // 4 bytes
    pwImage += sizeof(m_nRamSizeBytes) / 2;
    m_PICflags = *pwImage++;
    m_PICRR = (uint8_t) * pwImage++;
    m_PICMR = (uint8_t) * pwImage++;
    pwImage += 4 / 2;  // RESERVED
    m_PPIAwr = (uint8_t) * pwImage++;  m_PPIArd = (uint8_t) * pwImage++;
    m_PPIBwr = (uint8_t) * pwImage++;  m_PPIBrd = (uint8_t) * pwImage++;
    m_PPIC = *pwImage++;
    pwImage += 8 / 2;  // RESERVED
    // HR / UR
    memcpy(m_HR, pwImage, sizeof(m_HR));  // 32 bytes
    pwImage += sizeof(m_HR) / 2;
    memcpy(m_UR, pwImage, sizeof(m_UR));  // 32 bytes
    pwImage += 8 / 2;  // RESERVED
    // HDD controller
    m_hdsdh = *pwImage++;
    m_hdscnt = (uint8_t) * pwImage++;
    m_hdsnum = (uint8_t) * pwImage++;
    m_hdcnum = *pwImage++;
    m_hdint = *pwImage++ != 0;
    m_nHDbuff = (uint8_t) * pwImage++;
    m_nHDbuffpos = *pwImage++;
    m_HDbuffdir = *pwImage != 0;
    // Keyboard, mouse
    pwImage = reinterpret_cast<const uint16_t*>(pImage + 160);
    memcpy(m_keymatrix, pwImage, sizeof(m_keymatrix));  // 8 bytes
    pwImage += 8 / 2;
    m_keypos = *pwImage++;
    m_keyint = *pwImage++ != 0;
    pwImage += 4 / 2;  // RESERVED
    m_mousedx = (uint8_t) * pwImage++;
    m_mousedy = (uint8_t) * pwImage++;
    m_mousest = (uint8_t) * pwImage++;
    pwImage += 10 / 2;  // RESERVED
    // PIT8253 x 2
    memcpy(m_snl.m_chan, pwImage, sizeof(m_snl.m_chan));  // 30 bytes
    pwImage += 30 / 2;
    memcpy(m_snd.m_chan, pwImage, sizeof(m_snd.m_chan));  // 30 bytes
    // Timer
    const uint8_t* pImageTimer = pImage + 256;
    pImageTimer++;  // Seconds
    m_rtcalarmsec = *pImageTimer++;
    pImageTimer++;  // Minutes
    m_rtcalarmmin = *pImageTimer++;
    pImageTimer++;  // Hours
    m_rtcalarmhour = *pImageTimer++;
    pImageTimer++;  // Day of week
    pImageTimer++;  // Day of month
    pImageTimer++;  // Month
    pImageTimer++;  // Year
    pImageTimer += 2;  // RESERVED
    m_rtcticks = *(const uint16_t*)pImageTimer;
    pImageTimer += 2;
    memcpy(m_rtcmemory, pImageTimer, sizeof(m_rtcmemory));  // 50 bytes

    // CPU status
    const uint8_t* pImageCPU = pImage + 432;
    m_pCPU->LoadFromImage(pImageCPU);
    // HD buffers 2K
    const uint8_t* pImageBuffer2K = pImage + 512;
    memcpy(m_pHDbuff, pImageBuffer2K, 2048);
    // ROM
    const uint8_t* pImageRom = pImage + 3072;
    memcpy(m_pROM, pImageRom, 16 * 1024);
    // RAM
    const uint8_t* pImageRam = pImage + 20480;
    memcpy(m_pRAM, pImageRam, 4096 * 1024);
}


//////////////////////////////////////////////////////////////////////

void CMotherboard::DoSound(uint16_t s0, uint16_t s1, uint16_t s2)
{
    if (m_SoundGenCallback == nullptr)
        return;

    uint16_t sound = (uint16_t)(((uint32_t)s0 + (uint32_t)s1 + (uint32_t)s2) / 3);

    (*m_SoundGenCallback)(sound, sound);
}

void CMotherboard::SetSoundGenCallback(SOUNDGENCALLBACK callback)
{
    if (callback == nullptr)  // Reset callback
    {
        m_SoundGenCallback = nullptr;
    }
    else
    {
        m_SoundGenCallback = callback;
    }
}

void CMotherboard::SetSerialOutCallback(SERIALOUTCALLBACK outcallback)
{
    m_SerialOutCallback = outcallback;
}

void CMotherboard::SetParallelOutCallback(PARALLELOUTCALLBACK outcallback)
{
    if (outcallback == nullptr)  // Reset callback
    {
        m_PPIArd &= ~0x10;  // Reset Printer flag
        m_ParallelOutCallback = nullptr;
    }
    else
    {
        m_PPIArd |= 0x10;  // Set Printer flag
        m_ParallelOutCallback = outcallback;
    }
}


//////////////////////////////////////////////////////////////////////

#if !defined(PRODUCT)

void TraceInstruction(const CProcessor* pProc, const CMotherboard* pBoard, uint16_t address)
{
    bool okHaltMode = pProc->IsHaltMode();

    uint16_t memory[4];
    int addrtype;
    for (uint16_t i = 0; i < 4; i++)
        memory[i] = pBoard->GetWordView(address + i * 2, okHaltMode, true, &addrtype);

    TCHAR bufaddr[8];
    bufaddr[0] = okHaltMode ? 'H' : 'U';
    PrintOctalValue(bufaddr + 1, address);

    TCHAR instr[8];
    TCHAR args[32];
    DisassembleInstruction(memory, address, instr, args);

    DebugLogFormat(_T("%s\t%s\t%s\r\n"), bufaddr, instr, args);
}

#endif

//////////////////////////////////////////////////////////////////////
