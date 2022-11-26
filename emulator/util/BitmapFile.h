/*  This file is part of NEONBTL.
    NEONBTL is free software: you can redistribute it and/or modify it under the terms
of the GNU Lesser General Public License as published by the Free Software Foundation,
either version 3 of the License, or (at your option) any later version.
    NEONBTL is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU Lesser General Public License for more details.
    You should have received a copy of the GNU Lesser General Public License along with
NEONBTL. If not, see <http://www.gnu.org/licenses/>. */

// BitmapFile.h

#pragma once

//////////////////////////////////////////////////////////////////////


void BitmapFile_Init();
void BitmapFile_Done();

HBITMAP BitmapFile_LoadPngFromResource(LPCTSTR lpName);

// Save the image as .PNG file
bool BitmapFile_SavePngFile(
    const uint32_t* pBits,
    LPCTSTR sFileName,
    int screenWidth, int screenHeight);


//////////////////////////////////////////////////////////////////////
