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

#define FLOPPY_CMD_CORRECTION250        04
#define FLOPPY_CMD_ENGINESTART          020
#define FLOPPY_CMD_CORRECTION500        010
#define FLOPPY_CMD_SIDEUP               040
#define FLOPPY_CMD_DIR                  0100
#define FLOPPY_CMD_STEP                 0200
#define FLOPPY_CMD_SEARCHSYNC           0400
#define FLOPPY_CMD_SKIPSYNC             01000
//dir == 0 to center (towards trk0)
//dir == 1 from center (towards trk80)

#define FLOPPY_STATUS_TRACK0                 01  // Track 0 flag
#define FLOPPY_STATUS_RDY                  0200  // Ready status
#define FLOPPY_STATUS_WRITEPROTECT           04  // Write protect
#define FLOPPY_STATUS_MOREDATA             0200  // Need more data flag
#define FLOPPY_STATUS_CHECKSUMOK         040000  // Checksum verified OK
#define FLOPPY_STATUS_INDEXMARK         0100000  // Index flag, indicates the beginning of track

#define FLOPPY_RAWTRACKSIZE             6250
#define FLOPPY_RAWMARKERSIZE            (FLOPPY_RAWTRACKSIZE / 2)
#define FLOPPY_INDEXLENGTH              30

struct CFloppyDrive
{
    FILE*    fpFile;
    bool     okReadOnly;    // Write protection flag
    uint8_t* data;          // Data image for the whole disk
    uint16_t dataptr;       // Data offset within m_data - "head" position
    uint16_t datatrack;     // Track number of data in m_data array
    uint16_t dataside;      // Disk side of data in m_data array

public:
    CFloppyDrive();
    void Reset();       // Reset the device
};

// Floppy controller
class CFloppyController
{
protected:
    CMotherboard* m_pBoard;
    CFloppyDrive m_drivedata[4];  // Floppy drives
    CFloppyDrive* m_pDrive; // Current drive; nullptr if not selected
    uint8_t  m_drive;       // Current drive number: 0 to 3; 0xff if not selected
    uint8_t  m_phase;
    uint8_t  m_state;       // See FLOPPY_STATE_XXX defines
    uint8_t  m_command[9];
    uint8_t  m_commandlen;
    uint8_t  m_result[9];
    uint8_t  m_resultlen;
    uint8_t  m_resultpos;
    uint8_t  m_track;       // Track number: 0 to 79
    uint8_t  m_side;        // Disk side: 0 or 1
    bool     m_int;         // Interrupt flag
    bool     m_trackchanged; // true = m_data was changed - need to save it into the file
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
public:
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
    void PrepareTrack();
    void FlushChanges();  // If current track was changed - save it
};


//////////////////////////////////////////////////////////////////////
