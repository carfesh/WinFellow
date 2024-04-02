/*=========================================================================*/
/* Fellow Amiga Emulator                                                   */
/* WASAPI-based sound driver for Windows                                   */
/* Author: Torsten Enderling                                               */
/*                                                                         */
/* Copyright (C) 1991, 1992, 1996 Free Software Foundation, Inc.           */
/*                                                                         */
/* This program is free software; you can redistribute it and/or modify    */
/* it under the terms of the GNU General Public License as published by    */
/* the Free Software Foundation; either version 2, or (at your option)     */
/* any later version.                                                      */
/*                                                                         */
/* This program is distributed in the hope that it will be useful,         */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of          */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           */
/* GNU General Public License for more details.                            */
/*                                                                         */
/* You should have received a copy of the GNU General Public License       */
/* along with this program; if not, write to the Free Software Foundation, */
/* Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.          */
/*=========================================================================*/

/** @file
 *  WASAPI-based sound driver for Windows
 */

#include "WASAPISoundDriver.h"

void WASAPISoundDriver::Play(int16_t *left, int16_t *right, uint32_t sampleCount)
{
}

void WASAPISoundDriver::PollBufferPosition()
{
}

bool WASAPISoundDriver::SetCurrentSoundDeviceVolume(int volume)
{
  return volume;
}

bool WASAPISoundDriver::EmulationStart(SoundDriverRuntimeConfiguration runtimeConfiguration)
{
  return false;
}

void WASAPISoundDriver::EmulationStop()
{
}

bool WASAPISoundDriver::IsInitialized()
{
  return false;
}

WASAPISoundDriver::WASAPISoundDriver() : ISoundDriver()
{
}

WASAPISoundDriver::~WASAPISoundDriver()
{
}