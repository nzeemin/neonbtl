/*  This file is part of NEONBTL.
    NEONBTL is free software: you can redistribute it and/or modify it under the terms
of the GNU Lesser General Public License as published by the Free Software Foundation,
either version 3 of the License, or (at your option) any later version.
    NEONBTL is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU Lesser General Public License for more details.
    You should have received a copy of the GNU Lesser General Public License along with
NEONBTL. If not, see <http://www.gnu.org/licenses/>. */

// Board.h
//

#pragma once

#include "Defines.h"

class CProcessor;


//////////////////////////////////////////////////////////////////////

// Machine configurations
enum BKConfiguration
{
    // Configuration options
    BK_COPT_BK0010 = 0,
    BK_COPT_BK0011 = 1,
    BK_COPT_FDD = 16,
};


//////////////////////////////////////////////////////////////////////


// TranslateAddress result code
#define ADDRTYPE_RAM     0  // RAM
#define ADDRTYPE_ROM    32  // ROM
#define ADDRTYPE_IO     64  // I/O port
#define ADDRTYPE_DENY  128  // Access denied

//floppy debug
#define FLOPPY_FSM_WAITFORLSB	0
#define FLOPPY_FSM_WAITFORMSB	1
#define FLOPPY_FSM_WAITFORTERM1	2
#define FLOPPY_FSM_WAITFORTERM2	3


//////////////////////////////////////////////////////////////////////
// Special key codes

// Tape emulator callback used to read a tape recorded data.
// Input:
//   samples    Number of samples to play.
// Output:
//   result     Bit to put in tape input port.
typedef BOOL (CALLBACK* TAPEREADCALLBACK)(unsigned int samples);

// Tape emulator callback used to write a data to tape.
// Input:
//   value      Sample value to write.
typedef void (CALLBACK* TAPEWRITECALLBACK)(int value, unsigned int samples);

// Sound generator callback function type
typedef void (CALLBACK* SOUNDGENCALLBACK)(unsigned short L, unsigned short R);

// Teletype callback function type - board calls it if symbol ready to transmit
typedef void (CALLBACK* TELETYPECALLBACK)(unsigned char value);

class CFloppyController;

//////////////////////////////////////////////////////////////////////

class CMotherboard  // Souz-Neon computer
{
private:  // Devices
    CProcessor*     m_pCPU;  // CPU device
    CFloppyController*  m_pFloppyCtl;  // FDD control
private:  // Memory
    WORD        m_Configuration;  // See BK_COPT_Xxx flag constants
    BYTE*       m_pRAM;  // RAM, 512 KB
    BYTE*       m_pROM;  // ROM, 16 KB
    WORD        m_HR[8];
    WORD        m_UR[8];
public:  // Construct / destruct
    CMotherboard();
    ~CMotherboard();
public:  // Getting devices
    CProcessor*     GetCPU() { return m_pCPU; }
public:  // Memory access  //TODO: Make it private
    WORD        GetRAMWord(DWORD offset) const;
    WORD        GetRAMWord(WORD hioffset, WORD offset) const;
    BYTE        GetRAMByte(DWORD offset) const;
    BYTE        GetRAMByte(WORD hioffset, WORD offset) const;
    void        SetRAMWord(DWORD offset, WORD word);
    void        SetRAMWord(WORD hioffset, WORD offset, WORD word);
    void        SetRAMByte(DWORD offset, BYTE byte);
    void        SetRAMByte(WORD hioffset, WORD offset, BYTE byte);
    WORD        GetROMWord(WORD offset) const;
    BYTE        GetROMByte(WORD offset) const;
public:  // Debug
    void        DebugTicks();  // One Debug PPU tick -- use for debug step or debug breakpoint
    void        SetCPUBreakpoint(WORD bp) { m_CPUbp = bp; } // Set CPU breakpoint
    BOOL        GetTrace() const { return m_okTraceCPU; }
    void        SetTrace(BOOL okTraceCPU) { m_okTraceCPU = okTraceCPU; }
public:  // System control
    void        SetConfiguration(WORD conf);
    void        Reset();  // Reset computer
    void        LoadROM(int bank, const BYTE* pBuffer);  // Load 8 KB ROM image from the biffer
    void        LoadRAM(int startbank, const BYTE* pBuffer, int length);  // Load data into the RAM
    void        Tick50();           // Tick 50 Hz - goes to CPU EVNT line
    void		TimerTick();		// Timer Tick, 31250 Hz, 32uS -- dividers are within timer routine
    void        DCLO_Signal() { }  ///< DCLO signal
    void        ResetDevices();     // INIT signal
public:
    void        ExecuteCPU();  // Execute one CPU instruction
    BOOL        SystemFrame();  // Do one frame -- use for normal run
    void        KeyboardEvent(BYTE scancode, BOOL okPressed, BOOL okAr2);  // Key pressed or released
    WORD        GetKeyboardRegister(void);
    WORD        GetPrinterOutPort() const { return m_PortDLBUFout; }
    void        SetPrinterInPort(BYTE data);
    BOOL        IsTapeMotorOn() const { return (m_Port177716tap & 0200) == 0; }
public:  // Floppy
    BOOL        AttachFloppyImage(int slot, LPCTSTR sFileName);
    void        DetachFloppyImage(int slot);
    BOOL        IsFloppyImageAttached(int slot);
    BOOL        IsFloppyReadOnly(int slot);
public:  // Callbacks
    void		SetTapeReadCallback(TAPEREADCALLBACK callback, int sampleRate);
    void        SetTapeWriteCallback(TAPEWRITECALLBACK callback, int sampleRate);
    void		SetSoundGenCallback(SOUNDGENCALLBACK callback);
    void		SetTeletypeCallback(TELETYPECALLBACK callback);
public:  // Memory
    // Read command for execution
    WORD GetWordExec(WORD address, BOOL okHaltMode) { return GetWord(address, okHaltMode, TRUE); }
    // Read word from memory
    WORD GetWord(WORD address, BOOL okHaltMode) { return GetWord(address, okHaltMode, FALSE); }
    // Read word
    WORD GetWord(WORD address, BOOL okHaltMode, BOOL okExec);
    // Write word
    void SetWord(WORD address, BOOL okHaltMode, WORD word);
    // Read byte
    BYTE GetByte(WORD address, BOOL okHaltMode);
    // Write byte
    void SetByte(WORD address, BOOL okHaltMode, BYTE byte);
    // Read word from memory for debugger
    WORD GetWordView(WORD address, BOOL okHaltMode, BOOL okExec, int* pValid) const;
    // Read word from port for debugger
    WORD GetPortView(WORD address) const;
    // Read SEL register
    WORD GetSelRegister() const { return 0; }
    // Get video buffer address
    const BYTE* GetVideoBuffer();
private:
    void        TapeInput(BOOL inputBit);  // Tape input bit received
private:
    // Determite memory type for given address - see ADDRTYPE_Xxx constants
    //   okHaltMode - processor mode (USER/HALT)
    //   okExec - TRUE: read instruction for execution; FALSE: read memory
    //   pOffset - result - offset in memory plane
    int TranslateAddress(WORD address, BOOL okHaltMode, BOOL okExec, DWORD* pOffset) const;
private:  // Access to I/O ports
    WORD        GetPortWord(WORD address);
    void        SetPortWord(WORD address, WORD word);
    BYTE        GetPortByte(WORD address);
    void        SetPortByte(WORD address, BYTE byte);
public:  // Saving/loading emulator status
    //void        SaveToImage(BYTE* pImage);
    //void        LoadFromImage(const BYTE* pImage);
private:  // Ports: implementation
    WORD        m_Port177560;       // Serial port input state register
    WORD        m_Port177562;       // Serial port input data register
    WORD        m_Port177564;       // Serial port output state register
    WORD        m_Port177566;       // Serial port output data register
    WORD        m_PortKBDCSR;       // Keyboard status register
    WORD        m_PortKBDBUF;       // Keyboard register
    WORD        m_PortDLBUFin;      // Parallel port, input register
    WORD        m_PortDLBUFout;     // Parallel port, output register
    WORD        m_Port177716;       // System register (read only)
    WORD        m_Port177716mem;    // System register (memory)
    WORD        m_Port177716tap;    // System register (tape)
private:  // Timer implementation
    WORD		m_timer;
    WORD		m_timerreload;
    WORD		m_timerflags;
    WORD		m_timerdivider;
    void		SetTimerReload(WORD val);	//sets timer reload value
    void		SetTimerState(WORD val);	//sets timer state
private:
    WORD        m_CPUbp;  // CPU breakpoint address
    BOOL        m_okTraceCPU;
private:
    TAPEREADCALLBACK m_TapeReadCallback;
    TAPEWRITECALLBACK m_TapeWriteCallback;
    int			m_nTapeSampleRate;
    SOUNDGENCALLBACK m_SoundGenCallback;
    TELETYPECALLBACK m_TeletypeCallback;
private:
    void        DoSound(void);
};


//////////////////////////////////////////////////////////////////////
