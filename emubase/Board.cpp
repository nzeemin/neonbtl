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

    // Allocate memory for RAM and ROM
    m_pRAM = (BYTE*) ::malloc(128 * 1024);  //::memset(m_pRAM, 0, 128 * 1024);
    m_pROM = (BYTE*) ::malloc(64 * 1024);  ::memset(m_pROM, 0, 64 * 1024);

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
    //m_Configuration = conf;

    // Define memory map
    m_MemoryMap = 0xf0;  // By default, 000000-077777 is RAM, 100000-177777 is ROM
    m_MemoryMapOnOff = 0xff;  // By default, all 8K blocks are valid
    if (m_Configuration & BK_COPT_FDD)  // FDD with 16KB extra memory
        m_MemoryMap = 0xf0 - 32 - 64;  // 16KB extra memory mapped to 120000-157777
    if ((m_Configuration & (BK_COPT_BK0010 | BK_COPT_ROM_FOCAL)) == (BK_COPT_BK0010 | BK_COPT_ROM_FOCAL))
        m_MemoryMapOnOff = 0xbf;

    // Clean RAM/ROM
    ::memset(m_pRAM, 0, 128 * 1024);
    ::memset(m_pROM, 0, 64 * 1024);

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
    m_Port177560 = m_Port177562 = 0;
    m_Port177564 = 0200;
    m_Port177566 = 0;
    m_Port177660 = 0100;
    m_Port177662rd = 0;
    m_Port177662wr = 047400;
    m_Port177664 = 01330;
    m_Port177714in = m_Port177714out = 0;
    m_Port177716 = ((m_Configuration & BK_COPT_BK0011) ? 0140000 : 0100000) | 0300;
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
//   bank - number of 8k ROM bank, 0..7
void CMotherboard::LoadROM(int bank, const BYTE* pBuffer)
{
    ASSERT(bank >= 0 && bank <= 7);
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

WORD CMotherboard::GetRAMWord(WORD offset) const
{
    return *((WORD*)(m_pRAM + offset));
}
WORD CMotherboard::GetRAMWord(BYTE chunk, WORD offset) const
{
    DWORD dwOffset = (((DWORD)chunk & 7) << 14) + offset;
    return *((WORD*)(m_pRAM + dwOffset));
}
BYTE CMotherboard::GetRAMByte(WORD offset) const
{
    return m_pRAM[offset];
}
BYTE CMotherboard::GetRAMByte(BYTE chunk, WORD offset) const
{
    DWORD dwOffset = (((DWORD)chunk & 7) << 14) + offset;
    return m_pRAM[dwOffset];
}
void CMotherboard::SetRAMWord(WORD offset, WORD word)
{
    *((WORD*)(m_pRAM + offset)) = word;
}
void CMotherboard::SetRAMWord(BYTE chunk, WORD offset, WORD word)
{
    DWORD dwOffset = (((DWORD)chunk & 7) << 14) + offset;
    *((WORD*)(m_pRAM + dwOffset)) = word;
}
void CMotherboard::SetRAMByte(WORD offset, BYTE byte)
{
    m_pRAM[offset] = byte;
}
void CMotherboard::SetRAMByte(BYTE chunk, WORD offset, BYTE byte)
{
    DWORD dwOffset = (((DWORD)chunk & 7) << 14) + offset;
    m_pRAM[dwOffset] = byte;
}

WORD CMotherboard::GetROMWord(WORD offset) const
{
    ASSERT(offset < 1024 * 64);
    return *((WORD*)(m_pROM + offset));
}
BYTE CMotherboard::GetROMByte(WORD offset) const
{
    ASSERT(offset < 1024 * 64);
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
    if ((m_Port177662wr & 040000) == 0)
    {
        m_pCPU->TickEVNT();
    }
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

    if ((m_Port177660 & 0200) == 0)
    {
        m_Port177662rd = scancode & 0177;
        m_Port177660 |= 0200;  // "Key ready" flag in keyboard state register
        if ((m_Port177660 & 0100) == 0100)  // Keyboard interrupt enabled
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
    m_Port177714in = data;
}


//////////////////////////////////////////////////////////////////////
// Motherboard: memory management

// Read word from memory for debugger
WORD CMotherboard::GetWordView(WORD address, BOOL okHaltMode, BOOL okExec, int* pAddrType) const
{
    WORD offset;
    int addrtype = TranslateAddress(address, okHaltMode, okExec, &offset);

    *pAddrType = addrtype;

    switch (addrtype & ADDRTYPE_MASK)
    {
    case ADDRTYPE_RAM:
        //return GetRAMWord(offset & 0177776);  //TODO: Use (addrtype & ADDRTYPE_RAMMASK) bits
        return GetRAMWord(addrtype & ADDRTYPE_RAMMASK, offset & 0177776);
    case ADDRTYPE_ROM:
        return GetROMWord(offset);
    case ADDRTYPE_IO:
        return 0;  // I/O port, not memory
    case ADDRTYPE_DENY:
        return 0;  // This memory is inaccessible for reading
    }

    ASSERT(FALSE);  // If we are here - then addrtype has invalid value
    return 0;
}

WORD CMotherboard::GetWord(WORD address, BOOL okHaltMode, BOOL okExec)
{
    WORD offset;
    int addrtype = TranslateAddress(address, okHaltMode, okExec, &offset);

    switch (addrtype & ADDRTYPE_MASK)
    {
    case ADDRTYPE_RAM:
        return GetRAMWord(addrtype & ADDRTYPE_RAMMASK, offset & 0177776);
    case ADDRTYPE_ROM:
        return GetROMWord(offset);
    case ADDRTYPE_IO:
        //TODO: What to do if okExec == TRUE ?
        return GetPortWord(address);
    case ADDRTYPE_DENY:
        m_pCPU->MemoryError();
        return 0;
    }

    ASSERT(FALSE);  // If we are here - then addrtype has invalid value
    return 0;
}

BYTE CMotherboard::GetByte(WORD address, BOOL okHaltMode)
{
    WORD offset;
    int addrtype = TranslateAddress(address, okHaltMode, FALSE, &offset);

    switch (addrtype & ADDRTYPE_MASK)
    {
    case ADDRTYPE_RAM:
        return GetRAMByte(addrtype & ADDRTYPE_RAMMASK, offset);
    case ADDRTYPE_ROM:
        return GetROMByte(offset);
    case ADDRTYPE_IO:
        //TODO: What to do if okExec == TRUE ?
        return GetPortByte(address);
    case ADDRTYPE_DENY:
        m_pCPU->MemoryError();
        return 0;
    }

    ASSERT(FALSE);  // If we are here - then addrtype has invalid value
    return 0;
}

void CMotherboard::SetWord(WORD address, BOOL okHaltMode, WORD word)
{
    WORD offset;

    int addrtype = TranslateAddress(address, okHaltMode, FALSE, &offset);

    switch (addrtype & ADDRTYPE_MASK)
    {
    case ADDRTYPE_RAM:
        SetRAMWord(addrtype & ADDRTYPE_RAMMASK, offset & 0177776, word);
        return;
    case ADDRTYPE_ROM:  // Writing to ROM: exception
        m_pCPU->MemoryError();
        return;
    case ADDRTYPE_IO:
        SetPortWord(address, word);
        return;
    case ADDRTYPE_DENY:
        m_pCPU->MemoryError();
        return;
    }

    ASSERT(FALSE);  // If we are here - then addrtype has invalid value
}

void CMotherboard::SetByte(WORD address, BOOL okHaltMode, BYTE byte)
{
    WORD offset;
    int addrtype = TranslateAddress(address, okHaltMode, FALSE, &offset);

    switch (addrtype & ADDRTYPE_MASK)
    {
    case ADDRTYPE_RAM:
        SetRAMByte(addrtype & ADDRTYPE_RAMMASK, offset, byte);
        return;
    case ADDRTYPE_ROM:  // Writing to ROM: exception
        m_pCPU->MemoryError();
        return;
    case ADDRTYPE_IO:
        SetPortByte(address, byte);
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
    if (m_Configuration & BK_COPT_BK0011)
    {
        DWORD offset = (m_Port177662wr & 0100000) ? 6 : 5;
        offset *= 040000;
        return (m_pRAM + offset);
    }
    else
    {
        return (m_pRAM + 040000);
    }
}

int CMotherboard::TranslateAddress(WORD address, BOOL okHaltMode, BOOL okExec, WORD* pOffset) const
{
	if (okHaltMode && address <= 040000)
	{
        *pOffset = address;
		return ADDRTYPE_ROM;
	}

    if (address >= 0160000 && address < 170000)  // Port
    {
        *pOffset = address;
        return ADDRTYPE_IO;
    }

	//TODO: Логика диспетчера памяти
    *pOffset = address;
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
	case 0161060:
		//TODO: DLBUF -- Programmable parallel port
		return 0;
	case 0161062:
		//TODO: DLCSR -- Programmable parallel port
		return 0x0ffff;

    default:
#if !defined(PRODUCT)
            DebugLogFormat(_T("GETPORT %06o @ %06o\n"), address, m_pCPU->GetInstructionPC());
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
    switch (address)
    {
	case 0161012:
		//TODO: SNDС1R -- Sound control
		break;
	case 0161014:
		//TODO: SNDС1R -- Sound control
		break;
	case 0161016:
		//TODO: SNDСSR -- Sound control
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

	case 0161062:
		//TODO: DLCSR -- Programmable Parallel port control
		break;

	case 0161066:
		//TODO: KBDBUF -- Keyboard buffer
		break;

    default:
//#if !defined(PRODUCT)
//	    DebugLogFormat(_T("SETPORT %06o = %06o @ %06o\n"), address, word, m_pCPU->GetInstructionPC());
//#endif
        m_pCPU->MemoryError();
        break;
    }
}


//////////////////////////////////////////////////////////////////////
//
// Emulator image format:
//   32 bytes  - Header
//   32 bytes  - Board status
//   32 bytes  - CPU status
//   64 bytes  - CPU memory/IO controller status
//   32 Kbytes - ROM image
//   64 Kbytes - RAM image

//void CMotherboard::SaveToImage(BYTE* pImage)
//{
//    // Board data
//    WORD* pwImage = (WORD*) (pImage + 32);
//    *pwImage++ = m_timer;
//    *pwImage++ = m_timerreload;
//    *pwImage++ = m_timerflags;
//    *pwImage++ = m_timerdivider;
//    *pwImage++ = (WORD) m_chan0disabled;
//
//    // CPU status
//    BYTE* pImageCPU = pImage + 64;
//    m_pCPU->SaveToImage(pImageCPU);
//    // PPU status
//    BYTE* pImagePPU = pImageCPU + 32;
//    m_pPPU->SaveToImage(pImagePPU);
//    // CPU memory/IO controller status
//    BYTE* pImageCpuMem = pImagePPU + 32;
//    m_pFirstMemCtl->SaveToImage(pImageCpuMem);
//    // PPU memory/IO controller status
//    BYTE* pImagePpuMem = pImageCpuMem + 64;
//    m_pSecondMemCtl->SaveToImage(pImagePpuMem);
//
//    // ROM
//    BYTE* pImageRom = pImage + 256;
//    memcpy(pImageRom, m_pROM, 32 * 1024);
//    // RAM planes 0, 1, 2
//    BYTE* pImageRam = pImageRom + 32 * 1024;
//    memcpy(pImageRam, m_pRAM[0], 64 * 1024);
//    pImageRam += 64 * 1024;
//    memcpy(pImageRam, m_pRAM[1], 64 * 1024);
//    pImageRam += 64 * 1024;
//    memcpy(pImageRam, m_pRAM[2], 64 * 1024);
//}
//void CMotherboard::LoadFromImage(const BYTE* pImage)
//{
//    // Board data
//    WORD* pwImage = (WORD*) (pImage + 32);
//    m_timer = *pwImage++;
//    m_timerreload = *pwImage++;
//    m_timerflags = *pwImage++;
//    m_timerdivider = *pwImage++;
//    m_chan0disabled = (BYTE) *pwImage++;
//
//    // CPU status
//    const BYTE* pImageCPU = pImage + 64;
//    m_pCPU->LoadFromImage(pImageCPU);
//    // PPU status
//    const BYTE* pImagePPU = pImageCPU + 32;
//    m_pPPU->LoadFromImage(pImagePPU);
//    // CPU memory/IO controller status
//    const BYTE* pImageCpuMem = pImagePPU + 32;
//    m_pFirstMemCtl->LoadFromImage(pImageCpuMem);
//    // PPU memory/IO controller status
//    const BYTE* pImagePpuMem = pImageCpuMem + 64;
//    m_pSecondMemCtl->LoadFromImage(pImagePpuMem);
//
//    // ROM
//    const BYTE* pImageRom = pImage + 256;
//    memcpy(m_pROM, pImageRom, 32 * 1024);
//    // RAM planes 0, 1, 2
//    const BYTE* pImageRam = pImageRom + 32 * 1024;
//    memcpy(m_pRAM[0], pImageRam, 64 * 1024);
//    pImageRam += 64 * 1024;
//    memcpy(m_pRAM[1], pImageRam, 64 * 1024);
//    pImageRam += 64 * 1024;
//    memcpy(m_pRAM[2], pImageRam, 64 * 1024);
//}

//void CMotherboard::FloppyDebug(BYTE val)
//{
////#if !defined(PRODUCT)
////    TCHAR buffer[512];
////#endif
///*
//m_floppyaddr=0;
//m_floppystate=FLOPPY_FSM_WAITFORLSB;
//#define FLOPPY_FSM_WAITFORLSB	0
//#define FLOPPY_FSM_WAITFORMSB	1
//#define FLOPPY_FSM_WAITFORTERM1	2
//#define FLOPPY_FSM_WAITFORTERM2	3
//
//*/
//	switch(m_floppystate)
//	{
//		case FLOPPY_FSM_WAITFORLSB:
//			if(val!=0xff)
//			{
//				m_floppyaddr=val;
//				m_floppystate=FLOPPY_FSM_WAITFORMSB;
//			}
//			break;
//		case FLOPPY_FSM_WAITFORMSB:
//			if(val!=0xff)
//			{
//				m_floppyaddr|=val<<8;
//				m_floppystate=FLOPPY_FSM_WAITFORTERM1;
//			}
//			else
//			{
//				m_floppystate=FLOPPY_FSM_WAITFORLSB;
//			}
//			break;
//		case FLOPPY_FSM_WAITFORTERM1:
//			if(val==0xff)
//			{ //done
//				WORD par;
//				BYTE trk,sector,side;
//
//				par=m_pFirstMemCtl->GetWord(m_floppyaddr,0);
//
////#if !defined(PRODUCT)
////				wsprintf(buffer,_T(">>>>FDD Cmd %d "),(par>>8)&0xff);
////				DebugPrint(buffer);
////#endif
//                par=m_pFirstMemCtl->GetWord(m_floppyaddr+2,0);
//				side=par&0x8000?1:0;
////#if !defined(PRODUCT)
////				wsprintf(buffer,_T("Side %d Drv %d, Type %d "),par&0x8000?1:0,(par>>8)&0x7f,par&0xff);
////				DebugPrint(buffer);
////#endif
//				par=m_pFirstMemCtl->GetWord(m_floppyaddr+4,0);
//				sector=(par>>8)&0xff;
//				trk=par&0xff;
////#if !defined(PRODUCT)
////				wsprintf(buffer,_T("Sect %d, Trk %d "),(par>>8)&0xff,par&0xff);
////				DebugPrint(buffer);
////				PrintOctalValue(buffer,m_pFirstMemCtl->GetWord(m_floppyaddr+6,0));
////				DebugPrint(_T("Addr "));
////				DebugPrint(buffer);
////#endif
//				par=m_pFirstMemCtl->GetWord(m_floppyaddr+8,0);
////#if !defined(PRODUCT)
////				wsprintf(buffer,_T(" Block %d Len %d\n"),trk*20+side*10+sector-1,par);
////				DebugPrint(buffer);
////#endif
//
//				m_floppystate=FLOPPY_FSM_WAITFORLSB;
//			}
//			break;
//
//	}
//}


//////////////////////////////////////////////////////////////////////

WORD CMotherboard::GetKeyboardRegister(void)
{
    WORD res = 0;

    WORD mem000042 = GetRAMWord(000042);
    res |= (mem000042 & 0100000) == 0 ? KEYB_LAT : KEYB_RUS;

    return res;
}

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
