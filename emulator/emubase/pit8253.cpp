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
#include "Board.h"


//////////////////////////////////////////////////////////////////////

PIT8253_chan::PIT8253_chan() :
    control(0), phase(0), value(0), count(0), latchvalue(0), gate(true), gateprev(true),
    writehi(false), readhi(false), output(false)
{
}

PIT8253::PIT8253() : m_chan()
{
}

void PIT8253::Write(uint8_t address, uint8_t byte)
{
    int channel = address & 3;
    if (channel == 3)
    {
        channel = byte >> 6;
        if (channel == 3)
            return;
        PIT8253_chan& chan = m_chan[channel];
        chan.control = byte & 0x3f;
        chan.writehi = chan.readhi = false;
        if ((chan.control & 0x30) == 0)  // latch mode
        {
            chan.latchvalue = chan.value;
        }
        else
        {
            chan.phase = 0;
            uint8_t mode = (chan.control >> 1) & 7;
            chan.output = (mode != 0);
        }
    }
    else
    {
        PIT8253_chan& chan = m_chan[channel];
        uint8_t access = (chan.control & 0x30) >> 4;
        switch (access)
        {
        case 1:  // read/write counter lo byte
            chan.count = byte;
            break;
        case 2:  // read/write counter hi byte
            chan.count = (chan.count & 0x00ff) | (byte << 8);
            break;
        case 3:  // read/write lo byte first, then hi byte
            if (chan.writehi)
                chan.count = (chan.count & 0x00ff) | (byte << 8);
            else
                chan.count = byte;
            chan.writehi = !chan.writehi;
            break;
        }
        uint8_t mode = (chan.control >> 1) & 7;
        switch (mode)
        {
        case 2: case 3:
            if (chan.phase == 0) chan.phase = 1;
            break;
        case 0: case 4:
            chan.phase = 1;
            break;
        }
    }
}

uint8_t PIT8253::Read(uint8_t address)
{
    int channel = address & 3;
    if (channel == 3)
        return 0;
    PIT8253_chan& chan = m_chan[channel];
    uint8_t access = (chan.control & 0x30) >> 4;
    switch (access)
    {
    case 1:
        return chan.value & 0xff;
    case 2:
        return chan.value >> 8;
    case 3:
        chan.readhi = !chan.readhi;
        return chan.readhi ? chan.value & 0xff : chan.value >> 8;
    default:  // latch mode
        chan.readhi = !chan.readhi;
        return chan.readhi ? chan.latchvalue & 0xff : chan.latchvalue >> 8;
    }
}

void PIT8253::Tick(uint8_t channel)
{
    PIT8253_chan& chan = m_chan[channel];
    uint8_t mode = (chan.control >> 1) & 7;
    switch (mode)
    {
    case 0:  // Interrupt on Terminal Count
        /*
        phase|output  |length  |value|next|comment
        -----+--------+--------+-----+----+----------------------------------
            0|low     |infinity|     |1   |waiting for count
            1|low     |1       |     |2   |internal delay when counter loaded
            2|low     |n       |n..1 |3   |counting down
            3|high    |infinity|0..1 |3   |counting down
        */
        if (chan.phase != 0)
        {
            if (chan.phase == 1)
            {
                chan.value = chan.count;
                chan.phase = 2;
                return;
            }
            if (chan.gate != 0)
            {
                if (chan.phase == 2)
                {
                    if (chan.value <= 1)
                    {
                        chan.phase = 3;
                        chan.value = 0;
                        chan.output = true;
                    }
                }

                chan.value--;
            }
        }
        break;
    case 1:  // Hardware Retriggerable One-Shot AKA Programmable One-Shot
        /*
        phase|output  |length  |value|next|comment
        -----+--------+--------+-----+----+----------------------------------
            0|high    |infinity|     |1   |counting down
            1|high    |1       |     |2   |internal delay to load counter
            2|low     |n       |n..1 |3   |counting down
            3|high    |infinity|0..1 |3   |counting down
        */
        if (chan.phase == 0)  // Counting down
        {
            if (!chan.gateprev && chan.gate)  // gate rising-edge sensitive
                chan.phase = 1;
            chan.value--;
            return;
        }
        if (chan.phase == 1)  // counter load cycle, output goes low
        {
            chan.value = chan.count;
            chan.output = false;
            chan.phase = 2;
            chan.value--;
            return;
        }
        if (chan.phase == 2)  // counting down
        {
            if (chan.value == 0)  // counter wrapped, output goes high
            {
                chan.output = true;
                chan.phase = 3;
            }
            chan.value--;
            return;
        }
        // chan.phase == 3
        chan.value--;
        break;
    case 2:  // Rate Generator
        /*
        phase|output  |length  |value|next|comment
        -----+--------+--------+-----+----+----------------------------------
            0|high    |infinity|     |1   |waiting for count
            1|high    |1       |     |2   |internal delay to load counter
            2|high    |n       |n..2 |3   |counting down
            3|low     |1       |1    |2   |reload counter
        */
        if (!chan.gate)  // gate low or mode control write forces output high
        {
            chan.output = true;
            return;
        }
        if (chan.phase == 0)
        {
            chan.output = true;
            if (!chan.gateprev && chan.gate)  // gate rising reloads count and initiates counting
                chan.phase = 1;
            return;
        }
        if (chan.phase == 1)
        {
            chan.value = chan.count;
            chan.phase = 2;
            return;
        }
        if (chan.phase == 2)
        {
            if (chan.value <= 2)
                chan.phase = 3;
            else
                chan.value--;
            return;
        }
        // chan.phase == 3
        chan.output = false;
        chan.value = chan.count;
        chan.phase = 2;
        break;
    case 3:  // Square Wave Generator
        /*
        phase|output  |length  |value|next|comment
        -----+--------+--------+-----+----+----------------------------------
            0|high    |infinity|     |1   |waiting for count
            1|high    |1       |     |2   |internal delay to load counter
            2|high/low|n       |n..1 |2   |counting down, reload counter
        */
        if (!chan.gate || chan.phase == 0)
        {
            chan.output = true;
            return;
        }
        if (chan.phase == 1)
        {
            chan.value = chan.count;
            chan.phase = 2;
            return;
        }
        // chan.phase == 2
        if (chan.value > 1)
            chan.value--;
        else
            chan.value = chan.count;
        chan.output = (chan.value > chan.count / 2);
        break;
    case 4:  // Software Trigger Strobe
    case 5:  // Hardware Trigger Strobe
        /*
        phase|output  |length  |value|next|comment
        -----+--------+--------+-----+----+----------------------------------
            0|high    |infinity|0..1 |0   |waiting for count/counting down
            1|high    |1       |     |2   |internal delay when counter loaded
            2|high    |n       |n..1 |3   |counting down
            3|low     |1       |0    |0   |strobe
        */
        if (chan.gate == 0 && mode == 4)  // gate low in mode 4 disables counting
            return;
        if (chan.phase == 0)
        {
            if (!chan.gateprev && chan.gate)  // gate rising reloads count and initiates counting
                chan.phase = 1;
            return;
        }
        if (chan.phase == 1)
        {
            chan.phase = 2;
            chan.value = chan.count;
            return;
        }
        if (chan.phase == 2)
        {
            if (chan.value > 0)
                chan.value--;
            else  // counter has hit zero, set output to low
            {
                chan.output = false;
                chan.phase = 3;
            }
            return;
        }
        // chan.phase == 3
        chan.phase = 0;
        chan.value--;
        chan.output = true;
        break;
    }
}


//////////////////////////////////////////////////////////////////////

