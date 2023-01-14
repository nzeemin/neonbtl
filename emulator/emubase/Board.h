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
class Motherboard;
class CFloppyController;


//////////////////////////////////////////////////////////////////////

// Machine configurations
enum NeonConfiguration
{
    NEON_COPT_RAMSIZE_MASK = 4096 | 2048 | 1024 | 512,
};


//////////////////////////////////////////////////////////////////////


// TranslateAddress result code
#define ADDRTYPE_RAM     0  // RAM
#define ADDRTYPE_ROM     1  // ROM
#define ADDRTYPE_IO      4  // I/O port
#define ADDRTYPE_EMUL    8  // I/O port emulation, USER mode only
#define ADDRTYPE_DENY  128  // Access denied

//floppy debug
#define FLOPPY_FSM_WAITFORLSB   0
#define FLOPPY_FSM_WAITFORMSB   1
#define FLOPPY_FSM_WAITFORTERM1 2
#define FLOPPY_FSM_WAITFORTERM2 3

// Trace flags
#define TRACE_NONE         0  // Turn off all tracing
#define TRACE_FLOPPY    0100  // Trace floppies
#define TRACE_CPU      01000  // Trace CPU instructions
#define TRACE_ALL    0177777  // Trace all

// PIC 8259A flags
#define PIC_MODE_ICW1      1  // Wait for ICW1 after RESET
#define PIC_MODE_ICW2      2  // Wait for ICW2 after ICW1
#define PIC_MODE_MASK    255  // Mask for mode bits, usage: (m_PICflags & PIC_MODE_MASK)
#define PIC_CMD_POLL     256  // Flag for Poll Command


//////////////////////////////////////////////////////////////////////
// Special key codes

// Sound generator callback function type
typedef void (CALLBACK* SOUNDGENCALLBACK)(unsigned short L, unsigned short R);

// Serial port output callback
typedef void (CALLBACK* SERIALOUTCALLBACK)(uint8_t byte);

// Parallel port output callback
typedef void (CALLBACK* PARALLELOUTCALLBACK)(uint8_t byte);


//////////////////////////////////////////////////////////////////////

struct PIT8253_chan
{
    uint8_t     control;    // Control byte
    uint8_t     phase;
    uint16_t    value;      // Current counter value
    uint16_t    count;      // Counter reload value
    bool        gate;       // Gate input line
    bool        writehi;
    bool        readhi;
    bool        output;
public:
    PIT8253_chan();
};

class PIT8253
{
    friend class CMotherboard;
    PIT8253_chan    m_chan[3];
public:
    PIT8253();
    void        Write(uint8_t address, uint8_t byte);
    uint8_t     Read(uint8_t address);
    void        SetGate(uint8_t chan, bool gate);
    void        Tick();
    bool        GetOutput(uint8_t chan);
private:
    void        Tick(uint8_t channel);
};

//////////////////////////////////////////////////////////////////////

// Souz-Neon computer
class CMotherboard
{
public:  // Construct / destruct
    CMotherboard();
    ~CMotherboard();
private:  // Devices
    uint16_t    m_Configuration;  // See NEON_COPT_Xxx flag constants
    CProcessor* m_pCPU;  // CPU device
    CFloppyController* m_pFloppyCtl;  // FDD control
public:  // Getting devices
    CProcessor* GetCPU() { return m_pCPU; }
private:  // Memory
    uint8_t*    m_pROM;  // ROM, 16 KB
    uint8_t*    m_pRAM;  // RAM, 512..4096 KB
    uint16_t    m_HR[8];
    uint16_t    m_UR[8];
    uint32_t    m_nRamSizeBytes;  // Actual RAM size
    uint8_t*    m_pHDbuff;  // HD buffers, 2K
public:  // Memory access
    uint16_t    GetRAMWord(uint32_t offset) const;
    uint8_t     GetRAMByte(uint32_t offset) const;
    void        SetRAMWord(uint32_t offset, uint16_t word);
    void        SetRAMByte(uint32_t offset, uint8_t byte);
    uint16_t    GetROMWord(uint16_t offset) const;
    uint8_t     GetROMByte(uint16_t offset) const;
    uint32_t    GetRamSizeBytes() const { return m_nRamSizeBytes; }
public:  // Debug
    void        DebugTicks();  // One Debug CPU tick -- use for debug step or debug breakpoint
    void        SetCPUBreakpoints(const uint16_t* bps) { m_CPUbps = bps; } // Set CPU breakpoint list
    uint32_t    GetTrace() const { return m_dwTrace; }
    void        SetTrace(uint32_t dwTrace);
    void        LoadRAMBank(int bank, const void* buffer);
public:  // System control
    void        SetConfiguration(uint16_t conf);
    void        LoadROM(const uint8_t* pBuffer);  // Load 16 KB ROM image from the buffer
    void        Reset();  // Reset computer
    void        Tick50();           // Tick 50 Hz
    void        TimerTick();        // Timer Tick
    void        ResetDevices();     // INIT signal
    bool        SystemFrame();  // Do one frame -- use for normal run
    void        UpdateKeyboardMatrix(const uint8_t matrix[8]);
public:  // Floppy
    bool        AttachFloppyImage(int slot, LPCTSTR sFileName);
    void        DetachFloppyImage(int slot);
    bool        IsFloppyImageAttached(int slot) const;
    bool        IsFloppyReadOnly(int slot) const;
    // Fill the current HD buffer, to call from floppy controller only
    bool        FillHDBuffer(const uint8_t* data);
public:  // Callbacks
    void        SetSoundGenCallback(SOUNDGENCALLBACK callback);
    void        SetSerialOutCallback(SERIALOUTCALLBACK outcallback);
    void        SetParallelOutCallback(PARALLELOUTCALLBACK outcallback);
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
    // Read word from memory for video renderer and debugger
    uint8_t GetRAMByteView(uint32_t offset) const;
    uint16_t GetRAMWordView(uint32_t offset) const;
    uint16_t GetWordView(uint16_t address, bool okHaltMode, bool okExec, int* pAddrType) const;
    uint32_t GetRAMFullAddress(uint16_t address, bool okHaltMode) const;
    // Read word from port for debugger
    uint16_t GetPortView(uint16_t address) const;
    // Read SEL register
    static uint16_t GetSelRegister() { return 0; }
private:
    // Determine memory type for the given address - see ADDRTYPE_Xxx constants
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
    void        SaveToImage(uint8_t* pImage);
    void        LoadFromImage(const uint8_t* pImage);
private:  // Ports/devices: implementation
    uint16_t    m_PICflags;         // PIC 8259A flags, see PIC_Xxx constants
    uint8_t     m_PICRR;            // PIC interrupt request register
    uint8_t     m_PICMR;            // PIC mask register
    uint16_t    m_PPIA;
    uint16_t    m_PPIB;             // 161032 Printer data - bits 0..7
    uint16_t    m_PPIC;             // 161034
    uint16_t    m_hdsdh;
    bool        m_hdint;            // HDD interrupt flag
    uint8_t     m_nHDbuff;          // Index of the current HD buffer, 0..3
    uint16_t    m_nHDbuffpos;       // Current position in the current HD buffer, 0..511
    uint8_t     m_keymatrix[8];     // Keyboard matrix
    uint16_t    m_keypos;           // Keyboard reading position 0..7
    bool        m_keyint;           // Keyboard interrupt flag
    PIT8253     m_snd, m_snl;
    uint8_t     m_rtcalarmsec, m_rtcalarmmin, m_rtcalarmhour;
    uint8_t     m_rtcmemory[50];
private:
    void        ProcessPICWrite(bool a, uint8_t byte);
    uint8_t     ProcessPICRead(bool a);
    void        SetPICInterrupt(int signal, bool set = true);  // Set/reset PIC interrupt signal 0..7
    void        UpdateInterrupts();
    uint8_t     ProcessRtcRead(uint16_t address) const;
    void        ProcessRtcWrite(uint16_t address, uint8_t byte);
    void        ProcessTimerWrite(uint16_t address, uint8_t byte);
    uint8_t     ProcessTimerRead(uint16_t address);
    void        ProcessKeyboardWrite(uint8_t byte);
    void        DoSound();
private:
    const uint16_t* m_CPUbps;  // CPU breakpoint list, ends with 177777 value
    uint32_t    m_dwTrace;  // Trace flags
private:
    SOUNDGENCALLBACK m_SoundGenCallback;
    SERIALOUTCALLBACK m_SerialOutCallback;
    PARALLELOUTCALLBACK m_ParallelOutCallback;
};


//////////////////////////////////////////////////////////////////////
