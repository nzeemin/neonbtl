/*  This file is part of NEONBTL.
    NEONBTL is free software: you can redistribute it and/or modify it under the terms
of the GNU Lesser General Public License as published by the Free Software Foundation,
either version 3 of the License, or (at your option) any later version.
    NEONBTL is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU Lesser General Public License for more details.
    You should have received a copy of the GNU Lesser General Public License along with
NEONBTL. If not, see <http://www.gnu.org/licenses/>. */

// Floppy.cpp
// Floppy controller and drives emulation
// See defines in header file Emubase.h

#include "stdafx.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <share.h>
#include "Emubase.h"


//////////////////////////////////////////////////////////////////////


CFloppyDrive::CFloppyDrive()
{
    fpFile = nullptr;
    okReadOnly = false;
    datatrack = dataside = 0;
    dataptr = 0;
    data = nullptr;
}

void CFloppyDrive::Reset()
{
    datatrack = dataside = 0;
    dataptr = 0;
    //TODO: re-read data from the file
}


//////////////////////////////////////////////////////////////////////


CFloppyController::CFloppyController(CMotherboard* pBoard)
    : m_pBoard(pBoard)
{
    m_drive = m_side = m_track = 0;
    m_pDrive = m_drivedata;
    m_phase = FLOPPY_PHASE_CMD;
    m_state = FLOPPY_STATE_IDLE;
    m_int = false;
    m_commandlen = m_resultlen = m_resultpos = 0;
    m_trackchanged = false;
    m_okTrace = false;
}

CFloppyController::~CFloppyController()
{
    for (int drive = 0; drive < 4; drive++)
        DetachImage(drive);
}

void CFloppyController::Reset()
{
    if (m_okTrace) DebugLog(_T("Floppy RESET\r\n"));

    FlushChanges();

    m_drive = m_side = m_track = 0;
    m_pDrive = m_drivedata;
    m_phase = FLOPPY_PHASE_CMD;
    m_state = FLOPPY_STATE_IDLE;
    m_int = false;
    m_commandlen = m_resultlen = m_resultpos = 0;
    m_trackchanged = false;

    PrepareTrack();
}

bool CFloppyController::AttachImage(int drive, LPCTSTR sFileName)
{
    ASSERT(sFileName != nullptr);

    // If image attached - detach one first
    if (m_drivedata[drive].fpFile != nullptr)
        DetachImage(drive);

    // Open file
    m_drivedata[drive].okReadOnly = false;
    m_drivedata[drive].fpFile = ::_tfsopen(sFileName, _T("r+b"), _SH_DENYNO);
    if (m_drivedata[drive].fpFile == nullptr)
    {
        m_drivedata[drive].okReadOnly = true;
        m_drivedata[drive].fpFile = ::_tfsopen(sFileName, _T("rb"), _SH_DENYNO);
    }
    if (m_drivedata[drive].fpFile == nullptr)
        return false;

    m_drivedata[drive].data = (uint8_t*)::calloc(800 * 1024, 1);

    ::fread(m_drivedata[drive].data, 1, 800 * 1024, m_pDrive->fpFile);
    //TODO: Контроль ошибок чтения

    m_side = m_track = 0;
    m_drivedata[drive].datatrack = m_drivedata[drive].dataside = 0;
    m_drivedata[drive].dataptr = 0;
    m_trackchanged = false;

    //PrepareTrack();

    return true;
}

void CFloppyController::DetachImage(int drive)
{
    if (m_drivedata[drive].fpFile == nullptr) return;

    FlushChanges();

    ::fclose(m_drivedata[drive].fpFile);
    m_drivedata[drive].fpFile = nullptr;
    m_drivedata[drive].okReadOnly = false;
    ::free(m_drivedata[drive].data);  m_drivedata[drive].data = nullptr;
    m_drivedata[drive].Reset();
}

//////////////////////////////////////////////////////////////////////


uint8_t CFloppyController::GetState()
{
    uint8_t msr = 0;
    switch (m_phase)
    {
    case FLOPPY_PHASE_CMD:
        msr |= FLOPPY_MSR_RQM;
        //TODO
        break;
    case FLOPPY_PHASE_EXEC:
        msr |= FLOPPY_MSR_CB;
        break;
    case FLOPPY_PHASE_RESULT:
        msr |= FLOPPY_MSR_RQM | FLOPPY_MSR_DIO | FLOPPY_MSR_CB;
        break;
    }

    //if (m_pDrive == nullptr)
    //    return 0;
    //if (m_pDrive->fpFile == nullptr)
    //    return 0;//TODO

    return msr;
}

void CFloppyController::FifoWrite(uint8_t data)
{
    if (m_okTrace) DebugLogFormat(_T("Floppy FIFO WR 0x%02hx\r\n"), (uint16_t)data);

    if (m_phase == FLOPPY_PHASE_CMD)
    {
        m_int = false;
        m_command[m_commandlen++] = data;

        uint8_t cmd = CheckCommand();

        if (cmd == FLOPPY_COMMAND_INCOMPLETE)
            return;
        if (cmd == FLOPPY_COMMAND_INVALID)
        {
            m_phase = FLOPPY_PHASE_RESULT;
            m_result[0] = 0x80;
            m_resultlen = 1;
            m_resultpos = 0;
            m_commandlen = 0;
            return;
        }

        StartCommand(cmd);
    }
    //TODO

}

uint8_t CFloppyController::FifoRead()
{
    uint8_t r = 0xff;
    switch (m_phase)
    {
    case FLOPPY_PHASE_CMD:
        break;
    case FLOPPY_PHASE_EXEC:
        break;
    case FLOPPY_PHASE_RESULT:
        m_int = false;//TODO: not sure it should be here
        if (m_resultpos < m_resultlen)
        {
            r = m_result[m_resultpos++];
        }
        if (m_resultpos >= m_resultlen)
        {
            m_phase = FLOPPY_PHASE_CMD;
        }
        break;
    }
    if (m_okTrace) DebugLogFormat(_T("Floppy FIFO RD 0x%02hx\r\n"), (uint16_t)r);
    return r;
}

uint8_t CFloppyController::CheckCommand()
{
    switch (m_command[0])
    {
    case 0x03:
        return m_commandlen == 3 ? FLOPPY_COMMAND_SPECIFY : FLOPPY_COMMAND_INCOMPLETE;
    case 0x04:
        return m_commandlen == 2 ? FLOPPY_COMMAND_SENSE_DRIVE_STATUS : FLOPPY_COMMAND_INCOMPLETE;
    case 0x07:
        return m_commandlen == 2 ? FLOPPY_COMMAND_RECALIBRATE : FLOPPY_COMMAND_INCOMPLETE;
    case 0x08:
        return FLOPPY_COMMAND_SENSE_INTERRUPT_STATUS;
    case 0x0f:
        return m_commandlen == 3 ? FLOPPY_COMMAND_SEEK : FLOPPY_COMMAND_INCOMPLETE;
    }

    switch (m_command[0] & 0x1f)
    {
    case 0x02:
        return m_commandlen == 9 ? FLOPPY_COMMAND_READ_TRACK : FLOPPY_COMMAND_INCOMPLETE;

    case 0x05:
    case 0x09:
        return m_commandlen == 9 ? FLOPPY_COMMAND_WRITE_DATA : FLOPPY_COMMAND_INCOMPLETE;

    case 0x06:
    case 0x0c:
        return m_commandlen == 9 ? FLOPPY_COMMAND_READ_DATA : FLOPPY_COMMAND_INCOMPLETE;

    case 0x0a:
        return m_commandlen == 2 ? FLOPPY_COMMAND_READ_ID : FLOPPY_COMMAND_INCOMPLETE;

    case 0x0d:
        return m_commandlen == 6 ? FLOPPY_COMMAND_FORMAT_TRACK : FLOPPY_COMMAND_INCOMPLETE;

    case 0x11:
        return m_commandlen == 9 ? FLOPPY_COMMAND_SCAN_EQUAL : FLOPPY_COMMAND_INCOMPLETE;

    case 0x19:
        return m_commandlen == 9 ? FLOPPY_COMMAND_SCAN_LOW : FLOPPY_COMMAND_INCOMPLETE;

    case 0x1d:
        return m_commandlen == 9 ? FLOPPY_COMMAND_SCAN_HIGH : FLOPPY_COMMAND_INCOMPLETE;

    default:
        return FLOPPY_COMMAND_INVALID;
    }
}

void CFloppyController::StartCommand(uint8_t cmd)
{
    m_commandlen = 0;
    m_resultlen = 0;  m_resultpos = 0;
    m_phase = FLOPPY_PHASE_EXEC;

    ExecuteCommand(cmd);
}

void CFloppyController::ExecuteCommand(uint8_t cmd)
{
    uint8_t drive = m_command[1] & 3;
    switch (cmd)
    {
    case FLOPPY_COMMAND_READ_DATA:
        if (m_okTrace) DebugLogFormat(_T("Floppy CMD READ_DATA C%02x H%02x R%02x N%02x EOT%02x GPL%02x DTL%02x\r\n"),
                    m_command[2], m_command[3], m_command[4], m_command[5], m_command[6], m_command[7], m_command[8]);
        //m_state = FLOPPY_STATE_READ_DATA;
        //TODO
        m_phase = FLOPPY_PHASE_RESULT;//DEBUG
        m_result[0] = 0x20;//TODO
        m_result[1] = 0;//TODO
        m_result[2] = 0;//TODO
        m_result[3] = m_command[2];
        m_result[4] = m_command[3];
        m_result[5] = m_command[4];
        m_result[6] = m_command[5];
        m_resultlen = 7;
        m_int = true;//DEBUG
        {
            uint8_t sector = m_command[4] - 1;
            while (true)
            {
                size_t offset = (m_command[2] * 2 + m_command[3]) * 5120 + sector * 512;
                int block = offset / 512;
                if (m_okTrace) DebugLogFormat(_T("Floppy CMD READ_DATA sent to buffer at pos 0x%06x block %d.\r\n"), offset, block);
                bool contflag = m_pBoard->FillHDBuffer(m_drivedata[0].data + offset);
                if (!contflag)
                    break;
                sector = (sector + 1) % 10;
            }
        }
        break;

        //case FLOPPY_COMMAND_READ_TRACK:
        //    if (m_okTrace) DebugLogFormat(_T("Floppy CMD READ_TRACK C%02x H%02x R%02x N%02x EOT%02x GPL%02x DTL%02x\r\n"),
        //                m_command[1], m_command[2], m_command[3], m_command[4], m_command[5], m_command[6], m_command[7]);
        //    m_state = FLOPPY_STATE_READ_TRACK;
        //    //TODO
        //    break;

    case FLOPPY_COMMAND_RECALIBRATE:
        if (m_okTrace) DebugLogFormat(_T("Floppy CMD RECALIBRATE 0x%02hx\r\n"), (uint16_t)m_command[1]);
        //TODO: m_state = FLOPPY_STATE_RECALIBRATE;
        m_phase = FLOPPY_PHASE_CMD;//DEBUG
        m_int = true;//DEBUG
        break;

    case FLOPPY_COMMAND_SEEK:
        if (m_okTrace) DebugLogFormat(_T("Floppy CMD SEEK 0x%02hx 0x%02hx\r\n"), (uint16_t)m_command[1], (uint16_t)m_command[2]);
        m_phase = FLOPPY_PHASE_CMD;//DEBUG
        m_int = true;//DEBUG
        break;

    case FLOPPY_COMMAND_SENSE_INTERRUPT_STATUS:
        if (m_okTrace) DebugLogFormat(_T("Floppy CMD SENSE_INTERRUPT\r\n"));
        m_phase = FLOPPY_PHASE_RESULT;
        m_result[0] = 0x10;//TODO
        m_result[1] = 0x00;//TODO
        m_resultlen = 2;
        m_int = false;
        break;

    case FLOPPY_COMMAND_SPECIFY:
        if (m_okTrace) DebugLogFormat(_T("Floppy CMD SPECIFY 0x%02hx 0x%02hx\r\n"), (uint16_t)m_command[1], (uint16_t)m_command[2]);
        //TODO
        m_phase = FLOPPY_PHASE_CMD;
        break;

    default:
        if (m_okTrace) DebugLogFormat(_T("Floppy CMD 0x%02hx not implemented\r\n"), (uint16_t)m_command[0]);
    }
}

void CFloppyController::Periodic()
{
    //if (!IsEngineOn()) return;  // Вращаем дискеты только если включен мотор

    //// Вращаем дискеты во всех драйвах сразу
    //for (int drive = 0; drive < 4; drive++)
    //{
    //    m_drivedata[drive].dataptr += 2;
    //    if (m_drivedata[drive].dataptr >= FLOPPY_RAWTRACKSIZE)
    //        m_drivedata[drive].dataptr = 0;
    //}

    //if (m_pDrive != nullptr && m_pDrive->dataptr == 0)
    //    DebugLogFormat(_T("Floppy Index\n"));

    // Далее обрабатываем чтение/запись на текущем драйве
    if (m_pDrive == nullptr) return;
    if (!IsAttached(m_drive)) return;

    //TODO
}

// Read track data from file and fill m_data
void CFloppyController::PrepareTrack()
{
    FlushChanges();

    if (m_pDrive == nullptr) return;

    //if (m_okTrace) DebugLogFormat(_T("Floppy PREP  %hu TR %hu SD %hu\r\n"), m_drive, m_track, m_side);

    m_trackchanged = false;
    //NOTE: Not changing m_pDrive->dataptr
    m_pDrive->datatrack = m_track;
    m_pDrive->dataside = m_side;
}

void CFloppyController::FlushChanges()
{
    if (m_drive == 0xff) return;
    if (!IsAttached(m_drive)) return;
    if (!m_trackchanged) return;

    if (m_okTrace) DebugLogFormat(_T("Floppy FLUSH %hu TR %hu SD %hu\r\n"), m_drive, m_pDrive->datatrack, m_pDrive->dataside);

    // Decode track data from m_data
    uint8_t data[5120];  memset(data, 0, 5120);
    bool decoded = true; //DecodeTrackData(m_pDrive->data, data);

    if (decoded)  // Write to the file only if the track was correctly decoded from raw data
    {
        // Track has 10 sectors, 512 bytes each; offset of the file is === ((Track<<1)+SIDE)*5120
        long foffset = ((m_pDrive->datatrack * 2) + (m_pDrive->dataside)) * 5120;

        // Check file length
        ::fseek(m_pDrive->fpFile, 0, SEEK_END);
        uint32_t currentFileSize = ::ftell(m_pDrive->fpFile);
        while (currentFileSize < (uint32_t)(foffset + 5120))
        {
            uint8_t datafill[512];  ::memset(datafill, 0, 512);
            uint32_t bytesToWrite = ((uint32_t)(foffset + 5120) - currentFileSize) % 512;
            if (bytesToWrite == 0) bytesToWrite = 512;
            ::fwrite(datafill, 1, bytesToWrite, m_pDrive->fpFile);
            //TODO: Проверка на ошибки записи
            currentFileSize += bytesToWrite;
        }

        // Save data into the file
        ::fseek(m_pDrive->fpFile, foffset, SEEK_SET);
        size_t dwBytesWritten = ::fwrite(&data, 1, 5120, m_pDrive->fpFile);
        //TODO: Проверка на ошибки записи
    }
    else
    {
        if (m_okTrace) DebugLog(_T("Floppy FLUSH FAILED\r\n"));
    }

    m_trackchanged = false;
}


//////////////////////////////////////////////////////////////////////
