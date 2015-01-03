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

void TraceInstruction(CProcessor* pProc, CMotherboard* pBoard, WORD address);

//NOTE: Const for now, variable in the future
const DWORD NEON_RAM_SIZE_BYTES = 512 * 1024;

//////////////////////////////////////////////////////////////////////

CMotherboard::CMotherboard ()
{
    // Create devices
    m_pCPU = new CProcessor(this);
    m_pFloppyCtl = NULL;

    m_okTraceCPU = FALSE;
    m_TapeReadCallback = NULL;
    m_TapeWriteCallback = NULL;
    m_nTapeSampleRate = 0;
    m_SoundGenCallback = NULL;
    m_TeletypeCallback = NULL;

    ::memset(m_HR, 0, sizeof(m_HR));
    ::memset(m_UR, 0, sizeof(m_UR));

    // Allocate memory for RAM and ROM
    m_pRAM = (BYTE*) ::malloc(NEON_RAM_SIZE_BYTES);  //::memset(m_pRAM, 0, 512 * 1024);
    m_pROM = (BYTE*) ::malloc(16 * 1024);  ::memset(m_pROM, 0, 16 * 1024);

    SetConfiguration(0);  // Default configuration

    Reset();
}

CMotherboard::~CMotherboard ()
{
    // Delete devices
    delete m_pCPU;
    if (m_pFloppyCtl != NULL)
        delete m_pFloppyCtl;

    // Free memory
    ::free(m_pRAM);
    ::free(m_pROM);
}

void CMotherboard::SetConfiguration(WORD conf)
{
    m_Configuration = conf;

    // Clean RAM/ROM
    ::memset(m_pRAM, 0, NEON_RAM_SIZE_BYTES);
    ::memset(m_pROM, 0, 16 * 1024);

    //// Pre-fill RAM with "uninitialized" values
    //WORD * pMemory = (WORD *) m_pRAM;
    //WORD val = 0;
    //BYTE flag = 0;
    //for (DWORD i = 0; i < 128 * 1024; i += 2, flag--)
    //{
    //    *pMemory = val;  pMemory++;
    //    if (flag == 192)
    //        flag = 0;
    //    else
    //        val = ~val;
    //}

    if (m_pFloppyCtl == NULL && (conf & BK_COPT_FDD) != 0)
    {
        m_pFloppyCtl = new CFloppyController();
    }
    if (m_pFloppyCtl != NULL && (conf & BK_COPT_FDD) == 0)
    {
        delete m_pFloppyCtl;  m_pFloppyCtl = NULL;
    }
}

void CMotherboard::Reset ()
{
    m_pCPU->SetDCLOPin(TRUE);
    m_pCPU->SetACLOPin(TRUE);

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
    m_Port177716tap = 0200;

    m_timer = 0177777;
    m_timerdivider = 0;
    m_timerreload = 011000;
    m_timerflags = 0177400;

    ResetDevices();

    m_pCPU->SetDCLOPin(FALSE);
    m_pCPU->SetACLOPin(FALSE);
}

// Load 8 KB ROM image from the buffer
//   bank - number of 8k ROM bank, 0..1
void CMotherboard::LoadROM(int bank, const BYTE* pBuffer)
{
    ASSERT(bank >= 0 && bank <= 1);
    ::memcpy(m_pROM + 8192 * bank, pBuffer, 8192);
}

void CMotherboard::LoadRAM(int startbank, const BYTE* pBuffer, int length)
{
    ASSERT(pBuffer != NULL);
    ASSERT(startbank >= 0 && startbank < 15);
    int address = 8192 * startbank;
    ASSERT(address + length <= 128 * 1024);
    ::memcpy(m_pRAM + address, pBuffer, length);
}


// Floppy ////////////////////////////////////////////////////////////

BOOL CMotherboard::IsFloppyImageAttached(int slot)
{
    ASSERT(slot >= 0 && slot < 4);
    if (m_pFloppyCtl == NULL)
        return FALSE;
    return m_pFloppyCtl->IsAttached(slot);
}

BOOL CMotherboard::IsFloppyReadOnly(int slot)
{
    ASSERT(slot >= 0 && slot < 4);
    if (m_pFloppyCtl == NULL)
        return FALSE;
    return m_pFloppyCtl->IsReadOnly(slot);
}

BOOL CMotherboard::AttachFloppyImage(int slot, LPCTSTR sFileName)
{
    ASSERT(slot >= 0 && slot < 4);
    if (m_pFloppyCtl == NULL)
        return FALSE;
    return m_pFloppyCtl->AttachImage(slot, sFileName);
}

void CMotherboard::DetachFloppyImage(int slot)
{
    ASSERT(slot >= 0 && slot < 4);
    if (m_pFloppyCtl == NULL)
        return;
    m_pFloppyCtl->DetachImage(slot);
}


// Работа с памятью //////////////////////////////////////////////////

WORD CMotherboard::GetRAMWord(DWORD offset) const
{
    return *((WORD*)(m_pRAM + offset));
}
WORD CMotherboard::GetRAMWord(WORD hioffset, WORD offset) const
{
    DWORD dwOffset = MAKELONG(offset, hioffset);
    return *((WORD*)(m_pRAM + dwOffset));
}
BYTE CMotherboard::GetRAMByte(DWORD offset) const
{
    return m_pRAM[offset];
}
BYTE CMotherboard::GetRAMByte(WORD hioffset, WORD offset) const
{
    DWORD dwOffset = MAKELONG(offset, hioffset);
    return m_pRAM[dwOffset];
}
void CMotherboard::SetRAMWord(DWORD offset, WORD word)
{
    *((WORD*)(m_pRAM + offset)) = word;
}
void CMotherboard::SetRAMWord(WORD hioffset, WORD offset, WORD word)
{
    DWORD dwOffset = MAKELONG(offset, hioffset);
    *((WORD*)(m_pRAM + dwOffset)) = word;
}
void CMotherboard::SetRAMByte(DWORD offset, BYTE byte)
{
    m_pRAM[offset] = byte;
}
void CMotherboard::SetRAMByte(WORD hioffset, WORD offset, BYTE byte)
{
    DWORD dwOffset = MAKELONG(offset, hioffset);
    m_pRAM[dwOffset] = byte;
}

WORD CMotherboard::GetROMWord(WORD offset) const
{
    ASSERT(offset < 1024 * 16);
    return *((WORD*)(m_pROM + offset));
}
BYTE CMotherboard::GetROMByte(WORD offset) const
{
    ASSERT(offset < 1024 * 16);
    return m_pROM[offset];
}


//////////////////////////////////////////////////////////////////////


void CMotherboard::ResetDevices()
{
    if (m_pFloppyCtl != NULL)
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
    //    m_pCPU->TickEVNT();
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

    BOOL flag = FALSE;
    switch ((m_timerflags >> 5) & 3)  // bits 5,6 -- prescaler
    {
    case 0:  // 32 мкс
        flag = TRUE;
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

void CMotherboard::SetTimerReload(WORD val)	 // Sets timer reload value, write to port 177706
{
    //DebugPrintFormat(_T("SetTimerReload %06o\r\n"), val);
    m_timerreload = val;
}
void CMotherboard::SetTimerState(WORD val) // Sets timer state, write to port 177712
{
    //DebugPrintFormat(_T("SetTimerState %06o\r\n"), val);
    m_timer = m_timerreload;

    m_timerflags = 0177400 | val;
}

void CMotherboard::DebugTicks()
{
    m_pCPU->SetInternalTick(0);
    m_pCPU->Execute();
    if (m_pFloppyCtl != NULL)
        m_pFloppyCtl->Periodic();
}


/*
Каждый фрейм равен 1/25 секунды = 40 мс = 20000 тиков, 1 тик = 2 мкс.
12 МГц = 1 / 12000000 = 0.83(3) мкс
В каждый фрейм происходит:
* 120000 тиков ЦП - 6 раз за тик (БК-0010, 12МГц / 4 = 3 МГц, 3.3(3) мкс), либо
* 160000 тиков ЦП - 8 раз за тик (БК-0011, 12МГц / 3 = 4 МГц, 2.5 мкс)
* программируемый таймер - на каждый 128-й тик процессора; 42.6(6) мкс либо 32 мкс
* 2 тика IRQ2 50 Гц, в 0-й и 10000-й тик фрейма
* 625 тиков FDD - каждый 32-й тик (300 RPM = 5 оборотов в секунду)
* 68571 тиков AY-3-891x: 1.714275 МГц (12МГц / 7 = 1.714 МГц, 5.83(3) мкс)
*/
BOOL CMotherboard::SystemFrame()
{
    int frameProcTicks = (m_Configuration & BK_COPT_BK0011) ? 8 : 6;
    const int audioticks = 20286 / (SOUNDSAMPLERATE / 25);
    const int teletypeTicks = 20000 / (9600 / 25);
    int floppyTicks = (m_Configuration & BK_COPT_BK0011) ? 38 : 44;
    int teletypeTxCount = 0;

    int frameTapeTicks = 0, tapeSamplesPerFrame = 0, tapeReadError = 0;
    if (m_TapeReadCallback != NULL || m_TapeWriteCallback != NULL)
    {
        tapeSamplesPerFrame = m_nTapeSampleRate / 25;
        frameTapeTicks = 20000 / tapeSamplesPerFrame;
    }

    int timerTicks = 0;

    for (int frameticks = 0; frameticks < 20000; frameticks++)
    {
        for (int procticks = 0; procticks < frameProcTicks; procticks++)  // CPU ticks
        {
            if (m_pCPU->GetPC() == m_CPUbp)
                return FALSE;  // Breakpoint
#if !defined(PRODUCT)
            if (m_okTraceCPU && m_pCPU->GetInternalTick() == 0)
                TraceInstruction(m_pCPU, this, m_pCPU->GetPC() & ~1);
#endif
            m_pCPU->Execute();

            timerTicks++;
            if (timerTicks >= 128)
            {
                TimerTick();  // System timer tick: 31250 Hz = 32uS (BK-0011), 23437.5 Hz = 42.67 uS (BK-0010)
                timerTicks = 0;
            }
        }

        if (frameticks % 10000 == 0)
        {
            Tick50();  // 1/50 timer event
        }

        if ((m_Configuration & BK_COPT_FDD) && (frameticks % floppyTicks == 0))  // FDD tick
        {
            if (m_pFloppyCtl != NULL)
                m_pFloppyCtl->Periodic();
        }

        if (frameticks % audioticks == 0)  // AUDIO tick
            DoSound();

        if ((m_TapeReadCallback != NULL || m_TapeWriteCallback != NULL) && frameticks % frameTapeTicks == 0)
        {
            int tapeSamples = 0;
            const int readsTotal = 20000 / frameTapeTicks;
            while (true)
            {
                tapeSamples++;
                tapeReadError += readsTotal;
                if (2 * tapeReadError >= tapeSamples)
                {
                    tapeReadError -= tapeSamplesPerFrame;
                    break;
                }
            }

            // Reading the tape
            if (m_TapeReadCallback != NULL)
            {
                BOOL tapeBit = (*m_TapeReadCallback)(tapeSamples);
                TapeInput(tapeBit);
            }
            else if (m_TapeWriteCallback != NULL)
            {
                unsigned int value = 0;
                switch (m_Port177716tap & 0140)
                {
                case 0000: value = 0;                  break;
                case 0040: value = 0xffffffff / 4;     break;
                case 0100: value = 0xffffffff / 4 * 3; break;
                case 0140: value = 0xffffffff;         break;
                }
                (*m_TapeWriteCallback)(value, tapeSamples);
            }
        }

        if (frameticks % teletypeTicks)
        {
            if (teletypeTxCount > 0)
            {
                teletypeTxCount--;
                if (teletypeTxCount == 0)  // Translation countdown finished - the byte translated
                {
                    if (m_TeletypeCallback != NULL)
                        (*m_TeletypeCallback)(m_Port177566 & 0xff);
                    m_Port177564 |= 0200;
                    if (m_Port177564 & 0100)
                    {
                        m_pCPU->InterruptVIRQ(1, 064);
                    }
                }
            }
            else if ((m_Port177564 & 0200) == 0)
            {
                teletypeTxCount = 8;  // Start translation countdown
            }
        }
    }

    return TRUE;
}

// Key pressed or released
void CMotherboard::KeyboardEvent(BYTE scancode, BOOL okPressed, BOOL okAr2)
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

void CMotherboard::TapeInput(BOOL inputBit)
{
    WORD tapeBitOld = (m_Port177716 & 040);
    WORD tapeBitNew = inputBit ? 0 : 040;
    if (tapeBitNew != tapeBitOld)
    {
        m_Port177716 = (m_Port177716 & ~040) | tapeBitNew;  // Write new tape bit
        m_Port177716 |= 4;  // Set "ready" flag
    }
}

void CMotherboard::SetPrinterInPort(BYTE data)
{
    m_PortDLBUFin = data;
}


//////////////////////////////////////////////////////////////////////
// Motherboard: memory management

// Read word from memory for debugger
WORD CMotherboard::GetWordView(WORD address, BOOL okHaltMode, BOOL okExec, int* pAddrType) const
{
	address &= ~1;

    DWORD offset;
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

    ASSERT(FALSE);  // If we are here - then addrtype has invalid value
    return 0;
}

WORD CMotherboard::GetWord(WORD address, BOOL okHaltMode, BOOL okExec)
{
	address &= ~1;

    DWORD offset;
    int addrtype = TranslateAddress(address, okHaltMode, okExec, &offset);

    switch (addrtype)
    {
    case ADDRTYPE_RAM:
        return GetRAMWord(offset);
    case ADDRTYPE_ROM:
        return GetROMWord(LOWORD(offset));
    case ADDRTYPE_IO:
        //TODO: What to do if okExec == TRUE ?
        return GetPortWord(address);
    case ADDRTYPE_EMUL:
        //m_pCPU->SetHALTPin(TRUE);
        if ((m_PortPPIB & 1) == 0)
        {
            m_PortPPIB |= 1; m_HR[0] = address;
        }
        else if ((m_PortPPIB & 2) == 0)
        {
            m_PortPPIB |= 2; m_HR[1] = address;
        }
        return GetRAMWord(offset & 07777);
    case ADDRTYPE_DENY:
        m_pCPU->MemoryError();
        return 0;
    }

    ASSERT(FALSE);  // If we are here - then addrtype has invalid value
    return 0;
}

BYTE CMotherboard::GetByte(WORD address, BOOL okHaltMode)
{
    DWORD offset;
    int addrtype = TranslateAddress(address, okHaltMode, FALSE, &offset);

    switch (addrtype)
    {
    case ADDRTYPE_RAM:
        return GetRAMByte(offset);
    case ADDRTYPE_ROM:
        return GetROMByte(LOWORD(offset));
    case ADDRTYPE_IO:
        //TODO: What to do if okExec == TRUE ?
        return GetPortByte(address);
    case ADDRTYPE_EMUL:
        //m_pCPU->SetHALTPin(TRUE);
        if ((m_PortPPIB & 1) == 0)
        {
            m_PortPPIB |= 1; m_HR[0] = address;
        }
        else if ((m_PortPPIB & 2) == 0)
        {
            m_PortPPIB |= 2; m_HR[1] = address;
        }
        return GetRAMByte(offset & 07777);
    case ADDRTYPE_DENY:
        m_pCPU->MemoryError();
        return 0;
    }

    ASSERT(FALSE);  // If we are here - then addrtype has invalid value
    return 0;
}

void CMotherboard::SetWord(WORD address, BOOL okHaltMode, WORD word)
{
	address &= ~1;

    DWORD offset;
    int addrtype = TranslateAddress(address, okHaltMode, FALSE, &offset);

    switch (addrtype)
    {
    case ADDRTYPE_RAM:
        SetRAMWord(offset, word);
        return;
    case ADDRTYPE_ROM:  // Writing to ROM: exception
        m_pCPU->MemoryError();
        return;
    case ADDRTYPE_IO:
        SetPortWord(address, word);
        return;
    case ADDRTYPE_EMUL:
        SetRAMWord(offset & 07777, word);
        //m_pCPU->SetHALTPin(TRUE);
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
        m_pCPU->MemoryError();
        return;
    }

    ASSERT(FALSE);  // If we are here - then addrtype has invalid value
}

void CMotherboard::SetByte(WORD address, BOOL okHaltMode, BYTE byte)
{
    DWORD offset;
    int addrtype = TranslateAddress(address, okHaltMode, FALSE, &offset);

    switch (addrtype)
    {
    case ADDRTYPE_RAM:
        SetRAMByte(offset, byte);
        return;
    case ADDRTYPE_ROM:  // Writing to ROM: exception
        m_pCPU->MemoryError();
        return;
    case ADDRTYPE_IO:
        SetPortByte(address, byte);
        return;
    case ADDRTYPE_EMUL:
        SetRAMByte(offset & 07777, byte);
        //m_pCPU->SetHALTPin(TRUE);
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
        m_pCPU->MemoryError();
        return;
    }

    ASSERT(FALSE);  // If we are here - then addrtype has invalid value
}

// Calculates video buffer start address, for screen drawing procedure
const BYTE* CMotherboard::GetVideoBuffer()
{
    return m_pRAM + 040000;  //STUB
}

int CMotherboard::TranslateAddress(WORD address, BOOL okHaltMode, BOOL okExec, DWORD* pOffset) const
{
    if (okHaltMode && address < 040000)
    {
        *pOffset = address;
        return ADDRTYPE_ROM;
    }

    if (address >= 0160000)
    {
        if (address < 170000)  // Port
        {
            *pOffset = address;
            return ADDRTYPE_IO;
        }

        *pOffset = address;
        return ADDRTYPE_EMUL;
    }

    // Логика диспетчера памяти
    int memregno = address >> 13;
    WORD memreg = okHaltMode ? m_HR[memregno] : m_UR[memregno];
    if (memreg & 8)  // Запрет доступа к ОЗУ
    {
        *pOffset = 0;
        return ADDRTYPE_DENY;
    }
	DWORD longaddr = ((DWORD)(address & 017777)) + (((DWORD)(memreg & 037760)) << 8);
	if (longaddr > NEON_RAM_SIZE_BYTES)
	{
        *pOffset = 0;
        return ADDRTYPE_DENY;
	}

    *pOffset = longaddr;
    return ADDRTYPE_RAM;
}

BYTE CMotherboard::GetPortByte(WORD address)
{
    if (address & 1)
        return GetPortWord(address & 0xfffe) >> 8;

    return (BYTE) GetPortWord(address);
}

WORD CMotherboard::GetPortWord(WORD address)
{
    switch (address)
    {
    case 0161032:  // PPIB
#if !defined(PRODUCT)
        DebugLogFormat(_T("%06o\tGETPORT PPIB = %06o\n"), m_pCPU->GetInstructionPC(), address, m_PortPPIB);
#endif
        return m_PortPPIB;

    case 0161060:
#if !defined(PRODUCT)
        DebugLogFormat(_T("%06o\tGETPORT DLBUF\n"), m_pCPU->GetInstructionPC(), address);
#endif
        //TODO: DLBUF -- Programmable parallel port
        return 0;
    case 0161062:
        //TODO: DLCSR -- Programmable parallel port
#if !defined(PRODUCT)
        DebugLogFormat(_T("%06o\tGETPORT DLCSR\n"), m_pCPU->GetInstructionPC(), address);
#endif
        return 0x0f;

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
#if !defined(PRODUCT)
            DebugLogFormat(_T("%06o\tGETPORT HR%d = %06o\n"), m_pCPU->GetInstructionPC(), chunk, m_HR[chunk]);
#endif
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
#if !defined(PRODUCT)
            DebugLogFormat(_T("%06o\tGETPORT UR%d = %06o\n"), m_pCPU->GetInstructionPC(), chunk, m_UR[chunk]);
#endif
            return m_UR[chunk];
        }

    default:
#if !defined(PRODUCT)
        DebugLogFormat(_T("%06o\tGETPORT Unknown (%06o)\n"), m_pCPU->GetInstructionPC(), address);
#endif
        m_pCPU->MemoryError();
        return 0;
    }

    return 0;
}

// Read word from port for debugger
WORD CMotherboard::GetPortView(WORD address) const
{
    //switch (address)
    //{

    //default:
    return 0;
    //}
}

void CMotherboard::SetPortByte(WORD address, BYTE byte)
{
    WORD word;
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
void CMotherboard::SetPortWord(WORD address, WORD word)
{
#if !defined(PRODUCT)
    DebugLogFormat(_T("%06o\tSETPORT %06o -> (%06o)\n"), m_pCPU->GetInstructionPC(), word, address);
#endif
    switch (address)
    {
    case 0161000:  // Unknown port
    case 0161002:  // Unknown port
		break;

	case 0161012:
        //TODO: SNDС1R -- Sound control
        break;
    case 0161014:
        //TODO: SNDС1R -- Sound control
        break;
    case 0161016:
        //TODO: SNDСSR -- Sound control
        break;
	case 0161026:
		//TODO: SNLСSR -- Sound control
		break;

    case 0161030:
        //TODO: PPIA -- Parallel port
        break;
    case 0161032:
        //TODO: PPIB -- Parallel port data
        break;
    case 0161034:
        //TODO: PPIC -- System register
        break;
    case 0161036:
        //TODO: PPIP -- Parallel port mode control
        break;

    case 0161060:
        //TODO: DLBUF -- Programmable Parallel port buffer
        break;
    case 0161062:
        //TODO: DLCSR -- Programmable Parallel port control
        break;

    case 0161066:
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
            int chunk = (address >> 1) & 7;
            m_UR[chunk] = word;
            break;
        }

	case 0161412:  // Unknown port
		break;

    default:
#if !defined(PRODUCT)
        DebugLogFormat(_T("SETPORT Unknown %06o = %06o @ %06o\n"), address, word, m_pCPU->GetInstructionPC());
#endif
        m_pCPU->MemoryError();
        break;
    }
}


//////////////////////////////////////////////////////////////////////

void CMotherboard::DoSound(void)
{
    if (m_SoundGenCallback == NULL)
        return;

    BOOL bSoundBit = (m_Port177716tap & 0100) != 0;

    if (bSoundBit)
        (*m_SoundGenCallback)(0x1fff, 0x1fff);
    else
        (*m_SoundGenCallback)(0x0000, 0x0000);
}

void CMotherboard::SetTapeReadCallback(TAPEREADCALLBACK callback, int sampleRate)
{
    if (callback == NULL)  // Reset callback
    {
        m_TapeReadCallback = NULL;
        m_nTapeSampleRate = 0;
    }
    else
    {
        m_TapeReadCallback = callback;
        m_nTapeSampleRate = sampleRate;
        m_TapeWriteCallback = NULL;
    }
}

void CMotherboard::SetTapeWriteCallback(TAPEWRITECALLBACK callback, int sampleRate)
{
    if (callback == NULL)  // Reset callback
    {
        m_TapeWriteCallback = NULL;
        m_nTapeSampleRate = 0;
    }
    else
    {
        m_TapeWriteCallback = callback;
        m_nTapeSampleRate = sampleRate;
        m_TapeReadCallback = NULL;
    }
}

void CMotherboard::SetSoundGenCallback(SOUNDGENCALLBACK callback)
{
    if (callback == NULL)  // Reset callback
    {
        m_SoundGenCallback = NULL;
    }
    else
    {
        m_SoundGenCallback = callback;
    }
}

void CMotherboard::SetTeletypeCallback(TELETYPECALLBACK callback)
{
    if (callback == NULL)  // Reset callback
    {
        m_TeletypeCallback = NULL;
    }
    else
    {
        m_TeletypeCallback = callback;
    }
}


//////////////////////////////////////////////////////////////////////

#if !defined(PRODUCT)

void TraceInstruction(CProcessor* pProc, CMotherboard* pBoard, WORD address)
{
    BOOL okHaltMode = pProc->IsHaltMode();

    WORD memory[4];
    int addrtype;
    for (int i = 0; i < 4; i++)
        memory[i] = pBoard->GetWordView(address + i * 2, okHaltMode, TRUE, &addrtype);

    //if (addrtype != ADDRTYPE_RAM)
    //    return;

    TCHAR bufaddr[7];
    PrintOctalValue(bufaddr, address);

    TCHAR instr[8];
    TCHAR args[32];
    DisassembleInstruction(memory, address, instr, args);
    TCHAR buffer[64];
    wsprintf(buffer, _T("%s\t%s\t%s\r\n"), bufaddr, instr, args);

    DebugLog(buffer);
}

#endif

//////////////////////////////////////////////////////////////////////
