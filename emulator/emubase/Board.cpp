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

void TraceInstruction(const CProcessor* pProc, CMotherboard* pBoard, uint16_t address);


#define HU_INSTRUCTION_PC (m_pCPU->IsHaltMode() ? _T('H') : _T('U')), m_pCPU->GetInstructionPC()


//////////////////////////////////////////////////////////////////////

CMotherboard::CMotherboard()
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

    m_PICRR = m_PICMR = 0;
    m_PICflags = PIC_MODE_ICW1;

    SetConfiguration(0);  // Default configuration

    Reset();
}

CMotherboard::~CMotherboard()
{
    // Delete devices
    delete m_pCPU;
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

void CMotherboard::Reset()
{
    m_pCPU->SetDCLOPin(true);
    m_pCPU->SetACLOPin(true);

    // Reset ports
    m_PortPPIB = 11;  // IHLT EF1 EF0 - инверсные
    m_Port177560 = m_Port177562 = 0;
    m_Port177564 = 0200;
    m_Port177566 = 0;
    m_PortKBDCSR = 0100;
    m_PortKBDBUF = 0;

    m_timeralarmsec = m_timeralarmmin = m_timeralarmhour = 0;

    ResetDevices();

    m_pCPU->SetDCLOPin(false);
    m_pCPU->SetACLOPin(false);
}

// Load 16 KB ROM image from the buffer
void CMotherboard::LoadROM(const uint8_t* pBuffer)
{
    ::memcpy(m_pROM, pBuffer, 16384);
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

void CMotherboard::SetRAMBank(int bank, const void* buffer)
{
    if (bank < 0 || bank > (int)(m_nRamSizeBytes / 8192))
        return;
    memcpy(m_pRAM + bank * 8192, buffer, 8192);
}


//////////////////////////////////////////////////////////////////////


void CMotherboard::ResetDevices()
{
    DebugLogFormat(_T("%c%06ho\tRESET\n"), HU_INSTRUCTION_PC);

    if (m_pFloppyCtl != nullptr)
        m_pFloppyCtl->Reset();

    // Reset ports
    m_Port177560 = m_Port177562 = 0;
    m_Port177564 = 0200;
    m_Port177566 = 0;

    // Reset PIC 8259A
    m_PICRR = 0;
    m_PICflags = PIC_MODE_ICW1;  // Waiting for ICW1
    SetPICInterrupt(0);  // Сигнал INIT или команда RESET приводит к прерыванию 0

    // Reset timer
    //TODO
}

void CMotherboard::Tick50()  // 50 Hz timer
{
    //NOTE: На разных платах на INT5 ВН59 идет либо сигнал от RTC 64 Гц либо кадровая синхронизация 50 Гц
    SetPICInterrupt(5);  // Сигнал 50 Гц приводит к прерыванию 0
}

void CMotherboard::ExecuteCPU()
{
    m_pCPU->Execute();
}

void CMotherboard::TimerTick() // Timer Tick, 31250 Hz = 32 мкс (BK-0011), 23437.5 Hz = 42.67 мкс (BK-0010)
{
    //TODO
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

            //if ((procticks & 3) == 3)
            //    TimerTick();
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
void CMotherboard::KeyboardEvent(uint8_t scancode, bool okPressed)
{
    //TODO

    SetPICInterrupt(4);  // На INT4 идет запрос от контроллера клавиатуры
}


//////////////////////////////////////////////////////////////////////
// Motherboard: memory management

// Read word from memory for debugger
uint8_t CMotherboard::GetRAMByteView(uint32_t address) const
{
    if (address >= m_nRamSizeBytes)
        return 0;
    return m_pRAM[address];
}
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
        DebugLogFormat(_T("%c%06ho\tGETWORD (%06ho) EMUL\n"), HU_INSTRUCTION_PC, address);
        m_pCPU->SetHALTPin(true);
        if ((m_PortPPIB & 1) != 0)  // EF0 = 1
        {
            m_PortPPIB &= ~9;  // IHLT = 0, EF0 = 0
            m_HR[0] = address;
        }
        return GetRAMWord(offset & 07777);
    case ADDRTYPE_DENY:
        DebugLogFormat(_T("%c%06ho\tGETWORD DENY (%06ho)\n"), HU_INSTRUCTION_PC, address);
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
        DebugLogFormat(_T("%c%06ho\tGETBYTE (%06ho) EMUL\n"), HU_INSTRUCTION_PC, address);
        m_pCPU->SetHALTPin(true);
        if ((m_PortPPIB & 1) != 0)  // EF0 = 1
        {
            m_PortPPIB &= ~9;  // IHLT = 0, EF0 = 0
            m_HR[0] = address;
        }
        return GetRAMByte(offset & 07777);
    case ADDRTYPE_DENY:
        DebugLogFormat(_T("%c%06ho\tGETBYTE DENY (%06ho)\n"), HU_INSTRUCTION_PC, address);
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
        //DebugLogFormat(_T("%c%06ho\tSETWORD ROM (%06ho)\n"), HU_INSTRUCTION_PC, address);
        //m_pCPU->MemoryError();
        return;
    case ADDRTYPE_IO:
        SetPortWord(address, word);
        return;
    case ADDRTYPE_EMUL:
        DebugLogFormat(_T("%c%06ho\tSETWORD %06ho -> (%06ho) EMUL\n"), HU_INSTRUCTION_PC, word, address);
        SetRAMWord(offset & 07777, word);
        m_pCPU->SetHALTPin(true);
        if ((m_PortPPIB & 3) != 0)  // EF1, EF0 = 1
        {
            m_PortPPIB &= ~11;  // IHLT = 0, EF1,EF0 = 0
            m_HR[1] = address;
        }
        return;
    case ADDRTYPE_DENY:
        DebugLogFormat(_T("%c%06ho\tSETWORD DENY (%06ho)\n"), HU_INSTRUCTION_PC, address);
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
        //DebugLogFormat(_T("%c%06ho\tSETBYTE ROM (%06ho)\n"), HU_INSTRUCTION_PC, address);
        //m_pCPU->MemoryError();
        return;
    case ADDRTYPE_IO:
        SetPortByte(address, byte);
        return;
    case ADDRTYPE_EMUL:
        DebugLogFormat(_T("%c%06ho\tSETBYTE %03o -> (%06ho) EMUL\n"), HU_INSTRUCTION_PC, byte, address);
        SetRAMByte(offset & 07777, byte);
        m_pCPU->SetHALTPin(true);
        if ((m_PortPPIB & 3) != 0)  // EF1, EF0 = 1
        {
            m_PortPPIB &= ~11;  // IHLT = 0, EF1,EF0 = 0
            m_HR[1] = address;
        }
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

    return (uint8_t)GetPortWord(address);
}

uint16_t CMotherboard::GetPortWord(uint16_t address)
{
    uint16_t result;

    switch (address)
    {
    case 0161000:  // PICCSR
        {
            uint8_t b = ProcessPICRead(false);
            DebugLogFormat(_T("%c%06ho\tGETPORT PICCSR -> 0x%02hx\n"), HU_INSTRUCTION_PC, (uint16_t)b);
            return b;
        }

    case 0161002:  // PICMR
        {
            uint8_t b = ProcessPICRead(true);
            DebugLogFormat(_T("%c%06ho\tGETPORT PICMR -> 0x%02hx\n"), HU_INSTRUCTION_PC, (uint16_t)b);
            return b;
        }

    case 0161014:
        DebugLogFormat(_T("%c%06ho\tGETPORT %06ho SNDC2R -> %06ho\n"), HU_INSTRUCTION_PC, address, 0);
        return 0;//TODO

    case 0161032:  // PPIB
        result = m_PortPPIB;
        DebugLogFormat(_T("%c%06ho\tGETPORT %06ho PPIB -> %06ho\n"), HU_INSTRUCTION_PC, address, result);
        return result;

    case 0161034:  // PPIC
        DebugLogFormat(_T("%c%06ho\tGETPORT %06ho PPIC -> %06ho\n"), HU_INSTRUCTION_PC, address, m_PortPPIC);
        return m_PortPPIC;

    case 0161040:
    case 0161042:
    case 0161044:
    case 0161046:
    case 0161050:
    case 0161052:
    case 0161054:
    case 0161056:
        DebugLogFormat(_T("%c%06ho\tGETPORT %06ho HD\n"), HU_INSTRUCTION_PC, address);
        return 0;

    case 0161060:
        DebugLogFormat(_T("%c%06ho\tGETPORT DLBUF\n"), HU_INSTRUCTION_PC, address);
        //TODO: DLBUF -- Programmable parallel port
        return 0;
    case 0161062:  // DLCSR
        //TODO: DLCSR -- Programmable parallel port
        DebugLogFormat(_T("%c%06ho\tGETPORT DLCSR\n"), HU_INSTRUCTION_PC, address);
        return 0;

    case 0161064:  // KBDCSR
        DebugLogFormat(_T("%c%06ho\tGETPORT KBDCSR\n"), HU_INSTRUCTION_PC, address);
        return 0;
    case 0161066:  // KBDBUF
        DebugLogFormat(_T("%c%06ho\tGETPORT KBDBUF\n"), HU_INSTRUCTION_PC, address);
        return 0;

    case 0161070:
    case 0161072:
    case 0161076:
        DebugLogFormat(_T("%c%06ho\tGETPORT FD\n"), HU_INSTRUCTION_PC, address);
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
            DebugLogFormat(_T("%c%06ho\tGETPORT %06ho HR%d -> %06ho\n"), HU_INSTRUCTION_PC, address, chunk, m_HR[chunk]);
            if (chunk == 0 || chunk == 1)  // Чтение HR0 или HR1
                m_PortPPIB |= 3;  // Снимаем EF0 и EF1
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
            DebugLogFormat(_T("%c%06ho\tGETPORT %06ho UR%d -> %06ho\n"), HU_INSTRUCTION_PC, address, chunk, m_UR[chunk]);
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
        return GetRtcPortValue(address);

    default:
        DebugLogFormat(_T("%c%06ho\tGETPORT Unknown (%06ho)\n"), HU_INSTRUCTION_PC, address);
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
    case 0161000:  // PICCSR, но мы здесь будем отдавать PICRR
        return m_PICRR;
    case 0161002:  // PICMR
        return m_PICMR;

    case 0161032:  // PPIB
        return m_PortPPIB;
    case 0161034:  // PPIC
        return m_PortPPIC;

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
        return GetRtcPortValue(address);

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
        DebugLogFormat(_T("%c%06ho\tSETPORT %06ho -> (%06ho) PICCSR\n"), HU_INSTRUCTION_PC, word, address);
        ProcessPICWrite(false, word & 0xff);
        break;
    case 0161002:  // PICMR
        DebugLogFormat(_T("%c%06ho\tSETPORT %06ho -> (%06ho) PICMR 0x%02hx PICRR=0x%02hx\n"), HU_INSTRUCTION_PC, word, address, word & 0xff, m_PICRR);
        ProcessPICWrite(true, word & 0xff);
        break;

    case 0161012:
        DebugLogFormat(_T("%c%06ho\tSETPORT %06ho -> (%06ho) SNDC0R\n"), HU_INSTRUCTION_PC, word, address);
        //TODO: SNDC0R -- Sound control
        break;
    case 0161014:
        DebugLogFormat(_T("%c%06ho\tSETPORT %06ho -> (%06ho) SNDC1R\n"), HU_INSTRUCTION_PC, word, address);
        //TODO: SNDC1R -- Sound control
        break;
    case 0161016:
        DebugLogFormat(_T("%c%06ho\tSETPORT %06ho -> (%06ho) SNDCSR\n"), HU_INSTRUCTION_PC, word, address);
        //TODO: SNDCSR -- Sound control
        break;
    case 0161026:
        DebugLogFormat(_T("%c%06ho\tSETPORT %06ho -> (%06ho) SNLCSR\n"), HU_INSTRUCTION_PC, word, address);
        //TODO: SNLCSR -- Sound control
        break;

    case 0161030:
        DebugLogFormat(_T("%c%06ho\tSETPORT %06ho -> (%06ho) PPIA\n"), HU_INSTRUCTION_PC, word, address);
        //TODO: PPIA -- Parallel port
        break;
    case 0161032:
        DebugLogFormat(_T("%c%06ho\tSETPORT %06ho -> (%06ho) PPIB\n"), HU_INSTRUCTION_PC, word, address);
        //TODO: PPIB -- Parallel port data
        break;
    case 0161034:  // PPIC
        PrintBinaryValue(buffer, word);
        DebugLogFormat(_T("%c%06ho\tSETPORT %06ho -> (%06ho) PPIC %s\n"), HU_INSTRUCTION_PC, word, address, buffer + 12);
        m_PortPPIC = word;
        break;
    case 0161036:
        DebugLogFormat(_T("%c%06ho\tSETPORT %06ho -> (%06ho) PPIP\n"), HU_INSTRUCTION_PC, word, address);
        //TODO: PPIP -- Parallel port mode control
        break;

    case 0161040:
    case 0161042:
    case 0161044:
    case 0161046:
    case 0161050:
    case 0161052:
    case 0161054:
    case 0161056:
        DebugLogFormat(_T("%c%06ho\tSETPORT %06ho -> (%06ho) HD\n"), HU_INSTRUCTION_PC, word, address);
        break;

    case 0161060:
        DebugLogFormat(_T("%c%06ho\tSETPORT %06ho -> (%06ho) DLBUF\n"), HU_INSTRUCTION_PC, word, address);
        if (m_SerialOutCallback != nullptr)
            (*m_SerialOutCallback)(word & 0xff);
        break;
    case 0161062:  // DLCSR
        DebugLogFormat(_T("%c%06ho\tSETPORT %06ho -> (%06ho) DLCSR\n"), HU_INSTRUCTION_PC, word, address);
        //TODO: DLCSR -- Programmable Parallel port control
        break;

    case 0161066:
        DebugLogFormat(_T("%c%06ho\tSETPORT %06ho -> (%06ho) KBDBUF\n"), HU_INSTRUCTION_PC, word, address);
        //TODO: KBDBUF -- Keyboard buffer
        break;

    case 0161070:
    case 0161072:
    case 0161076:
        DebugLogFormat(_T("%c%06ho\tSETPORT %06ho -> (%06ho) FD\n"), HU_INSTRUCTION_PC, word, address);
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
            DebugLogFormat(_T("%c%06ho\tSETPORT HR %06ho -> (%06ho)\n"), HU_INSTRUCTION_PC, word, address);
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
            DebugLogFormat(_T("%c%06ho\tSETPORT UR %06ho -> (%06ho)\n"), HU_INSTRUCTION_PC, word, address);
            int chunk = (address >> 1) & 7;
            m_UR[chunk] = word;
            break;
        }

    case 0161412:  // Unknown port
        DebugLogFormat(_T("%c%06ho\tSETPORT %06ho -> (%06ho)\n"), HU_INSTRUCTION_PC, word, address);
        break;

    default:
        DebugLogFormat(_T("SETPORT Unknown %06ho = %06ho @ %c%06ho\n"), address, word, HU_INSTRUCTION_PC);
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
                // Тут сбрасываем источники прерывания
                m_PortPPIB &= ~4;  // reset IOINT
                m_PortPPIB |= 8;  // reset IHLT
                //NOTE: HR0 и HR1 очищаются в конце обработчика прерывания HALT в BIOS, см. P16HLT.MAC
            }
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
void CMotherboard::SetPICInterrupt(int signal)
{
    if (signal < 0 || signal > 7)
        return;
    uint16_t mode = m_PICflags & PIC_MODE_MASK;
    if (mode != 0)  // not READY
        return;
    int s = (1 << signal);
    if ((m_PICRR & s) == 0)
    {
        m_PICRR |= s;
        m_PortPPIB |= 4;  // set IOINT
        m_pCPU->SetHALTPin(true);
    }
}

// Get port value for Real Time Clock - ports 0161400..0161476 - КР512ВИ1 == MC146818
uint8_t CMotherboard::GetRtcPortValue(uint16_t address) const
{
    address = address & 0377;

    if (address >= 14 && address < 64)
        return m_timermemory[address - 14];

    time_t tnow = time(0);
    struct tm* lnow = localtime(&tnow);

    switch (address & 0377)
    {
    case 0:  // Seconds 0..59
        return (uint8_t)lnow->tm_sec;
    case 1:  // Seconds alarm
        return m_timeralarmsec;
    case 2:  // Minutes 0..59
        return (uint8_t)lnow->tm_min;
    case 3:  // Minutes alarm
        return m_timeralarmmin;
    case 4:  // Hours 0..23
        return (uint8_t)lnow->tm_hour;
    case 5:  // Hours alarm
        return m_timeralarmhour;
    case 6:  // Day of week
        return (uint8_t)(lnow->tm_wday + 1);  // 1..7 - Вс..Сб
    case 7:  // Day of month 1..31
        return (uint8_t)lnow->tm_mday;
    case 8:  // Month 1..12
        return (uint8_t)lnow->tm_mon;
    case 9:  // Year 0..99
        return (uint8_t)(lnow->tm_year % 100);
    default:
        return 0;
    }
}


//////////////////////////////////////////////////////////////////////
// Emulator image
//  Offset Length
//       0     32 bytes  - Header
//      32    480 bytes  - Board status
//     512     32 bytes  - CPU status
//     544   1504 bytes  - RESERVED
//    2048  16384 bytes  - ROM image 16K
//   18432               - RAM image 512/1024/2048/4096 KB
//
//  Board status (480 bytes):
//      32      4 bytes  - RAM size bytes
//      36     28 bytes  - RESERVED
//      64     32 bytes  - HR[8]
//      96     32 bytes  - UR[8]
//     128     64 bytes  - Timer
//     192
//
void CMotherboard::SaveToImage(uint8_t* pImage)
{
    // Board data
    uint16_t* pwImage = reinterpret_cast<uint16_t*>(pImage + 32);
    *pwImage++ = m_Configuration;
    memcpy(pwImage, &m_nRamSizeBytes, sizeof(m_nRamSizeBytes));  // 4 bytes
    pwImage += sizeof(m_nRamSizeBytes) / 2;
    pwImage += 28 / 2;  // RESERVED
    //TODO: m_PortPPIB, m_PortPPIC
    memcpy(pwImage, m_HR, sizeof(m_HR));  // 32 bytes
    pwImage += sizeof(m_HR) / 2;
    memcpy(pwImage, m_UR, sizeof(m_UR));  // 32 bytes
    //pwImage += sizeof(m_UR) / 2;

    // Timer
    time_t tnow = time(0);
    struct tm* lnow = localtime(&tnow);
    uint8_t* pImageTimer = pImage + 128;
    *pImageTimer++ = (uint8_t)lnow->tm_sec;  // Seconds
    *pImageTimer++ = m_timeralarmsec;
    *pImageTimer++ = (uint8_t)lnow->tm_min;  // Minutes
    *pImageTimer++ = m_timeralarmmin;
    *pImageTimer++ = (uint8_t)lnow->tm_hour;  // Hours
    *pImageTimer++ = m_timeralarmhour;
    *pImageTimer++ = (uint8_t)(lnow->tm_wday + 1);  // Day of week
    *pImageTimer++ = (uint8_t)lnow->tm_mday;  // Day of month
    *pImageTimer++ = (uint8_t)lnow->tm_mon;  // Month
    *pImageTimer++ = (uint8_t)(lnow->tm_year % 100);  // Year
    *pImageTimer++ = 0;
    *pImageTimer++ = 0;
    *pImageTimer++ = 0;
    *pImageTimer++ = 0;
    memcpy(pImageTimer, m_timermemory, sizeof(m_timermemory));  // 50 bytes

    //TODO

    // CPU status
    uint8_t* pImageCPU = pImage + 256;
    m_pCPU->SaveToImage(pImageCPU);
    // ROM
    uint8_t* pImageRom = pImage + 2048;
    memcpy(pImageRom, m_pROM, 16 * 1024);
    // RAM
    uint8_t* pImageRam = pImage + 18432;
    memcpy(pImageRam, m_pRAM, m_nRamSizeBytes);
}
void CMotherboard::LoadFromImage(const uint8_t* pImage)
{
    // Board data
    const uint16_t* pwImage = reinterpret_cast<const uint16_t*>(pImage + 32);
    m_Configuration = *pwImage++;
    memcpy(&m_nRamSizeBytes, pwImage, sizeof(m_nRamSizeBytes));  // 4 bytes
    pwImage += sizeof(m_nRamSizeBytes) / 2;
    pwImage += 28 / 2;  // RESERVED
    memcpy(m_HR, pwImage, sizeof(m_HR));  // 32 bytes
    pwImage += sizeof(m_HR) / 2;
    memcpy(m_UR, pwImage, sizeof(m_UR));  // 32 bytes
    pwImage += sizeof(m_UR) / 2;
    //TODO

    // CPU status
    const uint8_t* pImageCPU = pImage + 256;
    m_pCPU->LoadFromImage(pImageCPU);

    // ROM
    const uint8_t* pImageRom = pImage + 2048;
    memcpy(m_pROM, pImageRom, 16 * 1024);
    // RAM
    const uint8_t* pImageRam = pImage + 18432;
    memcpy(m_pRAM, pImageRam, m_nRamSizeBytes);
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

void TraceInstruction(const CProcessor* pProc, CMotherboard* pBoard, uint16_t address)
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
    _sntprintf(buffer, sizeof(buffer) - 1, _T("%s\t%s\t%s\r\n"), bufaddr, instr, args);

    DebugLog(buffer);
}

#endif

//////////////////////////////////////////////////////////////////////
