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

void TraceInstruction(CProcessor* pProc, CMotherboard* pBoard, uint16_t address);

//////////////////////////////////////////////////////////////////////

CMotherboard::CMotherboard ()
{
    // Create devices
    m_pCPU = new CProcessor(this);
    m_pFloppyCtl = nullptr;

    m_dwTrace = 0;
    m_SoundGenCallback = nullptr;

    ::memset(m_HR, 0, sizeof(m_HR));
    ::memset(m_UR, 0, sizeof(m_UR));

    // Allocate memory for ROM
    m_nRamSizeBytes = 0;
    m_pRAM = nullptr;  // RAM allocation in SetConfiguration() method
    m_pROM = static_cast<uint8_t*>(::calloc(16 * 1024, 1));

    SetConfiguration(0);  // Default configuration

    Reset();
}

CMotherboard::~CMotherboard ()
{
    // Delete devices
    delete m_pCPU;
    if (m_pFloppyCtl != nullptr)
        delete m_pFloppyCtl;

    // Free memory
    ::free(m_pRAM);
    ::free(m_pROM);
}

void CMotherboard::SetConfiguration(uint16_t conf)
{
    m_Configuration = conf;

    // Allocate RAM; clean RAM/ROM
    if (m_pRAM != nullptr)
        ::free(m_pRAM);
    uint32_t nRamSizeKbytes = conf & NEON_COPT_RAMSIZE_MASK;
    if (nRamSizeKbytes == 0)
        nRamSizeKbytes = 512;
    m_nRamSizeBytes = nRamSizeKbytes * 1024;
    m_pRAM = static_cast<uint8_t*>(::calloc(m_nRamSizeBytes, 1));
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

    if (m_pFloppyCtl == nullptr && (conf & NEON_COPT_FDD) != 0)
    {
        m_pFloppyCtl = new CFloppyController();
    }
    if (m_pFloppyCtl != nullptr && (conf & NEON_COPT_FDD) == 0)
    {
        delete m_pFloppyCtl;  m_pFloppyCtl = nullptr;
    }
}

void CMotherboard::Reset ()
{
    m_pCPU->SetDCLOPin(true);
    m_pCPU->SetACLOPin(true);

    // Reset ports
    m_PortPPIB = 0;
    m_Port177560 = m_Port177562 = 0;
    m_Port177564 = 0200;
    m_Port177566 = 0;
    m_PortKBDCSR = 0100;
    m_PortKBDBUF = 0;
    m_PortDLBUFin = m_PortDLBUFout = 0;
    m_Port177716 = 0300;
    m_Port177716mem = 0000002;

    m_timer = 0177777;
    m_timerdivider = 0;
    m_timerreload = 011000;
    m_timerflags = 0177400;

    ResetDevices();

    m_pCPU->SetDCLOPin(false);
    m_pCPU->SetACLOPin(false);
}

// Load 8 KB ROM image from the buffer
//   bank - number of 8k ROM bank, 0..1
void CMotherboard::LoadROM(int bank, const uint8_t* pBuffer)
{
    ASSERT(bank >= 0 && bank <= 1);
    ::memcpy(m_pROM + 8192 * bank, pBuffer, 8192);
}

//void CMotherboard::LoadRAM(int startbank, const uint8_t* pBuffer, int length)
//{
//    ASSERT(pBuffer != nullptr);
//    ASSERT(startbank >= 0 && startbank < 15);
//    int address = 8192 * startbank;
//    ASSERT(address + length <= 128 * 1024);
//    ::memcpy(m_pRAM + address, pBuffer, length);
//}


// Floppy ////////////////////////////////////////////////////////////

bool CMotherboard::IsFloppyImageAttached(int slot)
{
    ASSERT(slot >= 0 && slot < 2);
    if (m_pFloppyCtl == nullptr)
        return false;
    return m_pFloppyCtl->IsAttached(slot);
}

bool CMotherboard::IsFloppyReadOnly(int slot)
{
    ASSERT(slot >= 0 && slot < 2);
    if (m_pFloppyCtl == nullptr)
        return false;
    return m_pFloppyCtl->IsReadOnly(slot);
}

bool CMotherboard::AttachFloppyImage(int slot, LPCTSTR sFileName)
{
    ASSERT(slot >= 0 && slot < 2);
    if (m_pFloppyCtl == nullptr)
        return false;
    return m_pFloppyCtl->AttachImage(slot, sFileName);
}

void CMotherboard::DetachFloppyImage(int slot)
{
    ASSERT(slot >= 0 && slot < 2);
    if (m_pFloppyCtl == nullptr)
        return;
    m_pFloppyCtl->DetachImage(slot);
}


// Работа с памятью //////////////////////////////////////////////////

uint16_t CMotherboard::GetRAMWord(uint32_t offset) const
{
    ASSERT(offset < m_nRamSizeBytes);
    return *((uint16_t*)(m_pRAM + offset));
}
uint8_t CMotherboard::GetRAMByte(uint32_t offset) const
{
    return m_pRAM[offset];
}
void CMotherboard::SetRAMWord(uint32_t offset, uint16_t word)
{
    *((uint16_t*)(m_pRAM + offset)) = word;
}
void CMotherboard::SetRAMByte(uint32_t offset, uint8_t byte)
{
    m_pRAM[offset] = byte;
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
    if (m_pFloppyCtl != nullptr)
        m_pFloppyCtl->Reset();

    // Reset ports
    m_Port177560 = m_Port177562 = 0;
    m_Port177564 = 0200;
    m_Port177566 = 0;

    // Reset timer
    m_timerflags = 0177400;
    m_timer = 0177777;
    m_timerreload = 011000;
}

void CMotherboard::Tick50()  // 50 Hz timer
{
    //if ((m_Port177662wr & 040000) == 0)
    //{
        m_pCPU->TickEVNT();
    //}
}

void CMotherboard::ExecuteCPU()
{
    m_pCPU->Execute();
}

void CMotherboard::TimerTick() // Timer Tick, 31250 Hz = 32 мкс (BK-0011), 23437.5 Hz = 42.67 мкс (BK-0010)
{
    if ((m_timerflags & 1) == 1)  // STOP, the timer stopped
    {
        m_timer = m_timerreload;
        return;
    }
    if ((m_timerflags & 16) == 0)  // Not RUN, the timer paused
        return;

    m_timerdivider++;

    bool flag = false;
    switch ((m_timerflags >> 5) & 3)  // bits 5,6 -- prescaler
    {
    case 0:  // 32 мкс
        flag = true;
        break;
    case 1:  // 32 * 16 = 512 мкс
        flag = (m_timerdivider >= 16);
        break;
    case 2: // 32 * 4 = 128 мкс
        flag = (m_timerdivider >= 4);
        break;
    case 3:  // 32 * 16 * 4 = 2048 мкс, 8129 тактов процессора
        flag = (m_timerdivider >= 64);
        break;
    }
    if (!flag)  // Nothing happened
        return;

    m_timerdivider = 0;
    m_timer--;
    if (m_timer == 0)
    {
        if ((m_timerflags & 2) == 0)  // If not WRAPAROUND
        {
            if ((m_timerflags & 8) != 0)  // If ONESHOT and not WRAPAROUND then reset RUN bit
                m_timerflags &= ~16;

            m_timer = m_timerreload;
        }

        if ((m_timerflags & 4) != 0)  // If EXPENABLE
            m_timerflags |= 128;  // Set EXPIRY bit
    }
}

void CMotherboard::SetTimerReload(uint16_t val)  // Sets timer reload value, write to port 177706
{
    //DebugPrintFormat(_T("SetTimerReload %06o\r\n"), val);
    m_timerreload = val;
}
void CMotherboard::SetTimerState(uint16_t val) // Sets timer state, write to port 177712
{
    //DebugPrintFormat(_T("SetTimerState %06o\r\n"), val);
    m_timer = m_timerreload;

    m_timerflags = 0177400 | val;
}

void CMotherboard::DebugTicks()
{
    m_pCPU->ClearInternalTick();
    m_pCPU->Execute();
    if (m_pFloppyCtl != nullptr)
        m_pFloppyCtl->Periodic();
}


/*
Каждый фрейм равен 1/25 секунды = 40 мс = 20000 тиков, 1 тик = 2 мкс.
12 МГц = 1 / 12000000 = 0.83(3) мкс
В каждый фрейм происходит:
* 320000 тиков ЦП - 16 раз за тик - 8 МГц
* программируемый таймер - на каждый 4-й тик процессора - 2 МГц
* 2 тика IRQ2 50 Гц, в 0-й и 10000-й тик фрейма
* 625 тиков FDD - каждый 32-й тик (300 RPM = 5 оборотов в секунду)
* 882 тиков звука (для частоты 22050 Гц)
*/
bool CMotherboard::SystemFrame()
{
    const int soundSamplesPerFrame = SOUNDSAMPLERATE / 25;
    int soundBrasErr = 0;

    for (int frameticks = 0; frameticks < 20000; frameticks++)
    {
        for (int procticks = 0; procticks < 16; procticks++)  // CPU ticks
        {
#if !defined(PRODUCT)
            if (m_dwTrace && m_pCPU->GetInternalTick() == 0)
                TraceInstruction(m_pCPU, this, m_pCPU->GetPC() & ~1);
#endif
            m_pCPU->Execute();
            if (m_CPUbps != nullptr)  // Check for breakpoints
            {
                const uint16_t* pbps = m_CPUbps;
                while (*pbps != 0177777) { if (m_pCPU->GetPC() == *pbps++) return false; }
            }

            if ((procticks & 3) == 3)
                TimerTick();
        }

        if (frameticks % 10000 == 0)
            Tick50();  // 1/50 timer event

        if ((m_Configuration & NEON_COPT_FDD) && (frameticks % 32 == 0))  // FDD tick
        {
            if (m_pFloppyCtl != nullptr)
                m_pFloppyCtl->Periodic();
        }

        soundBrasErr += soundSamplesPerFrame;
        if (2 * soundBrasErr >= 20000)
        {
            soundBrasErr -= 20000;
            DoSound();
        }
    }

    return true;
}

// Key pressed or released
void CMotherboard::KeyboardEvent(uint8_t scancode, bool okPressed, bool okAr2)
{
    //if (scancode == BK_KEY_STOP)
    //{
    //    if (okPressed)
    //    {
    //        m_pCPU->AssertHALT();
    //    }
    //    return;
    //}

    if (!okPressed)  // Key released
    {
        m_Port177716 |= 0100;  // Reset "Key pressed" flag in system register
        m_Port177716 |= 4;  // Set "ready" flag
        return;
    }

    m_Port177716 &= ~0100;  // Set "Key pressed" flag in system register
    m_Port177716 |= 4;  // Set "ready" flag

    if ((m_PortKBDCSR & 0200) == 0)
    {
        m_PortKBDBUF = scancode & 0177;
        m_PortKBDCSR |= 0200;  // "Key ready" flag in keyboard state register
        if ((m_PortKBDCSR & 0100) == 0100)  // Keyboard interrupt enabled
        {
            m_pCPU->InterruptVIRQ(1, (okAr2 ? 0274 : 060));
        }
    }
}

void CMotherboard::SetPrinterInPort(uint8_t data)
{
    m_PortDLBUFin = data;
}


//////////////////////////////////////////////////////////////////////
// Motherboard: memory management

// Read word from memory for debugger
uint16_t CMotherboard::GetRAMWordView(uint32_t address) const
{
    if (address >= m_nRamSizeBytes)
        return 0;
    return *((uint16_t*)(m_pRAM + address));
}
uint16_t CMotherboard::GetWordView(uint16_t address, bool okHaltMode, bool okExec, int* pAddrType) const
{
    address &= ~1;

    uint32_t offset;
    int addrtype = TranslateAddress(address, okHaltMode, okExec, &offset);

    *pAddrType = addrtype;

    switch (addrtype)
    {
    case ADDRTYPE_RAM:
        return GetRAMWord(offset);
    case ADDRTYPE_ROM:
        return GetROMWord(LOWORD(offset));
    case ADDRTYPE_IO:
        return 0;  // I/O port, not memory
    case ADDRTYPE_EMUL:
        return GetRAMWord(offset & 07777);  // I/O port emulation
    case ADDRTYPE_DENY:
        return 0;  // This memory is inaccessible for reading
    }

    ASSERT(false);  // If we are here - then addrtype has invalid value
    return 0;
}

uint16_t CMotherboard::GetWord(uint16_t address, bool okHaltMode, bool okExec)
{
    address &= ~1;

    uint32_t offset;
    int addrtype = TranslateAddress(address, okHaltMode, okExec, &offset);

    switch (addrtype)
    {
    case ADDRTYPE_RAM:
        return GetRAMWord(offset);
    case ADDRTYPE_ROM:
        return GetROMWord(LOWORD(offset));
    case ADDRTYPE_IO:
        //TODO: What to do if okExec == true ?
        return GetPortWord(address);
    case ADDRTYPE_EMUL:
        DebugLogFormat(_T("%06o\tGETWORD (%06o) EMUL\n"), m_pCPU->GetInstructionPC(), address);
        m_pCPU->SetHALTPin(true);
        if ((m_PortPPIB & 1) == 0)
        {
            m_PortPPIB |= 1; m_HR[0] = address & 07777;
        }
        else if ((m_PortPPIB & 2) == 0)
        {
            m_PortPPIB |= 2; m_HR[1] = address & 07777;
        }
        return GetRAMWord(offset & 07777);
    case ADDRTYPE_DENY:
        DebugLogFormat(_T("%06o\tGETWORD DENY (%06o)\n"), m_pCPU->GetInstructionPC(), address);
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

    switch (addrtype)
    {
    case ADDRTYPE_RAM:
        return GetRAMByte(offset);
    case ADDRTYPE_ROM:
        return GetROMByte(LOWORD(offset));
    case ADDRTYPE_IO:
        //TODO: What to do if okExec == true ?
        return GetPortByte(address);
    case ADDRTYPE_EMUL:
        DebugLogFormat(_T("%06o\tGETBYTE (%06o) EMUL\n"), m_pCPU->GetInstructionPC(), address);
        m_pCPU->SetHALTPin(true);
        if ((m_PortPPIB & 1) == 0)
        {
            m_PortPPIB |= 1; m_HR[0] = address & 07777;
        }
        else if ((m_PortPPIB & 2) == 0)
        {
            m_PortPPIB |= 2; m_HR[1] = address & 07777;
        }
        return GetRAMByte(offset & 07777);
    case ADDRTYPE_DENY:
        DebugLogFormat(_T("%06o\tGETBYTE DENY (%06o)\n"), m_pCPU->GetInstructionPC(), address);
        m_pCPU->MemoryError();
        return 0;
    }

    ASSERT(false);  // If we are here - then addrtype has invalid value
    return 0;
}

void CMotherboard::SetWord(uint16_t address, bool okHaltMode, uint16_t word)
{
    address &= ~1;

    uint32_t offset;
    int addrtype = TranslateAddress(address, okHaltMode, false, &offset);

    switch (addrtype)
    {
    case ADDRTYPE_RAM:
        SetRAMWord(offset, word);
        return;
    case ADDRTYPE_ROM:  // Writing to ROM
        //DebugLogFormat(_T("%06o\tSETWORD ROM (%06o)\n"), m_pCPU->GetInstructionPC(), address);
        //m_pCPU->MemoryError();
        return;
    case ADDRTYPE_IO:
        SetPortWord(address, word);
        return;
    case ADDRTYPE_EMUL:
        DebugLogFormat(_T("%06o\tSETWORD %06o -> (%06o) EMUL\n"), m_pCPU->GetInstructionPC(), word, address);
        SetRAMWord(offset & 07777, word);
        m_pCPU->SetHALTPin(true);
        if ((m_PortPPIB & 1) == 0)
        {
            m_PortPPIB |= 1; m_HR[0] = address & 07777;
        }
        else if ((m_PortPPIB & 2) == 0)
        {
            m_PortPPIB |= 2; m_HR[1] = address & 07777;
        }
        return;
    case ADDRTYPE_DENY:
        DebugLogFormat(_T("%06o\tSETWORD DENY (%06o)\n"), m_pCPU->GetInstructionPC(), address);
        m_pCPU->MemoryError();
        return;
    }

    ASSERT(false);  // If we are here - then addrtype has invalid value
}

void CMotherboard::SetByte(uint16_t address, bool okHaltMode, uint8_t byte)
{
    uint32_t offset;
    int addrtype = TranslateAddress(address, okHaltMode, false, &offset);

    switch (addrtype)
    {
    case ADDRTYPE_RAM:
        SetRAMByte(offset, byte);
        return;
    case ADDRTYPE_ROM:  // Writing to ROM
        //DebugLogFormat(_T("%06o\tSETBYTE ROM (%06o)\n"), m_pCPU->GetInstructionPC(), address);
        //m_pCPU->MemoryError();
        return;
    case ADDRTYPE_IO:
        SetPortByte(address, byte);
        return;
    case ADDRTYPE_EMUL:
        DebugLogFormat(_T("%06o\tSETBYTE %03o -> (%06o) EMUL\n"), m_pCPU->GetInstructionPC(), byte, address);
        SetRAMByte(offset & 07777, byte);
        m_pCPU->SetHALTPin(true);
        if ((m_PortPPIB & 1) == 0)
        {
            m_PortPPIB |= 1; m_HR[0] = address;
        }
        else if ((m_PortPPIB & 2) == 0)
        {
            m_PortPPIB |= 2; m_HR[1] = address;
        }
        return;
    case ADDRTYPE_DENY:
        DebugLogFormat(_T("%06o\tSETBYTE DENY (%06o)\n"), m_pCPU->GetInstructionPC(), address);
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
        if (address < 0174000 || address >= 0177700)  // Port
        {
            *pOffset = address;
            return ADDRTYPE_IO;
        }

        *pOffset = address;
        return ADDRTYPE_EMUL;
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
    if (longaddr >= m_nRamSizeBytes)
    {
        *pOffset = 0;
        return ADDRTYPE_DENY;
    }

    *pOffset = longaddr;
    //ASSERT(longaddr < m_nRamSizeBytes);
    return ADDRTYPE_RAM;
}

uint8_t CMotherboard::GetPortByte(uint16_t address)
{
    if (address & 1)
        return GetPortWord(address & 0xfffe) >> 8;

    return (uint8_t) GetPortWord(address);
}

uint16_t CMotherboard::GetPortWord(uint16_t address)
{
    uint16_t result;

    switch (address)
    {
    case 0161000:  // PICCSR
        DebugLogFormat(_T("%06o\tGETPORT PICCSR = %06o\n"), m_pCPU->GetInstructionPC(), 0);
        return 0;//TODO

    case 0161002:  // PICMR
        DebugLogFormat(_T("%06o\tGETPORT PICMR = %06o\n"), m_pCPU->GetInstructionPC(), 0);
        return 0;//TODO

    case 0161014:
        DebugLogFormat(_T("%06o\tGETPORT SNDС2R = %06o\n"), m_pCPU->GetInstructionPC(), 0);
        return 0;//TODO

    case 0161032:  // PPIB
        result = 0xfffc | m_PortPPIB;
        DebugLogFormat(_T("%06o\tGETPORT PPIB = %06o\n"), m_pCPU->GetInstructionPC(), result);
        return result;

    case 0161034:  // PPIC
        DebugLogFormat(_T("%06o\tGETPORT PPIC = %06o\n"), m_pCPU->GetInstructionPC(), m_PortPPIC);
        return m_PortPPIC;

    case 0161060:
        DebugLogFormat(_T("%06o\tGETPORT DLBUF\n"), m_pCPU->GetInstructionPC(), address);
        //TODO: DLBUF -- Programmable parallel port
        return 0;
    case 0161062:  // DLCSR
        //TODO: DLCSR -- Programmable parallel port
        DebugLogFormat(_T("%06o\tGETPORT DLCSR\n"), m_pCPU->GetInstructionPC(), address);
        return 0;

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
            DebugLogFormat(_T("%06o\tGETPORT HR%d = %06o\n"), m_pCPU->GetInstructionPC(), chunk, m_HR[chunk]);
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
            DebugLogFormat(_T("%06o\tGETPORT UR%d = %06o\n"), m_pCPU->GetInstructionPC(), chunk, m_UR[chunk]);
            return m_UR[chunk];
        }

    default:
        DebugLogFormat(_T("%06o\tGETPORT Unknown (%06o)\n"), m_pCPU->GetInstructionPC(), address);
        m_pCPU->MemoryError();
        return 0;
    }

    //return 0;
}

// Read word from port for debugger
uint16_t CMotherboard::GetPortView(uint16_t address) const
{
    switch (address)
    {
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

//void DebugPrintFormat(LPCTSTR pszFormat, ...);  //DEBUG
void CMotherboard::SetPortWord(uint16_t address, uint16_t word)
{
    TCHAR buffer[17];

    switch (address)
    {
    case 0161000:  // PICCSR
        DebugLogFormat(_T("%06o\tSETPORT %06o -> (%06o) PICCSR\n"), m_pCPU->GetInstructionPC(), word, address);
        break;
    case 0161002:  // PICMR
        DebugLogFormat(_T("%06o\tSETPORT %06o -> (%06o) PICMR\n"), m_pCPU->GetInstructionPC(), word, address);
        break;

    case 0161012:
        DebugLogFormat(_T("%06o\tSETPORT %06o -> (%06o) SNDC0R\n"), m_pCPU->GetInstructionPC(), word, address);
        //TODO: SNDC0R -- Sound control
        break;
    case 0161014:
        DebugLogFormat(_T("%06o\tSETPORT %06o -> (%06o) SNDC1R\n"), m_pCPU->GetInstructionPC(), word, address);
        //TODO: SNDC1R -- Sound control
        break;
    case 0161016:
        DebugLogFormat(_T("%06o\tSETPORT %06o -> (%06o) SNDCSR\n"), m_pCPU->GetInstructionPC(), word, address);
        //TODO: SNDCSR -- Sound control
        break;
    case 0161026:
        DebugLogFormat(_T("%06o\tSETPORT %06o -> (%06o) SNLCSR\n"), m_pCPU->GetInstructionPC(), word, address);
        //TODO: SNLCSR -- Sound control
        break;

    case 0161030:
        DebugLogFormat(_T("%06o\tSETPORT %06o -> (%06o) PPIA\n"), m_pCPU->GetInstructionPC(), word, address);
        //TODO: PPIA -- Parallel port
        break;
    case 0161032:
        DebugLogFormat(_T("%06o\tSETPORT %06o -> (%06o) PPIB\n"), m_pCPU->GetInstructionPC(), word, address);
        //TODO: PPIB -- Parallel port data
        break;
    case 0161034:  // PPIC
        PrintBinaryValue(buffer, word);
        DebugLogFormat(_T("%06o\tSETPORT %06o -> (%06o) PPIC %s\n"), m_pCPU->GetInstructionPC(), word, address, buffer + 12);
        m_PortPPIC = word;
        break;
    case 0161036:
        DebugLogFormat(_T("%06o\tSETPORT %06o -> (%06o) PPIP\n"), m_pCPU->GetInstructionPC(), word, address);
        //TODO: PPIP -- Parallel port mode control
        break;

    case 0161060:
        DebugLogFormat(_T("%06o\tSETPORT %06o -> (%06o) DLBUF\n"), m_pCPU->GetInstructionPC(), word, address);
        if (m_SerialOutCallback != nullptr)
            (*m_SerialOutCallback)(word & 0xff);
        break;
    case 0161062:  // DLCSR
        DebugLogFormat(_T("%06o\tSETPORT %06o -> (%06o) DLCSR\n"), m_pCPU->GetInstructionPC(), word, address);
        //TODO: DLCSR -- Programmable Parallel port control
        break;

    case 0161066:
        DebugLogFormat(_T("%06o\tSETPORT %06o -> (%06o) KBDBUF\n"), m_pCPU->GetInstructionPC(), word, address);
        //TODO: KBDBUF -- Keyboard buffer
        break;

    case 0161200:
    case 0161202:
    case 0161204:
    case 0161206:
    case 0161210:
    case 0161212:
    case 0161214:
    case 0161216:
        {
            DebugLogFormat(_T("%06o\tSETPORT HR %06o -> (%06o)\n"), m_pCPU->GetInstructionPC(), word, address);
            int chunk = (address >> 1) & 7;
            m_HR[chunk] = word;
            break;
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
            DebugLogFormat(_T("%06o\tSETPORT UR %06o -> (%06o)\n"), m_pCPU->GetInstructionPC(), word, address);
            int chunk = (address >> 1) & 7;
            m_UR[chunk] = word;
            break;
        }

    case 0161412:  // Unknown port
        DebugLogFormat(_T("%06o\tSETPORT %06o -> (%06o)\n"), m_pCPU->GetInstructionPC(), word, address);
        break;

    default:
        DebugLogFormat(_T("SETPORT Unknown %06o = %06o @ %06o\n"), address, word, m_pCPU->GetInstructionPC());
        m_pCPU->MemoryError();
        break;
    }
}


//////////////////////////////////////////////////////////////////////

void CMotherboard::DoSound(void)
{
    if (m_SoundGenCallback == nullptr)
        return;

    bool bSoundBit = 0;//TODO

    if (bSoundBit)
        (*m_SoundGenCallback)(0x1fff, 0x1fff);
    else
        (*m_SoundGenCallback)(0x0000, 0x0000);
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


//////////////////////////////////////////////////////////////////////

#if !defined(PRODUCT)

void TraceInstruction(CProcessor* pProc, CMotherboard* pBoard, uint16_t address)
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
    TCHAR buffer[64];
    wsprintf(buffer, _T("%s\t%s\t%s\r\n"), bufaddr, instr, args);

    DebugLog(buffer);
}

#endif

//////////////////////////////////////////////////////////////////////
