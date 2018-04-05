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
enum NeonConfiguration
{
    // Configuration options
    NEON_COPT_FDD = 16,

    NEON_COPT_RAMSIZE_MASK = 4096 | 2048 | 1024 | 512,
};


//////////////////////////////////////////////////////////////////////


// TranslateAddress result code
#define ADDRTYPE_RAM     0  // RAM
#define ADDRTYPE_ROM     1  // ROM
#define ADDRTYPE_IO      4  // I/O port
#define ADDRTYPE_EMUL    8  // I/O port emulation
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
typedef bool (CALLBACK* TAPEREADCALLBACK)(unsigned int samples);

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
    CProcessor* m_pCPU;  // CPU device
    CFloppyController* m_pFloppyCtl;  // FDD control
private:  // Memory
    uint16_t    m_Configuration;  // See NEON_COPT_Xxx flag constants
    uint8_t*    m_pRAM;  // RAM, 512..4096 KB
    uint8_t*    m_pROM;  // ROM, 16 KB
    uint16_t    m_HR[8];
    uint16_t    m_UR[8];
    uint32_t    m_nRamSizeBytes;  // Actual RAM size
public:  // Construct / destruct
    CMotherboard();
    ~CMotherboard();
public:  // Getting devices
    CProcessor* GetCPU() { return m_pCPU; }
public:  // Memory access  //TODO: Make it private
    uint16_t    GetRAMWord(uint32_t offset) const;
    uint8_t     GetRAMByte(uint32_t offset) const;
    void        SetRAMWord(uint32_t offset, uint16_t word);
    void        SetRAMByte(uint32_t offset, uint8_t byte);
    uint16_t    GetROMWord(uint16_t offset) const;
    uint8_t     GetROMByte(uint16_t offset) const;
public:  // Debug
    void        DebugTicks();  // One Debug PPU tick -- use for debug step or debug breakpoint
    void        SetCPUBreakpoint(uint16_t bp) { m_CPUbp = bp; } // Set CPU breakpoint
    bool        GetTrace() const { return m_okTraceCPU; }
    void        SetTrace(bool okTraceCPU) { m_okTraceCPU = okTraceCPU; }
public:  // System control
    void        SetConfiguration(uint16_t conf);
    void        Reset();  // Reset computer
    void        LoadROM(int bank, const uint8_t* pBuffer);  // Load 8 KB ROM image from the biffer
    void        LoadRAM(int startbank, const uint8_t* pBuffer, int length);  // Load data into the RAM
    void        Tick50();           // Tick 50 Hz - goes to CPU EVNT line
    void		TimerTick();		// Timer Tick, 31250 Hz, 32uS -- dividers are within timer routine
    void        DCLO_Signal() { }  ///< DCLO signal
    void        ResetDevices();     // INIT signal
public:
    void        ExecuteCPU();  // Execute one CPU instruction
    bool        SystemFrame();  // Do one frame -- use for normal run
    void        KeyboardEvent(uint8_t scancode, bool okPressed, bool okAr2);  // Key pressed or released
    uint16_t    GetKeyboardRegister(void);
    uint16_t    GetPrinterOutPort() const { return m_PortDLBUFout; }
    void        SetPrinterInPort(uint8_t data);
    bool        IsTapeMotorOn() const { return (m_Port177716tap & 0200) == 0; }
public:  // Floppy
    bool        AttachFloppyImage(int slot, LPCTSTR sFileName);
    void        DetachFloppyImage(int slot);
    bool        IsFloppyImageAttached(int slot);
    bool        IsFloppyReadOnly(int slot);
public:  // Callbacks
    void        SetTapeReadCallback(TAPEREADCALLBACK callback, int sampleRate);
    void        SetTapeWriteCallback(TAPEWRITECALLBACK callback, int sampleRate);
    void        SetSoundGenCallback(SOUNDGENCALLBACK callback);
    void        SetTeletypeCallback(TELETYPECALLBACK callback);
public:  // Memory
    // Read command for execution
    uint16_t GetWordExec(uint16_t address, bool okHaltMode) { return GetWord(address, okHaltMode, true); }
    // Read word from memory
    uint16_t GetWord(uint16_t address, bool okHaltMode) { return GetWord(address, okHaltMode, false); }
    // Read word
    uint16_t GetWord(uint16_t address, bool okHaltMode, bool okExec);
    // Write word
    void SetWord(uint16_t address, bool okHaltMode, uint16_t word);
    // Read byte
    uint8_t GetByte(uint16_t address, bool okHaltMode);
    // Write byte
    void SetByte(uint16_t address, bool okHaltMode, uint8_t byte);
    // Read word from memory for debugger
    uint16_t GetRAMWordView(uint32_t address) const;
    uint16_t GetWordView(uint16_t address, bool okHaltMode, bool okExec, int* pValid) const;
    // Read word from port for debugger
    uint16_t GetPortView(uint16_t address) const;
    // Read SEL register
    uint16_t GetSelRegister() const { return 0; }
private:
    void        TapeInput(bool inputBit);  // Tape input bit received
private:
    // Determite memory type for given address - see ADDRTYPE_Xxx constants
    //   okHaltMode - processor mode (USER/HALT)
    //   okExec - true: read instruction for execution; false: read memory
    //   pOffset - result - offset in memory plane
    int TranslateAddress(uint16_t address, bool okHaltMode, bool okExec, uint32_t* pOffset) const;
private:  // Access to I/O ports
    uint16_t    GetPortWord(uint16_t address);
    void        SetPortWord(uint16_t address, uint16_t word);
    uint8_t     GetPortByte(uint16_t address);
    void        SetPortByte(uint16_t address, uint8_t byte);
public:  // Saving/loading emulator status
    //void        SaveToImage(uint8_t* pImage);
    //void        LoadFromImage(const uint8_t* pImage);
private:  // Ports: implementation
    uint16_t    m_PortPPIB;
    uint16_t    m_Port177560;       // Serial port input state register
    uint16_t    m_Port177562;       // Serial port input data register
    uint16_t    m_Port177564;       // Serial port output state register
    uint16_t    m_Port177566;       // Serial port output data register
    uint16_t    m_PortKBDCSR;       // Keyboard status register
    uint16_t    m_PortKBDBUF;       // Keyboard register
    uint16_t    m_PortDLBUFin;      // Parallel port, input register
    uint16_t    m_PortDLBUFout;     // Parallel port, output register
    uint16_t    m_Port177716;       // System register (read only)
    uint16_t    m_Port177716mem;    // System register (memory)
    uint16_t    m_Port177716tap;    // System register (tape)
private:  // Timer implementation
    uint16_t    m_timer;
    uint16_t    m_timerreload;
    uint16_t    m_timerflags;
    uint16_t    m_timerdivider;
    void		SetTimerReload(uint16_t val);	//sets timer reload value
    void		SetTimerState(uint16_t val);	//sets timer state
private:
    uint16_t    m_CPUbp;  // CPU breakpoint address
    bool        m_okTraceCPU;
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
