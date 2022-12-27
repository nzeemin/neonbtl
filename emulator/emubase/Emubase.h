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

#define FLOPPY_FSM_IDLE         0

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
#define FLOPPY_STATUS_RDY                    02  // Ready status
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
    uint16_t dataptr;       // Data offset within m_data - "head" position
    uint8_t  data[FLOPPY_RAWTRACKSIZE];  // Raw track image for the current track
    uint8_t  marker[FLOPPY_RAWMARKERSIZE];  // Marker positions
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
    CFloppyDrive m_drivedata[2];  // Floppy drives
    CFloppyDrive* m_pDrive; // Current drive; nullptr if not selected
    uint16_t m_drive;       // Drive number: from 0 to 3; -1 if not selected
    uint16_t m_track;       // Track number: from 0 to 79
    uint16_t m_side;        // Disk side: 0 or 1
    uint16_t m_status;      // See FLOPPY_STATUS_XXX defines
    uint16_t m_flags;       // See FLOPPY_CMD_XXX defines
    uint16_t m_datareg;     // Read mode data register
    uint16_t m_writereg;    // Write mode data register
    bool m_writeflag;       // Write mode data register has data
    bool m_writemarker;     // Write marker in m_marker
    uint16_t m_shiftreg;    // Write mode shift register
    bool m_shiftflag;       // Write mode shift register has data
    bool m_shiftmarker;     // Write marker in m_marker
    bool m_writing;         // true = write mode, false = read mode
    bool m_searchsync;      // Read sub-mode: true = search for sync, false = just read
    bool m_crccalculus;     // true = CRC is calculated now
    bool m_trackchanged;    // true = m_data was changed - need to save it into the file
    bool m_okTrace;         // Trace mode on/off

public:
    CFloppyController();
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
    bool IsReadOnly(int drive) { return m_drivedata[drive].okReadOnly; }
    // Check if floppy engine now rotates
    bool IsEngineOn() { return (m_flags & FLOPPY_CMD_ENGINESTART) != 0; }
    uint16_t GetData();         // Reading port 177132 - data
    uint16_t GetState();        // Reading port 177130 - device status
    uint16_t GetDataView() const { return m_datareg; }  // Get port 177132 value for debugger
    uint16_t GetStateView() const { return m_status; }  // Get port 177130 value for debugger
    void SetCommand(uint16_t cmd);  // Writing to port 177130 - commands
    void WriteData(uint16_t data);  // Writing to port 177132 - data
    void Periodic();            // Rotate disk; call it each 64 us - 15625 times per second
    void SetTrace(bool okTrace) { m_okTrace = okTrace; }  // Set trace mode on/off

private:
    void PrepareTrack();
    void FlushChanges();  // If current track was changed - save it

};


//////////////////////////////////////////////////////////////////////
