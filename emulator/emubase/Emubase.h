/*  This file is part of NEONBTL.
    NEONBTL is free software: you can redistribute it and/or modify it under the terms
of the GNU Lesser General Public License as published by the Free Software Foundation,
either version 3 of the License, or (at your option) any later version.
    NEONBTL is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU Lesser General Public License for more details.
    You should have received a copy of the GNU Lesser General Public License along with
NEONBTL. If not, see <http://www.gnu.org/licenses/>. */

// Emubase.h  Header for use of all emubase classes

#pragma once

#include "Board.h"
#include "Processor.h"


//////////////////////////////////////////////////////////////////////


#define SOUNDSAMPLERATE  22050


//////////////////////////////////////////////////////////////////////
// Disassembler

// Disassemble one instruction of KM1801VM2 processor
//   pMemory - memory image (we read only words of the instruction)
//   sInstr  - instruction mnemonics buffer - at least 8 characters
//   sArg    - instruction arguments buffer - at least 32 characters
//   Return value: number of words in the instruction
uint16_t DisassembleInstruction(const uint16_t* pMemory, uint16_t addr, TCHAR* sInstr, TCHAR* sArg);

bool Disasm_CheckForJump(const uint16_t* memory, int* pDelta);

// Prepare "Jump Hint" string, and also calculate condition for conditional jump
// Returns: jump prediction flag: true = will jump, false = will not jump
bool Disasm_GetJumpConditionHint(
    const uint16_t* memory, const CProcessor * pProc, const CMotherboard * pBoard, LPTSTR buffer);

// Prepare "Instruction Hint" for a regular instruction (not a branch/jump one)
// buffer, buffer2 - buffers for 1st and 2nd lines of the instruction hint, min size 42
// Returns: number of hint lines; 0 = no hints
int Disasm_GetInstructionHint(
    const uint16_t* memory, const CProcessor * pProc, const CMotherboard * pBoard,
    LPTSTR buffer, LPTSTR buffer2);


//////////////////////////////////////////////////////////////////////
// CFloppy

class CMotherboard;

#define FLOPPY_MAX_TRACKS       83

#define FLOPPY_PHASE_CMD        1
#define FLOPPY_PHASE_EXEC       2
#define FLOPPY_PHASE_RESULT     3

#define FLOPPY_STATE_IDLE          0
#define FLOPPY_STATE_RECALIBRATE   1
#define FLOPPY_STATE_SEEK          2
#define FLOPPY_STATE_READ_DATA     3
#define FLOPPY_STATE_WRITE_DATA    4
#define FLOPPY_STATE_READ_TRACK    5
#define FLOPPY_STATE_FORMAT_TRACK  6
#define FLOPPY_STATE_READ_ID       7
#define FLOPPY_STATE_SCAN_DATA     8

#define FLOPPY_COMMAND_INVALID              0xff
#define FLOPPY_COMMAND_INCOMPLETE           0xfe
#define FLOPPY_COMMAND_SPECIFY                 3
#define FLOPPY_COMMAND_SENSE_DRIVE_STATUS      4
#define FLOPPY_COMMAND_RECALIBRATE             7
#define FLOPPY_COMMAND_SENSE_INTERRUPT_STATUS  8
#define FLOPPY_COMMAND_SEEK                   15
#define FLOPPY_COMMAND_READ_TRACK              2
#define FLOPPY_COMMAND_WRITE_DATA              5
#define FLOPPY_COMMAND_READ_DATA               6
#define FLOPPY_COMMAND_READ_ID                10
#define FLOPPY_COMMAND_FORMAT_TRACK           14
#define FLOPPY_COMMAND_SCAN_EQUAL             17
#define FLOPPY_COMMAND_SCAN_LOW               25
#define FLOPPY_COMMAND_SCAN_HIGH              29

#define FLOPPY_MSR_DB   0x0f
#define FLOPPY_MSR_CB   0x10
#define FLOPPY_MSR_EXM  0x20
#define FLOPPY_MSR_DIO  0x40
#define FLOPPY_MSR_RQM  0x80

struct CFloppyDrive
{
    FILE*    fpFile;
    uint8_t* data;          // Data image for the whole disk
    uint32_t datasize;
    uint32_t dirtystart, dirtyend;  // Range of unsaved data; dirtyend == 0 means everything saved
    uint16_t dirtycount;
    bool     okReadOnly;    // Write protection flag

public:
    CFloppyDrive();
    void Reset();           // Reset the device

    void WriteBlock(uint16_t block, const uint8_t* src);

    bool IsDirty() const { return dirtyend != 0; }  // Has unsaved data
    void Flush();  // Save any unsaved data
};

// Floppy controller
class CFloppyController
{
protected:
    CMotherboard* m_pBoard;
    CFloppyDrive m_drivedata[4];  // Floppy drives
    CFloppyDrive* m_pDrive; // Current drive; nullptr if not selected
    uint8_t  m_drive;       // Current drive number: 0 to 3; 0xff if not selected
    uint8_t  m_phase;       // See FLOPPY_PHASE_XXX defines
    uint8_t  m_state;       // See FLOPPY_STATE_XXX defines
    uint8_t  m_command[9];  // Buffer for command bytes
    uint8_t  m_commandlen;
    uint8_t  m_result[9];   // Buffer for command result bytes
    uint8_t  m_resultlen;
    uint8_t  m_resultpos;   // Current position in the result buffer
    uint8_t  m_track;       // Track number: 0 to 79
    uint8_t  m_side;        // Disk side: 0 or 1
    bool     m_int;         // Interrupt flag
    bool     m_motor;       // Motor on/off
    bool     m_okTrace;     // Trace mode on/off

public:
    CFloppyController(CMotherboard* pBoard);
    ~CFloppyController();
    void Reset();           // Reset the device

public:
    // Attach the image to the drive - insert disk
    bool AttachImage(int drive, LPCTSTR sFileName);
    // Detach image from the drive - remove disk
    void DetachImage(int drive);
    // Check if the drive has an image attached
    bool IsAttached(int drive) const { return (m_drivedata[drive].fpFile != nullptr); }
    // Check if the drive's attached image is read-only
    bool IsReadOnly(int drive) const { return m_drivedata[drive].okReadOnly; }
    // Check if floppy engine now rotates
    bool IsEngineOn() const { return m_motor; }
public:
    void     SetParams(uint8_t side, uint8_t density, uint8_t drive, uint8_t motor);
    uint8_t  GetState();        // Reading status
    uint16_t GetStateView() const { return m_state; }  // Get status value for debugger
    void     FifoWrite(uint8_t cmd);  // Writing commands
    uint8_t  FifoRead();
    void Periodic();            // Rotate disk; call it each 64 us - 15625 times per second
    bool CheckInterrupt() const { return m_int; }
    void SetTrace(bool okTrace) { m_okTrace = okTrace; }  // Set trace mode on/off

private:
    uint8_t CheckCommand();
    void StartCommand(uint8_t cmd);
    void ExecuteCommand(uint8_t cmd);
    void FlushChanges();  // Save all unsaved data
};


//////////////////////////////////////////////////////////////////////
// CHardDrive

#define IDE_DISK_SECTOR_SIZE      512

// IDE hard drive
class CHardDrive
{
protected:
    FILE*   m_fpFile;           // File pointer for the attached HDD image
    bool    m_okReadOnly;       // Flag indicating that the HDD image file is read-only
    uint8_t m_status;           // IDE status register, see IDE_STATUS_XXX constants
    uint8_t m_error;            // IDE error register, see IDE_ERROR_XXX constants
    uint8_t m_command;          // Current IDE command, see IDE_COMMAND_XXX constants
    uint32_t m_lba;             // LBA sector number
    int     m_numcylinders;     // Cylinder count
    int     m_numheads;         // Head count
    int     m_numsectors;       // Sectors per track
    int     m_curhead;          // Current head number
    int     m_curheadreg;       // Current head number
    int     m_sectorcount;      // Sector counter for read/write operations
    uint8_t m_buffer[IDE_DISK_SECTOR_SIZE];  // Sector data buffer
    int     m_bufferoffset;     // Current offset within sector: 0..511
    int     m_timeoutcount;     // Timeout counter to wait for the next event
    int     m_timeoutevent;     // Current stage of operation, see TimeoutEvent enum

public:
    CHardDrive();
    ~CHardDrive();
    // Reset the device.
    void Reset();
    // Attach HDD image file to the device
    bool AttachImage(LPCTSTR sFileName);
    // Detach HDD image file from the device
    void DetachImage();
    // Check if the attached hard drive image is read-only
    bool IsReadOnly() const { return m_okReadOnly; }

public:
    // Read word from the device port
    uint16_t ReadPort(uint16_t port);
    // Write word th the device port
    void WritePort(uint16_t port, uint16_t data);
    // Rotate disk
    void Periodic();

private:
    uint32_t CalculateOffset() const;  // Calculate sector offset in the HDD image
    void HandleCommand(uint8_t command);  // Handle the IDE command
    void ReadNextSector();
    void ReadSectorDone();
    void WriteSectorDone();
    void NextSector();          // Advance to the next sector, CHS-based
    void ContinueRead();
    void ContinueWrite();
    void IdentifyDrive();       // Prepare m_buffer for the IDENTIFY DRIVE command
};


//////////////////////////////////////////////////////////////////////
