/*=========================================================================*/
/* Fellow                                                                  */
/*                                                                         */
/* Floppy Emulation                                                        */
/*                                                                         */
/* Authors: Petter Schau                                                   */
/*          Torsten Enderling                                              */
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
 * The floppy module handles floppy disc drive emulation.
 * It supports the use of .adf files, and is able to handle gzip (via embedded
 * zlib code) and xdms (also embedded) compressed disc images.
 *
 * It contains experimental support for .ipf files originating from the C.A.P.S.
 * project <a href="http://www.softpres.org">Software Preservation Society</a>).
 * CAPS  * support is not yet fully functional, because timings are not emulated
 * correctly to support copy-protected ("flakey") images.
 *
 * CAPS support is only available for the 32 bit version, since no 64 bit version
 * of the library is available. CAPS support is only enabled, when the preprocessor
 * definition FELLOW_SUPPORT_CAPS is set - this should be disabled for 64 bit builds.
 *
 * @todo CAPS has been renamed to SPS, and a 64 bit version is available;
 *       update to a current version
 * @todo enhance timing for flakey image support
 */

#ifdef _FELLOW_DEBUG_CRT_MALLOC
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#endif

#include "fellow/api/defs.h"
#include "fellow/api/Services.h"
#include "fellow/application/Fellow.h"
#include "fellow/chipset/ChipsetInfo.h"
#include "FMEM.H"
#include "fellow/chipset/Floppy.h"
#include "fellow/application/HostRenderer.h"
#include "fellow/chipset/Graphics.h"
#include "fellow/chipset/Cia.h"
#include "fellow/scheduler/Scheduler.h"
#include "fellow/cpu/CpuModule.h"
#include "interrupt.h"
#include <sys/timeb.h>

#include "xdms.h"
#include "zlibwrap.h"

#ifdef FELLOW_SUPPORT_CAPS
#include "caps.h"
#endif

#ifdef RETRO_PLATFORM
#include "RetroPlatform.h"
#endif

using namespace fellow::api;

#define MFM_FILLB 0xaa
#define MFM_FILLL 0xaaaaaaaa
#define MFM_MASK 0x55555555
#define FLOPPY_INSERTED_DELAY 150

/* Andromeda Sequential assumes at least 640 bytes of gap */
/* 640 bytes is exactly the amount of bytes left over if */
/* the drive spins 5 times per second. */

#define FLOPPY_GAP_BYTES 720
#define FLOPPY_FAST_WORDS 32

/*---------------*/
/* Configuration */
/*---------------*/

floppyinfostruct floppy[4];     /* Info about drives */
BOOLE floppy_fast;              /* Select fast floppy transfer */
UBY tmptrack[20 * 1024 * 11];   /* Temporary track buffer */
floppyDMAinfostruct floppy_DMA; /* Info about a DMA transfer */
BOOLE floppy_DMA_started;       /* Disk DMA started */
BOOLE floppy_DMA_read;          /* DMA read or write */
BOOLE floppy_has_sync;

/*-----------------------------------*/
/* Disk registers and help variables */
/*-----------------------------------*/

ULO dsklen, dsksync, dskpt, dskbytr; /* Registers */
UWO adcon;
ULO diskDMAen; /* Write counter for dsklen */
UWO dskbyt_tmp = 0;
BOOLE dskbyt1_read = FALSE;
BOOLE dskbyt2_read = FALSE;

static UBY floppyBootBlockOFS[] = {0x44, 0x4f, 0x53, 0x00, 0xc0, 0x20, 0x0f, 0x19, 0x00, 0x00, 0x03, 0x70, 0x43, 0xfa, 0x00, 0x18, 0x4e, 0xae, 0xff, 0xa0, 0x4a, 0x80, 0x67, 0x0a, 0x20,
                                   0x40, 0x20, 0x68, 0x00, 0x16, 0x70, 0x00, 0x4e, 0x75, 0x70, 0xff, 0x60, 0xfa, 0x64, 0x6f, 0x73, 0x2e, 0x6c, 0x69, 0x62, 0x72, 0x61, 0x72, 0x79};

static UBY floppyBootBlockFFS[] = {
    0x44, 0x4F, 0x53, 0x01, 0xE3, 0x3D, 0x0E, 0x72, 0x00, 0x00, 0x03, 0x70, 0x43, 0xFA, 0x00, 0x3E, 0x70, 0x25, 0x4E, 0xAE, 0xFD, 0xD8, 0x4A, 0x80, 0x67, 0x0C, 0x22, 0x40, 0x08, 0xE9, 0x00, 0x06,
    0x00, 0x22, 0x4E, 0xAE, 0xFE, 0x62, 0x43, 0xFA, 0x00, 0x18, 0x4E, 0xAE, 0xFF, 0xA0, 0x4A, 0x80, 0x67, 0x0A, 0x20, 0x40, 0x20, 0x68, 0x00, 0x16, 0x70, 0x00, 0x4E, 0x75, 0x70, 0xFF, 0x4E, 0x75,
    0x64, 0x6F, 0x73, 0x2E, 0x6C, 0x69, 0x62, 0x72, 0x61, 0x72, 0x79, 0x00, 0x65, 0x78, 0x70, 0x61, 0x6E, 0x73, 0x69, 0x6F, 0x6E, 0x2E, 0x6C, 0x69, 0x62, 0x72, 0x61, 0x72, 0x79, 0x00, 0x00, 0x00,
};

//#define FLOPPY_LOG
#ifdef FLOPPY_LOG

char floppylogfilename[MAX_PATH];
FILE *floppylogfile = 0;

void floppyLogClear()
{
  remove(floppylogfilename);
}

void floppyLog(STR *msg)
{
  if (floppylogfile == 0)
  {
    floppylogfile = fopen(floppylogfilename, "a");
    if (floppylogfile == 0) return;
  }
  fputs(msg, floppylogfile);
}

void floppyLogDMARead(ULO drive, ULO track, ULO side, ULO length, ULO ticks)
{
  STR msg[256];
  sprintf(
      msg,
      "DMA Read Started: FrameNo=%I64u Y=%.3u X=%.3u Drive=%u Track=%u Side=%u Pt=%.8X Length=%u Ticks=%u PC=%.6X\n",
      busGetRasterFrameCount(),
      busGetRasterY(),
      busGetRasterX(),
      drive,
      track,
      side,
      dskpt,
      length,
      ticks,
      cpuGetPC());
  floppyLog(msg);
}

void floppyLogStep(ULO drive, ULO from, ULO to)
{
  STR msg[256];
  sprintf(msg, "Step: FrameNo=%I64u Y=%.3u X=%.3u Drive %u from track %u to %u PC=%.6X\n", busGetRasterFrameCount(), busGetRasterY(), busGetRasterX(), drive, from, to, cpuGetPC());
  floppyLog(msg);
}

void floppyLogValue(STR *text, ULO v)
{
  STR msg[256];
  sprintf(msg, "%s: FrameNo=%I64u Y=%.3u X=%.3u Value=%.8X PC=%.6X\n", text, busGetRasterFrameCount(), busGetRasterY(), busGetRasterX(), v, cpuGetPC());
  floppyLog(msg);
}

void floppyLogValueWithTicks(STR *text, ULO v, ULO ticks)
{
  STR msg[256];
  sprintf(msg, "%s: FrameNo=%I64u Y=%.3u X=%.3u Value=%.8X Ticks=%.5u PC=%.6X\n", text, busGetRasterFrameCount(), busGetRasterY(), busGetRasterX(), v, ticks, cpuGetPC());
  floppyLog(msg);
}

void floppyLogMessageWithTicks(STR *text, ULO ticks)
{
  STR msg[256];
  sprintf(msg, "%s: FrameNo=%I64u Y=%.3u X=%.3u Ticks=%.5u PC=%.6X\n", text, busGetRasterFrameCount(), busGetRasterY(), busGetRasterX(), ticks, cpuGetPC());
  floppyLog(msg);
}

#endif

/*=======================*/
/* Register access stubs */
/*=======================*/

/*
=====
ADCON
=====

$dff09e - Read from $dff010

Paula
*/

UWO radcon(ULO address)
{
  return adcon;
}

void wadcon(UWO data, ULO address)
{
  if (data & 0x8000)
  {
    adcon = adcon | (data & 0x7fff);
  }
  else
  {
    adcon = adcon & ~(data & 0x7fff);
  }
#ifdef FLOPPY_LOG
  {
    STR msg[256];
    sprintf(msg, "adcon (wait for disksync %.4X is %s)", dsksync, (adcon & 0x0400) ? "enabled" : "disabled");
    floppyLogValue(msg, adcon);
  }
#endif
}

/*----------*/
/* DSKBYTR  */
/* $dff01a  */
/*----------*/

UWO rdskbytr(ULO address)
{
  UWO tmp = (UWO)(floppy_DMA_started << 14);
  ULO currentX = scheduler.GetRasterX();
  if (dsklen & 0x4000) tmp |= 0x2000;
  if (floppy_has_sync) tmp |= 0x1000;
  if (currentX < 114 && !dskbyt1_read)
  {
    tmp |= 0x8000 | (dskbyt_tmp >> 8);
    dskbyt1_read = TRUE;
  }
  else if (currentX >= 114 && !dskbyt2_read)
  {
    tmp |= 0x8000 | (dskbyt_tmp & 0xff);
    dskbyt2_read = TRUE;
  }
  return tmp;
}

/*----------*/
/* DSKPTH   */
/* $dff020  */
/*----------*/

void wdskpth(UWO data, ULO address)
{
  dskpt = chipsetReplaceHighPtr(dskpt, data);

#ifdef FLOPPY_LOG
  floppyLogValue("dskpth", dskpt);
#endif
}

/*----------*/
/* DSKPTL   */
/* $dff022  */
/*----------*/

void wdskptl(UWO data, ULO address)
{
  dskpt = chipsetReplaceLowPtr(dskpt, data);

#ifdef FLOPPY_LOG
  floppyLogValue("dskptl", dskpt);
#endif
}

/*----------*/
/* DSKLEN   */
/* $dff024  */
/*----------*/
void floppyClearDMAState();
void wdsklen(UWO data, ULO address)
{
  dsklen = data;

#ifdef FLOPPY_LOG
  floppyLogValue("dsklen", dsklen);
#endif

  if (data & 0x8000)
  {
    if (++diskDMAen >= 2)
    {
      floppyDMAStart();
    }
  }
  else
  {
    // DMA is off
    if (floppy_DMA_started)
    {
#ifdef FLOPPY_LOG
      floppyLogValue("DMA was stopped with words left", floppy_DMA.wordsleft);
#endif
      floppyClearDMAState();
    }
    diskDMAen = 0;
  }
}

/*----------*/
/* DSKSYNC  */
/* $dff07e  */
/*----------*/

void wdsksync(UWO data, ULO address)
{
  dsksync = data;

#ifdef FLOPPY_LOG
  floppyLogValue("dsksync", dsksync);
#endif
}

/*==================================*/
/* Some functions used from cia.c   */
/*==================================*/

/* Several drives can be selected, we simply return the first */

LON floppySelectedGet()
{
  LON i = 0;
  while (i < 4)
  {
    if (floppy[i].enabled && floppy[i].sel)
    {
      return i;
    }
    i++;
  }
  return -1;
}

void floppySelectedSet(ULO selbits)
{
  for (ULO i = 0; i < 4; i++)
  {
    if (floppy[i].enabled)
    {
      floppy[i].sel = ((selbits & 1) == 0);
    }
    selbits >>= 1;
  }
}

BOOLE floppyIsTrack0(ULO drive)
{
  if (drive != -1)
  {
    if (floppy[drive].enabled)
    {
      return (floppy[drive].track == 0);
    }
  }
  return FALSE;
}

BOOLE floppyIsWriteProtectedConfig(ULO drive)
{
  if (drive != -1)
  {
    if (floppy[drive].enabled)
    {
      return floppy[drive].writeprotconfig;
    }
  }
  return FALSE;
}

BOOLE floppyIsWriteProtectedEnforced(ULO drive)
{
  if (drive != -1)
  {
    if (floppy[drive].enabled)
    {
      return floppy[drive].writeprotenforce;
    }
  }
  return FALSE;
}

/* write-protection can result from several states */
/* the user may have configured write-protection in the fellow config */
/* the floppy module can enforce write-protection if the file cannot be written to or is an IPF image */

BOOLE floppyIsWriteProtected(ULO drive)
{
  return (floppyIsWriteProtectedConfig(drive) || floppyIsWriteProtectedEnforced(drive));
}

BOOLE floppyIsReady(ULO drive)
{
  if (drive != -1)
  {
    if (floppy[drive].enabled)
    {
      if (floppy[drive].idmode)
      {
        return (floppy[drive].idcount++ < 32);
      }
      else
      {
        return (floppy[drive].motor && floppy[drive].inserted);
      }
    }
  }
  return FALSE;
}

BOOLE floppyIsChanged(ULO drive)
{
  if (drive != -1)
  {
    if (floppy[drive].enabled)
    {
      return floppy[drive].changed;
    }
  }
  return FALSE;
}

// If motor is turned off, idmode is reset and on
// If motor is turned on, idmode is off
// Cia detects the change in SEL from high to low and calls this when needed
// Note, motor is active low

void floppyMotorSet(ULO drive, bool mtr)
{
  F_ASSERT(drive < 4);

  if (!floppy[drive].enabled)
  {
    return;
  }

  if (floppy[drive].motor && mtr)
  {
    floppy[drive].idmode = TRUE;
    floppy[drive].idcount = 0;
  }
  else if (!floppy[drive].motor && !mtr)
  {
    floppy[drive].idmode = FALSE;
    floppy[drive].motor_ticks = 0;
  }
  if (floppy[drive].motor != (!mtr))
  {
    Service->HUD.NotifyFloppyLEDChanged(drive, !mtr, !floppy_DMA_read);
  }
  floppy[drive].motor = !mtr;
}

void floppySideSet(BOOLE s)
{
  for (ULO i = 0; i < 4; i++)
  {
    if (floppy[i].enabled)
    {
      floppy[i].side = !s;
    }
  }
}

void floppyDirSet(BOOLE dr)
{
  for (ULO i = 0; i < 4; i++)
  {
    if (floppy[i].enabled)
    {
      floppy[i].dir = dr;
    }
  }
}

/** Move the floppy head to a given position (step).
 */
void floppyStepSet(BOOLE stp)
{
  for (ULO i = 0; i < 4; i++)
  {
    if (!floppy[i].enabled)
    {
      continue;
    }

    if (floppy[i].sel)
    {
      if (!stp && floppy[i].changed && floppy[i].inserted && ((Draw.GetFramecounter() - floppy[i].insertedframe) > FLOPPY_INSERTED_DELAY))
      {
        floppy[i].changed = FALSE;
      }
      if (!floppy[i].step && !stp)
      {
        if (!floppy[i].dir)
        {
          if (floppy[i].track < floppy[i].tracks + 3)
          {
#ifdef FLOPPY_LOG
            floppyLogStep(i, floppy[i].track, floppy[i].track + 1);
#endif
            floppy[i].track++;

#ifdef RETRO_PLATFORM
            if (RP.GetHeadlessMode()) RP.PostFloppyDriveSeek(i, floppy[i].track);
#endif
          }
        }
        else
        {
          if (floppy[i].track > 0)
          {
#ifdef FLOPPY_LOG
            floppyLogStep(i, floppy[i].track, floppy[i].track - 1);
#endif
            floppy[i].track--;

#ifdef RETRO_PLATFORM
            if (RP.GetHeadlessMode()) RP.PostFloppyDriveSeek(i, floppy[i].track);
#endif
          }
        }
      }
      floppy[i].step = !stp;
    }
  }
}

/*=============================================*/
/* Will MFM encode one sector from src to dest */
/*=============================================*/

void floppySectorMfmEncode(ULO tra, ULO sec, const UBY *src, UBY *dest, ULO sync)
{
  ULO hck = 0, dck = 0;

  /* Preamble and sync */

  *(dest + 0) = 0xaa;
  *(dest + 1) = 0xaa;
  *(dest + 2) = 0xaa;
  *(dest + 3) = 0xaa;
  *(dest + 4) = (UBY)(sync >> 8);
  *(dest + 5) = (UBY)(sync & 0xff);
  *(dest + 6) = (UBY)(sync >> 8);
  *(dest + 7) = (UBY)(sync & 0xff);

  /* Track and sector info */

  ULO tmp = 0xff000000 | (tra << 16) | (sec << 8) | (11 - sec);
  ULO even = (tmp & MFM_MASK);
  ULO odd = ((tmp >> 1) & MFM_MASK);
  *(dest + 8) = (UBY)((odd & 0xff000000) >> 24);
  *(dest + 9) = (UBY)((odd & 0xff0000) >> 16);
  *(dest + 10) = (UBY)((odd & 0xff00) >> 8);
  *(dest + 11) = (UBY)(odd & 0xff);
  *(dest + 12) = (UBY)((even & 0xff000000) >> 24);
  *(dest + 13) = (UBY)((even & 0xff0000) >> 16);
  *(dest + 14) = (UBY)((even & 0xff00) >> 8);
  *(dest + 15) = (UBY)(even & 0xff);

  /* Fill unused space */

  for (ULO x = 16; x < 48; x++)
  {
    *(dest + x) = MFM_FILLB;
  }

  /* Encode data section of sector */

  for (ULO x = 64; x < 576; x++)
  {
    tmp = *(src + x - 64);
    odd = (tmp & 0x55);
    even = (tmp >> 1) & 0x55;
    *(dest + x) = (UBY)(even | MFM_FILLB);
    *(dest + x + 512) = (UBY)(odd | MFM_FILLB);
  }

  /* Calculate checksum for unused space */

  for (ULO x = 8; x < 48; x += 4)
  {
    hck ^= (((ULO) * (dest + x)) << 24) | (((ULO) * (dest + x + 1)) << 16) | (((ULO) * (dest + x + 2)) << 8) | ((ULO) * (dest + x + 3));
  }
  even = odd = hck;
  odd >>= 1;
  even |= MFM_FILLL;
  odd |= MFM_FILLL;
  *(dest + 48) = (UBY)((odd & 0xff000000) >> 24);
  *(dest + 49) = (UBY)((odd & 0xff0000) >> 16);
  *(dest + 50) = (UBY)((odd & 0xff00) >> 8);
  *(dest + 51) = (UBY)(odd & 0xff);
  *(dest + 52) = (UBY)((even & 0xff000000) >> 24);
  *(dest + 53) = (UBY)((even & 0xff0000) >> 16);
  *(dest + 54) = (UBY)((even & 0xff00) >> 8);
  *(dest + 55) = (UBY)(even & 0xff);

  /* Calculate checksum for data section */

  for (ULO x = 64; x < 1088; x += 4)
  {
    dck ^= (((ULO) * (dest + x)) << 24) | (((ULO) * (dest + x + 1)) << 16) | (((ULO) * (dest + x + 2)) << 8) | ((ULO) * (dest + x + 3));
  }
  even = odd = dck;
  odd >>= 1;
  even |= MFM_FILLL;
  odd |= MFM_FILLL;
  *(dest + 56) = (UBY)((odd & 0xff000000) >> 24);
  *(dest + 57) = (UBY)((odd & 0xff0000) >> 16);
  *(dest + 58) = (UBY)((odd & 0xff00) >> 8);
  *(dest + 59) = (UBY)(odd & 0xff);
  *(dest + 60) = (UBY)((even & 0xff000000) >> 24);
  *(dest + 61) = (UBY)((even & 0xff0000) >> 16);
  *(dest + 62) = (UBY)((even & 0xff00) >> 8);
  *(dest + 63) = (UBY)(even & 0xff);
}

void floppyGapMfmEncode(UBY *dst)
{
  for (ULO i = 0; i < FLOPPY_GAP_BYTES; i++)
  {
    *dst++ = MFM_FILLB;
  }
}

/*===========================================================*/
/* Decode one MFM sector from src to dst                     */
/* Warning: Amiga OS appears to tag the gap with a sync..?   */
/* src is pointing after sync words on entry                 */
/* Returns the sector number found in the MFM encoded header */
/*===========================================================*/

ULO floppySectorMfmDecode(const UBY *src, UBY *dst, ULO track)
{
  ULO odd = (src[0] << 24) | (src[1] << 16) | (src[2] << 8) | src[3];
  ULO even = (src[4] << 24) | (src[5] << 16) | (src[6] << 8) | src[7];
  even &= MFM_MASK;
  odd = (odd & MFM_MASK) << 1;
  even |= odd;
  ULO src_ff = (even & 0xff000000) >> 24;
  ULO src_sector = (even & 0xff00) >> 8;
  ULO src_track = (even & 0xff0000) >> 16;
  if ((src_ff != 0xff) || (src_sector > 10) || (src_track != track))
  {
    return 0xffffffff;
  }
  src += (48 + 8);
  for (ULO i = 0; i < 512; i++)
  {
    even = (*(src + i)) & MFM_MASK;
    odd = (*(src + i + 512)) & MFM_MASK;
    *(dst++) = (UBY)((even << 1) | odd);
  }
  return src_sector;
}

/*=================================================================*/
/* Save one sector to disk and cache                               */
/* mfmsrc points to after the sync words somewhere in Amiga memory */
/* It uses tmptrack as scratchpad                                  */
/* returns TRUE if this really was a sector                        */
/*=================================================================*/

BOOLE floppySectorSave(ULO drive, ULO track, UBY *mfmsrc)
{
  if (!floppyIsWriteProtected(drive))
  {
    ULO sector = floppySectorMfmDecode(mfmsrc, tmptrack, track);
    if (sector < 11)
    {
      fseek(floppy[drive].F, floppy[drive].trackinfo[track].file_offset + sector * 512, SEEK_SET);
      fwrite(tmptrack, 1, 512, floppy[drive].F);

      // Problem with keeping the MFM for ADFs is that the sector sequence could be anything and they are enumerated. Re-encode from plain data.
      floppySectorMfmEncode(track, sector, tmptrack, floppy[drive].trackinfo[track].mfm_data + sector * 1088, 0x4489);
    }
    else
    {
      return FALSE;
    }
  }
  return TRUE;
}

/** Set read-only enforced flag for a drive.
 */
static void floppySetReadOnlyEnforced(ULO drive, bool enforce)
{
  floppy[drive].writeprotenforce = enforce;

#ifdef RETRO_PLATFORM
  if (RP.GetHeadlessMode()) RP.SendFloppyDriveReadOnly(drive, enforce);
#endif
}

/*===============================*/
/* Make one track of MFM data    */
/* Track is MFM tracks (0 - 163) */
/*===============================*/

void floppyTrackMfmEncode(ULO track, UBY *src, UBY *dst, ULO sync)
{
  for (ULO i = 0; i < 11; i++)
  {
    floppySectorMfmEncode(track, i, src + i * 512, dst + i * 1088, sync);
  }
  floppyGapMfmEncode(dst + 11 * 1088);
}

/*=======================================================*/
/* Load AmigaDOS track from disk into the track buffer   */
/* Track is MFM tracks (0 - 163)                         */
/*=======================================================*/

void floppyTrackLoad(ULO drive, ULO track)
{
  fseek(floppy[drive].F, track * 5632, SEEK_SET);
  fread(tmptrack, 1, 5632, floppy[drive].F);
  floppyTrackMfmEncode(track, tmptrack, floppy[drive].trackinfo[track].mfm_data, 0x4489);
}

/*======================*/
/* Set error conditions */
/*======================*/

void floppyError(ULO drive, FLOPPY_ERROR_CODE errorID)
{
  floppy[drive].imagestatus = FLOPPY_STATUS_CODE::FLOPPY_STATUS_ERROR;
  floppy[drive].imageerror = errorID;
  floppy[drive].inserted = FALSE;
  if (floppy[drive].F != nullptr)
  {
    fclose(floppy[drive].F);
    floppy[drive].F = nullptr;
  }
}

/** Write the current date/time into the floppy disk buffer.
 */
static void floppyWriteDiskDate(UBY *strBuffer)
{
  struct timeb time
  {
  };
  const time_t timediff = ((8 * 365 + 2) * (24 * 60 * 60)) * (time_t)1000;
  const time_t msecs_per_day = 24 * 60 * 60 * 1000;

  ftime(&time);

  time_t sec = time.time;
  sec -= time.timezone * 60;
  if (time.dstflag) sec += 3600;

  time_t t = sec * 1000 + time.millitm - timediff;

  if (t < 0) t = 0;

  time_t days = t / msecs_per_day;
  t -= days * msecs_per_day;
  time_t mins = t / (60 * 1000);
  t -= mins * (60 * 1000);
  time_t ticks = t / (1000 / 50);

  memoryWriteLongToPointer((ULO)days, strBuffer);
  memoryWriteLongToPointer((ULO)mins, strBuffer + 4);
  memoryWriteLongToPointer((ULO)ticks, strBuffer + 8);
}

/** Write the checksum into the floppy disk buffer.
 */
static void floppyWriteDiskChecksum(const UBY *strBuffer, UBY *strChecksum)
{
  ULO lChecksum = 0;

  for (int i = 0; i < 512; i += 4)
  {
    const UBY *p = strBuffer + i;
    lChecksum += memoryReadLongFromPointer(p);
  }

  lChecksum = ~lChecksum + 1;

  memoryWriteLongToPointer(lChecksum, strChecksum);
}

bool floppyValidateAmigaDOSVolumeName(const STR *strVolumeName)
{
  const STR *strIllegalVolumeNames[7] = {"SYS", "DEVS", "LIBS", "FONTS", "C", "L", "S"};
  STR strIllegalCharacters[2] = {':', '/'};

  if (strVolumeName == nullptr) return false;

  if (strVolumeName[0] == '\0') return false;

  if (strlen(strVolumeName) > 30) return false;

  for (int i = 0; i < 2; i++)
    if (strchr(strVolumeName, strIllegalCharacters[i]) != nullptr) return false;

  for (int i = 0; i < 7; i++)
    if (stricmp(strVolumeName, strIllegalVolumeNames[i]) == 0) return false;

  return true;
}

static void floppyWriteDiskBootblock(UBY *strCylinderContent, bool bFFS, bool bBootable)
{
  strcpy((char *)strCylinderContent, "DOS");
  strCylinderContent[3] = bFFS ? 1 : 0;

  if (bBootable) memcpy(strCylinderContent, bFFS ? floppyBootBlockFFS : floppyBootBlockOFS, bFFS ? sizeof(floppyBootBlockFFS) : sizeof(floppyBootBlockOFS));
}

static void floppyWriteDiskRootBlock(UBY *strCylinderContent, ULO lBlockIndex, const UBY *strVolumeLabel)
{
  strCylinderContent[0 + 3] = 2;
  strCylinderContent[12 + 3] = 0x48;
  strCylinderContent[312] = strCylinderContent[313] = strCylinderContent[314] = strCylinderContent[315] = 0xff;
  strCylinderContent[316 + 2] = (lBlockIndex + 1) >> 8;
  strCylinderContent[316 + 3] = (lBlockIndex + 1) & 255;
  strCylinderContent[432] = (UBY)strlen((char *)strVolumeLabel);
  strcpy((char *)strCylinderContent + 433, (const char *)strVolumeLabel);
  strCylinderContent[508 + 3] = 1;
  floppyWriteDiskDate(strCylinderContent + 420);
  memcpy(strCylinderContent + 472, strCylinderContent + 420, 3 * 4);
  memcpy(strCylinderContent + 484, strCylinderContent + 420, 3 * 4);
  floppyWriteDiskChecksum(strCylinderContent, strCylinderContent + 20);
  memset(strCylinderContent + 512 + 4, 0xff, 2 * lBlockIndex / 8);
  strCylinderContent[512 + 0x72] = 0x3f;
  floppyWriteDiskChecksum(strCylinderContent + 512, strCylinderContent + 512);
}

bool floppyImageADFCreate(STR *strImageFilename, STR *strVolumeLabel, bool bFormat, bool bBootable, bool bFFS)
{
  bool bResult = false;
  ULO lImageSize = 2 * 11 * 80 * 512; // 2 tracks per cylinder, 11 sectors per track, 80 cylinders, 512 bytes per sector
  ULO lCylinderSize = 2 * 11 * 512;   // 2 tracks per cylinder, 11 sectors per track, 512 bytes per sector

  if (bFormat)
  {
    if (!floppyValidateAmigaDOSVolumeName(strVolumeLabel)) return false;
  }

  FILE *f = fopen(strImageFilename, "wb");

  if (f)
  {
    UBY *strCylinderContent = (UBY *)malloc(lCylinderSize);

    if (strCylinderContent)
    {
      for (ULO i = 0; i < lImageSize; i += lCylinderSize)
      {
        memset(strCylinderContent, 0, lCylinderSize);

        if (bFormat)
        {
          if (i == 0)
            floppyWriteDiskBootblock(strCylinderContent, bFFS, bBootable);
          else if (i == lImageSize / 2)
            floppyWriteDiskRootBlock(strCylinderContent, lImageSize / 1024, (UBY *)strVolumeLabel);
        }

        fwrite(strCylinderContent, lCylinderSize, 1, f);
      }

      fclose(f);
      free(strCylinderContent);
      bResult = TRUE;
    }
  }

  return bResult;
}

/*===============================*/
/* Handling of Compressed Images */
/*===============================*/

/*=========================*/
/* Uncompress a BZip image */
/*=========================*/

BOOLE floppyImageCompressedBZipPrepare(const STR *diskname, ULO drive)
{
  char *gzname = Service->Fileops.GetTemporaryFilename();

  if (gzname == nullptr)
  {
    floppyError(drive, FLOPPY_ERROR_CODE::FLOPPY_ERROR_COMPRESS_TMPFILEOPEN);
    return FALSE;
  }

  STR cmdline[512];
  sprintf(cmdline, "bzip2.exe -k -d -s -c %s > %s", diskname, gzname);
  system(cmdline);
  strcpy(floppy[drive].imagenamereal, gzname);
  free(gzname);
  floppy[drive].zipped = TRUE;
  return TRUE;
}

/*========================*/
/* Uncompress a DMS image */
/*========================*/

BOOLE floppyImageCompressedDMSPrepare(const STR *diskname, ULO drive)
{
  char *gzname;

  if ((gzname = Service->Fileops.GetTemporaryFilename()) == nullptr)
  {
    floppyError(drive, FLOPPY_ERROR_CODE::FLOPPY_ERROR_COMPRESS_TMPFILEOPEN);
    return FALSE;
  }

  USHORT result = dmsUnpack((char *)diskname, gzname);
  if (result != 0)
  {
    STR szErrorMessage[1024] = "";

    dmsErrMsg(result, (char *)diskname, gzname, (char *)szErrorMessage);

    Service->Log.AddLogRequester(FELLOW_REQUESTER_TYPE::FELLOW_REQUESTER_TYPE_ERROR, "ERROR extracting DMS floppy image: %s", szErrorMessage);

    free(gzname);
    return FALSE;
  }

  strcpy(floppy[drive].imagenamereal, gzname);
  free(gzname);
  floppy[drive].zipped = TRUE;
  return TRUE;
}

/*============================*/
/* Uncompress a GZipped image */
/*============================*/

BOOLE floppyImageCompressedGZipPrepare(const STR *diskname, ULO drive)
{
  char *gzname;
  if ((gzname = Service->Fileops.GetTemporaryFilename()) == nullptr)
  {
    floppyError(drive, FLOPPY_ERROR_CODE::FLOPPY_ERROR_COMPRESS_TMPFILEOPEN);
    return FALSE;
  }

  if (!gzUnpack(diskname, gzname))
  {
    free(gzname);
    return FALSE;
  }

  strcpy(floppy[drive].imagenamereal, gzname);
  free(gzname);
  floppy[drive].zipped = TRUE;
  if ((access(diskname, 2)) == -1)
  {
    floppySetReadOnlyEnforced(drive, true);
  }
  return TRUE;
}

/*=================================*/
/* Remove TMP image of zipped file */
/*=================================*/

void floppyImageCompressedRemove(ULO drive)
{
  if (floppy[drive].zipped)
  {
    if ((!floppyIsWriteProtected(drive)) && ((access(floppy[drive].imagename, 2)) != -1)) // file is not read-only
    {
      STR *dotptr = strrchr(floppy[drive].imagename, '.');
      if (dotptr != nullptr)
      {
        if ((stricmp(dotptr, ".gz") == 0) || (stricmp(dotptr, ".z") == 0) || (stricmp(dotptr, ".adz") == 0))
        {
          if (!gzPack(floppy[drive].imagenamereal, floppy[drive].imagename))
          {
            Service->Log.AddLog("floppyImageCompressedRemove(): Couldn't recompress file %s\n", floppy[drive].imagename);
          }
          else
          {
            Service->Log.AddLog("floppyImageCompressedRemove(): Succesfully recompressed file %s\n", floppy[drive].imagename);
          }
        }
      }
    }
    floppy[drive].zipped = FALSE;
    remove(floppy[drive].imagenamereal);
  }
}

/*============================*/
/* Uncompress an image to TMP */
/* Returns TRUE if image was  */
/* compressed.                */
/*============================*/

BOOLE floppyImageCompressedPrepare(const STR *diskname, ULO drive)
{
  const STR *dotptr = strrchr(diskname, '.');
  if (dotptr == nullptr)
  {
    return FALSE;
  }

  if ((stricmp(dotptr, ".bz2") == 0) || (stricmp(dotptr, ".bz") == 0))
  {
    return floppyImageCompressedBZipPrepare(diskname, drive);
  }

  if ((stricmp(dotptr, ".gz") == 0) || (stricmp(dotptr, ".z") == 0) || (stricmp(dotptr, ".adz") == 0))
  {
    return floppyImageCompressedGZipPrepare(diskname, drive);
  }

  if (stricmp(dotptr, ".dms") == 0)
  {
    return floppyImageCompressedDMSPrepare(diskname, drive);
  }
  return FALSE;
}

/*==============================*/
/* Remove disk image from drive */
/*==============================*/

void floppyImageRemove(ULO drive)
{
  if (floppy[drive].F != nullptr)
  {
    fclose(floppy[drive].F);
    floppy[drive].F = nullptr;
  }
  if (floppy[drive].imagestatus == FLOPPY_STATUS_CODE::FLOPPY_STATUS_NORMAL_OK || floppy[drive].imagestatus == FLOPPY_STATUS_CODE::FLOPPY_STATUS_EXTENDED_OK)
  {
    if (floppy[drive].zipped)
    {
      floppyImageCompressedRemove(drive);
    }
  }
#ifdef FELLOW_SUPPORT_CAPS
  else if (floppy[drive].imagestatus == FLOPPY_STATUS_CODE::FLOPPY_STATUS_IPF_OK)
  {
    capsUnloadImage(drive);
  }
#endif
#ifdef RETRO_PLATFORM
  if (RP.GetHeadlessMode())
  {
    RP.SendFloppyDriveContent(drive, "", floppyIsWriteProtected(drive) ? true : false);
  }
#endif
  floppy[drive].imagestatus = FLOPPY_STATUS_CODE::FLOPPY_STATUS_NONE;
  floppy[drive].inserted = FALSE;
  floppy[drive].changed = TRUE;
  floppySetReadOnlyEnforced(drive, false);
}

/*========================================================*/
/* Prepare a disk image, ie. uncompress and report errors */
/*========================================================*/

void floppyImagePrepare(const STR *diskname, ULO drive)
{
  if (!floppyImageCompressedPrepare(diskname, drive))
  {
    if (floppy[drive].imagestatus != FLOPPY_STATUS_CODE::FLOPPY_STATUS_ERROR)
    {
      strcpy(floppy[drive].imagenamereal, diskname);
      floppy[drive].zipped = FALSE;
    }
  }
}

/*=======================================*/
/* Check type of image, number of tracks */
/* Returns the image status              */
/*=======================================*/

FLOPPY_STATUS_CODE floppyImageGeometryCheck(fs_wrapper_object_info *fsnp, ULO drive)
{
  STR head[8];
  fread(head, 1, 8, floppy[drive].F);
  if (strncmp(head, "UAE--ADF", 8) == 0)
  {
    floppy[drive].imagestatus = FLOPPY_STATUS_CODE::FLOPPY_STATUS_EXTENDED_OK;
    floppy[drive].tracks = 80;
  }
  else if (strncmp(head, "UAE-1ADF", 8) == 0)
  {
    floppy[drive].imagestatus = FLOPPY_STATUS_CODE::FLOPPY_STATUS_EXTENDED2_OK;
    floppy[drive].tracks = 80;
  }
#ifdef FELLOW_SUPPORT_CAPS
  else if (strncmp(head, "CAPS", 4) == 0)
  {
    floppy[drive].imagestatus = FLOPPY_STATUS_CODE::FLOPPY_STATUS_IPF_OK;
    floppy[drive].tracks = 80;
  }
#endif
  else
  {
    floppy[drive].tracks = fsnp->size / 11264;
    if ((floppy[drive].tracks * 11264) != (ULO)fsnp->size)
    {
      floppyError(drive, FLOPPY_ERROR_CODE::FLOPPY_ERROR_SIZE);
    }
    else
    {
      floppy[drive].imagestatus = FLOPPY_STATUS_CODE::FLOPPY_STATUS_NORMAL_OK;
    }
    if (floppy[drive].track >= floppy[drive].tracks)
    {
      floppy[drive].track = 0;
    }
  }
  return floppy[drive].imagestatus;
}

/*======================*/
/* Load normal ADF file */
/*======================*/

void floppyImageNormalLoad(ULO drive)
{
  for (ULO i = 0; i < floppy[drive].tracks * 2; i++)
  {
    floppy[drive].trackinfo[i].file_offset = i * 5632;
    floppy[drive].trackinfo[i].mfm_length = 11968 + FLOPPY_GAP_BYTES;
    floppy[drive].trackinfo[i].mfm_data = floppy[drive].mfm_data + i * (11968 + FLOPPY_GAP_BYTES);
    floppyTrackLoad(drive, i);
  }
  floppy[drive].inserted = TRUE;
  floppy[drive].insertedframe = Draw.GetFramecounter();
}

/*========================*/
/* Load extended ADF file */
/*========================*/

void floppyImageExtendedLoad(ULO drive)
{
  ULO i;
  UBY tinfo[4];
  ULO syncs[160], lengths[160];

  /* read table from header containing sync and length words */
  fseek(floppy[drive].F, 8, SEEK_SET);
  for (i = 0; i < floppy[drive].tracks * 2; i++)
  {
    fread(tinfo, 1, 4, floppy[drive].F);
    syncs[i] = (((ULO)tinfo[0]) << 8) | ((ULO)tinfo[1]);
    lengths[i] = (((ULO)tinfo[2]) << 8) | ((ULO)tinfo[3]);
  }

  /* initial offset of track data in file, and in the internal mfm_buffer */
  ULO file_offset = floppy[drive].tracks * 8 + 8;
  ULO mfm_offset = 0;
  /* read the actual track data */
  fseek(floppy[drive].F, file_offset, SEEK_SET);
  for (i = 0; i < floppy[drive].tracks * 2; i++)
  {
    floppy[drive].trackinfo[i].mfm_data = floppy[drive].mfm_data + mfm_offset;
    floppy[drive].trackinfo[i].file_offset = file_offset;
    if (!syncs[i]) /* Sync = 0 means AmigaDOS tracks (Stored non-MFM encoded) */
    {
      floppy[drive].trackinfo[i].mfm_length = 11968 + FLOPPY_GAP_BYTES;
      mfm_offset += floppy[drive].trackinfo[i].mfm_length;
      fread(tmptrack, 1, lengths[i], floppy[drive].F);
      floppyTrackMfmEncode(i, tmptrack, floppy[drive].trackinfo[i].mfm_data, 0x4489);
    }
    else /* raw MFM tracks */
    {
      mfm_offset += lengths[i] + 2;
      floppy[drive].trackinfo[i].mfm_length = lengths[i] + 2;
      floppy[drive].trackinfo[i].mfm_data[0] = (UBY)(syncs[i] >> 8);
      floppy[drive].trackinfo[i].mfm_data[1] = (UBY)(syncs[i] & 0xff);
      fread(floppy[drive].trackinfo[i].mfm_data + 2, 1, lengths[i], floppy[drive].F);
    }
    file_offset += lengths[i];
  }
  floppy[drive].inserted = TRUE;
  floppy[drive].insertedframe = Draw.GetFramecounter();
}

#ifdef FELLOW_SUPPORT_CAPS
/*=======================*/
/* Load a CAPS IPF Image */
/*=======================*/

void floppyImageIPFLoad(ULO drive)
{
  UBY *LastTrackMFMData = floppy[drive].mfm_data;

  if (!capsLoadImage(drive, floppy[drive].F, &floppy[drive].tracks))
  {
    Service->Log.AddLog("floppyImageIPFLoad(): Unable to load CAPS IPF Image. Is the Plug-In installed correctly?\n");
    return;
  }

  for (ULO i = 0; i < floppy[drive].tracks * 2; i++)
  {
    ULO maxtracklength;
    floppy[drive].trackinfo[i].mfm_data = LastTrackMFMData;

    capsLoadTrack(drive, i, floppy[drive].trackinfo[i].mfm_data, &floppy[drive].trackinfo[i].mfm_length, &maxtracklength, floppy[drive].timebuf, &floppy[drive].flakey);
    LastTrackMFMData += maxtracklength;
    floppy[drive].trackinfo[i].file_offset = 0xffffffff; /* set file offset to something pretty invalid */
  }

  floppySetReadOnlyEnforced(drive, true);
  floppy[drive].inserted = TRUE;
  floppy[drive].insertedframe = Draw.GetFramecounter();
}
#endif

/** Insert an image into a floppy drive
 */
void floppySetDiskImage(ULO drive, const STR *diskname)
{
  bool bSuccess = false;

  if (floppy[drive].enabled)
  {
    Service->Log.AddLog("floppySetDiskImage(%u, '%s')...\n", drive, diskname);
  }

  if (strcmp(diskname, floppy[drive].imagename) == 0)
  {
    return; /* Same image */
  }

  floppyImageRemove(drive);

  if (strcmp(diskname, "") == 0)
  {
    floppy[drive].inserted = FALSE;
    floppy[drive].imagestatus = FLOPPY_STATUS_CODE::FLOPPY_STATUS_NONE;
    strcpy(floppy[drive].imagename, "");
  }
  else
  {
    fs_wrapper_object_info *fsnp = Service->FSWrapper.GetFSObjectInfo(diskname);
    if (fsnp == nullptr)
    {
      floppyError(drive, FLOPPY_ERROR_CODE::FLOPPY_ERROR_EXISTS_NOT);
    }
    else
    {
      if (fsnp->type != fs_wrapper_object_types::FILE)
      {
        floppyError(drive, FLOPPY_ERROR_CODE::FLOPPY_ERROR_FILE);
      }
      else
      {
        floppyImagePrepare(diskname, drive);

        if (floppy[drive].zipped)
        {
          delete fsnp;
          fsnp = Service->FSWrapper.GetFSObjectInfo(floppy[drive].imagenamereal);
          if (fsnp == nullptr)
          {
            floppyError(drive, FLOPPY_ERROR_CODE::FLOPPY_ERROR_COMPRESS);
          }
        }

        if (floppy[drive].imagestatus != FLOPPY_STATUS_CODE::FLOPPY_STATUS_ERROR)
        {
          if (!fsnp->writeable)
          {
            floppySetReadOnlyEnforced(drive, true);
          }

          if ((floppy[drive].F = fopen(floppy[drive].imagenamereal, (floppyIsWriteProtected(drive) ? "rb" : "r+b"))) == nullptr)
          {
            floppyError(drive, (floppy[drive].zipped) ? FLOPPY_ERROR_CODE::FLOPPY_ERROR_COMPRESS : FLOPPY_ERROR_CODE::FLOPPY_ERROR_FILE);
          }
          else
          {
            strcpy(floppy[drive].imagename, diskname);
            switch (floppyImageGeometryCheck(fsnp, drive))
            {
              case FLOPPY_STATUS_CODE::FLOPPY_STATUS_NORMAL_OK:
                floppyImageNormalLoad(drive);
                bSuccess = true;
                break;
              case FLOPPY_STATUS_CODE::FLOPPY_STATUS_EXTENDED_OK:
                floppyImageExtendedLoad(drive);
                bSuccess = true;
                break;
              case FLOPPY_STATUS_CODE::FLOPPY_STATUS_EXTENDED2_OK:
                Service->Log.AddLog("floppySetDiskImage(%u, '%s') ERROR: floppy image is in unsupported extended2 ADF format.\n", drive, diskname);
                break;
#ifdef FELLOW_SUPPORT_CAPS
              case FLOPPY_STATUS_CODE::FLOPPY_STATUS_IPF_OK:
                floppyImageIPFLoad(drive);
                bSuccess = true;
                break;
#endif
              default:
                // Error already set by floppyImageGeometryCheck()
                Service->Log.AddLog("floppySetDiskImage(%u, '%s') ERROR: unexpected floppy image geometry status.\n", drive, diskname);
                break;
            }
#ifdef RETRO_PLATFORM
            if (RP.GetHeadlessMode() && bSuccess)
            {
              RP.SendFloppyDriveContent(drive, diskname, floppyIsWriteProtected(drive) ? true : false);
            }
#endif
          }
        }
      }
      delete fsnp;
    }
  }
}

/*============================================================================*/
/* Set enabled flag for a drive                                               */
/*============================================================================*/

void floppySetEnabled(ULO drive, BOOLE enabled)
{
  floppy[drive].enabled = enabled;
}

/** Set read-only flag for a drive.
 */
void floppySetReadOnlyConfig(ULO drive, BOOLE readonly)
{
  if (floppy[drive].writeprotconfig != readonly)
  {
    floppy[drive].writeprotconfig = readonly;
    floppy[drive].changed = TRUE;
  }

#ifdef RETRO_PLATFORM
  if (RP.GetHeadlessMode()) RP.SendFloppyDriveReadOnly(drive, readonly ? true : false);
#endif
}

/*============================================================================*/
/* Set fast DMA flag                                                          */
/*============================================================================*/

void floppySetFastDMA(BOOLE fastDMA)
{
  floppy_fast = fastDMA;

#ifdef RETRO_PLATFORM
  if (RP.GetHeadlessMode()) RP.SendFloppyTurbo(fastDMA ? true : false);
#endif
}

/*========================*/
/* Initial drive settings */
/*========================*/

void floppyDriveTableInit()
{
  for (ULO i = 0; i < 4; i++)
  {
    floppy[i].F = nullptr;
    floppy[i].sel = FALSE;
    floppy[i].track = 0;
    floppy[i].writeprotconfig = FALSE;
    floppy[i].writeprotenforce = FALSE;
    floppy[i].dir = FALSE;
    floppy[i].motor = FALSE;
    floppy[i].motor_ticks = 0;
    floppy[i].side = FALSE;
    floppy[i].inserted = FALSE;
    floppy[i].insertedframe = 0;
    floppy[i].idmode = FALSE;
    floppy[i].idcount = 0;
    floppy[i].imagename[0] = '\0';
    floppy[i].imagenamereal[0] = '\0';
    floppy[i].imagestatus = FLOPPY_STATUS_CODE::FLOPPY_STATUS_NONE;
    floppy[i].zipped = FALSE;
    floppy[i].changed = TRUE;
    /* Need to be large enough to hold UAE--ADF encoded tracks */
    floppy[i].mfm_data = (UBY *)malloc(FLOPPY_TRACKS * 25000);
#ifdef FELLOW_SUPPORT_CAPS
    floppy[i].flakey = FALSE;
    floppy[i].timebuf = (ULO *)malloc(FLOPPY_TRACKS * 25000);
#endif
  }
}

void floppyDriveTableReset()
{
  for (ULO i = 0; i < 4; i++)
  {
    floppy[i].sel = FALSE;
    floppy[i].track = 0;
    floppy[i].dir = FALSE;
    floppy[i].motor = FALSE;
    floppy[i].motor_ticks = 0;
    floppy[i].side = FALSE;
    floppy[i].insertedframe = Draw.GetFramecounter() + FLOPPY_INSERTED_DELAY + 1;
    floppy[i].idmode = FALSE;
    floppy[i].idcount = 0;
  }
}

/*==================*/
/* Free memory used */
/*==================*/

void floppyMfmDataFree()
{
  for (ULO i = 0; i < 4; i++)
  {
    if (floppy[i].mfm_data != nullptr)
    {
      free(floppy[i].mfm_data);
    }
  }
}

#ifdef FELLOW_SUPPORT_CAPS
void floppyTimeBufDataFree()
{
  for (ULO i = 0; i < 4; i++)
  {
    if (floppy[i].timebuf != nullptr)
    {
      free(floppy[i].timebuf);
    }
  }
}
#endif

/*============================*/
/* Install IO register stubs  */
/*============================*/

void floppyIOHandlersInstall()
{
  memorySetIoReadStub(0x10, radcon);
  memorySetIoReadStub(0x1a, rdskbytr);
  memorySetIoWriteStub(0x20, wdskpth);
  memorySetIoWriteStub(0x22, wdskptl);
  memorySetIoWriteStub(0x24, wdsklen);
  memorySetIoWriteStub(0x7e, wdsksync);
  memorySetIoWriteStub(0x9e, wadcon);
}

void floppyIORegistersClear()
{
  dskpt = 0;
  dsklen = 0;
  dsksync = 0;
  adcon = 0;
  diskDMAen = 0;
}

void floppyClearDMAState()
{
  floppy_DMA_started = FALSE;
  floppy_DMA_read = TRUE;
  floppy_DMA.dskpt = 0;
  floppy_DMA.wait = 0;
  floppy_DMA.wordsleft = 0;
  floppy_DMA.wait_for_sync = FALSE;
  floppy_DMA.sync_found = FALSE;
}

BOOLE floppyDMAReadStarted()
{
  return floppy_DMA_started && floppy_DMA_read;
}

BOOLE floppyDMAWriteStarted()
{
  return floppy_DMA_started && !floppy_DMA_read;
}

BOOLE floppyDMAChannelOn()
{
  return (dmaconr & 0x0010) && (dsklen & 0x8000);
}

BOOLE floppyHasIndex(ULO sel_drv)
{
  F_ASSERT(sel_drv < 4);
  return floppy[sel_drv].motor_ticks == 0;
}

ULO floppyGetLinearTrack(ULO sel_drv)
{
  F_ASSERT(sel_drv < 4);
  return floppy[sel_drv].track * 2 + floppy[sel_drv].side;
}

BOOLE floppyIsSpinning(ULO sel_drv)
{
  F_ASSERT(sel_drv < 4);
  return floppy[sel_drv].motor && floppy[sel_drv].enabled && floppy[sel_drv].inserted;
}

bool floppyGetEnabled(unsigned int driveIndex)
{
  F_ASSERT(driveIndex < 4);
  return floppy[driveIndex].enabled;
}

bool floppyGetMotor(unsigned int driveIndex)
{
  F_ASSERT(driveIndex < 4);
  return floppy[driveIndex].motor;
}

unsigned int floppyGetTrack(unsigned int driveIndex)
{
  F_ASSERT(driveIndex < 4);
  return floppy[driveIndex].track;
}

bool floppyGetIsWriting(unsigned int driveIndex)
{
  return floppySelectedGet() == driveIndex && floppyDMAWriteStarted();
}

/*======================================================================*/
/* Initialize disk DMA reading                                          */
/* I'm having a problem with KS3.1, which will eventually write corrupt */
/* mfm-sectors if there is a gap in the mfm read from the disk.         */
/* I don't understand why this happens, but for now, get around the     */
/* problem by not using gap for disk accesses performed by the KS.      */
/*======================================================================*/

void floppyDMAReadInit(ULO drive)
{
  floppy_DMA_started = TRUE;
  floppy_DMA_read = TRUE;
  floppy_DMA.wordsleft = dsklen & 0x3fff;
  floppy_DMA.dskpt = dskpt;

  if (drive == -1)
  {
    return;
  }

  // Workaround, require normal sync with MFM generated from ADF. (North and South (?), Prince of Persia, Lemmings 2)
  static int count = 0;
  if (floppy[drive].imagestatus == FLOPPY_STATUS_CODE::FLOPPY_STATUS_NORMAL_OK && dsksync != 0 && dsksync != 0x4489 && dsksync != 0x8914)
  {
    if (count < 100)
    {
      Service->Log.AddLog("floppyDMAReadInit(): WARNING: unusual dsksync value encountered: 0x%x\n", dsksync);
      count++;
      if (count == 100)
      {
        Service->Log.AddLog("floppyDMAReadInit(): Too many warnings, disabling message.\n");
      }
    }
  }

  floppy_DMA.wait_for_sync = (adcon & 0x0400) && ((floppy[drive].imagestatus != FLOPPY_STATUS_CODE::FLOPPY_STATUS_NORMAL_OK && dsksync != 0) ||
                                                  (floppy[drive].imagestatus == FLOPPY_STATUS_CODE::FLOPPY_STATUS_NORMAL_OK && (dsksync == 0x4489 || dsksync == 0x8914 || dsksync == 0x4124)));

  floppy_DMA.sync_found = FALSE;
  floppy_DMA.dont_use_gap = ((cpuGetPC() & 0xf80000) == 0xf80000);

#ifdef FLOPPY_LOG
  floppyLogDMARead(drive, floppy[drive].track, floppy[drive].side, floppy_DMA.wordsleft, floppy[drive].motor_ticks);
#endif

  if (floppy_DMA.dont_use_gap && (floppy[drive].motor_ticks >= 11968))
  {
    floppy[drive].motor_ticks = 0;
  }
}

/*==========================================*/
/* Process a disk DMA write operation       */
/* Do it all in one piece, and delay irq.   */
/*==========================================*/

ULO floppyFindNextSync(ULO pos, LON length)
{
  ULO offset = pos;
  BOOLE is_sync = FALSE;
  BOOLE past_sync = FALSE;
  while ((length > 0) && (!past_sync))
  {
    BOOLE was_sync = is_sync;
    is_sync = (chipmemReadWord(offset) == 0x4489);
    past_sync = (was_sync && !is_sync);
    length -= 2;
    offset = chipsetMaskPtr(offset + 2);
  }
  return chipsetMaskPtr(offset - pos - ((past_sync) ? 2 : 0));
}

void floppyDMAWriteInit(LON drive)
{
  LON total_length = (dsklen & 0x3fff) * 2;
  LON length = total_length;
  ULO pos = dskpt;
  ULO track_lin;
  BOOLE ended;

  Service->HUD.NotifyFloppyLEDChanged(drive, true, true);

  if ((drive == -1) || !floppyDMAChannelOn())
  {
    ended = TRUE;
  }
  track_lin = floppyGetLinearTrack(drive);
  while (length > 0)
  {
    ULO next_after_sync_offset = floppyFindNextSync(pos, length);
    length -= next_after_sync_offset;
    pos += next_after_sync_offset;

    if (length >= 1080)
    {
      if (floppySectorSave(drive, track_lin, memory_chip + pos))
      {
        length -= 1080;
        pos += 1080;
      }
    }
    else
    {
      Service->Log.AddLog("Floppy write MFM ended with an incomplete sector.\n");
      break;
    }
  }
  floppy_DMA_read = FALSE;
  floppy_DMA.wait = (total_length / (floppy_fast ? FLOPPY_FAST_WORDS : 2)) + FLOPPY_WAIT_INITIAL;
  floppy_DMA_started = TRUE;
}

/*========================================*/
/* Called when the dsklen register        */
/* has been written twice                 */
/* Will initiate a disk DMA read or write */
/*========================================*/

void floppyDMAStart()
{
  if (dsklen & 0x4000)
  {
    floppyDMAWriteInit(floppySelectedGet());
  }
  else
  {
    floppyDMAReadInit(floppySelectedGet());
  }
  // diskDMAen = 0;
}

/*========================================================================*/
/* Data was copied when the write started, here we just delay the irq for */
/* some time.                                                             */
/*========================================================================*/

void floppyDMAWrite()
{
  if (--floppy_DMA.wait == 0)
  {
    floppy_DMA_started = FALSE;

#ifdef FLOPPY_LOG
    floppyLogMessageWithTicks(((intena & 0x4002) != 0x4002) ? "DSKDONEIRQ (Write, irq not enabled)" : "DSKDONEIRQ (Write, irq enabled)", floppy[floppySelectedGet()].motor_ticks);
#endif

    wintreq_direct(0x8002, 0xdff09c, true);
  }
}

/* Returns TRUE is sync is seen for the first time */
BOOLE floppyCheckSync(UWO word_under_head)
{
  if (adcon & 0x0400)
  {
    // ULO sync_to_use = (floppy[floppySelectedGet()].imagestatus == FLOPPY_STATUS_NORMAL_OK) ? 0x4489 : dsksync;
    ULO sync_to_use = dsksync;
    BOOLE word_is_sync = (word_under_head == sync_to_use);
    BOOLE found_sync = !floppy_has_sync && word_is_sync;
    if (found_sync)
    {

#ifdef FLOPPY_LOG
      floppyLogMessageWithTicks(((intena & 0x5000) != 0x5000) ? "DSKSYNCIRQ, IRQ not enabled" : "DSKSYNCIRQ, IRQ enabled", floppy[floppySelectedGet()].motor_ticks);
#endif

      wintreq_direct(0x9000, 0xdff09c, true);
    }
    floppy_has_sync = word_is_sync;
    return found_sync;
  }
  return FALSE; // Not using sync.
}

void floppyReadWord(UWO word_under_head, BOOLE found_sync)
{
  // This skips the first sync word
  if (found_sync && (floppy_DMA.wait_for_sync && !floppy_DMA.sync_found))
  {
    floppy_DMA.sync_found = TRUE;
  }
  else if (floppy_DMA.wait_for_sync && floppy_DMA.sync_found)
  {
    floppy_DMA.wait_for_sync = floppy_DMA.sync_found = FALSE;
  }
  if (floppyDMAChannelOn() && !floppy_DMA.wait_for_sync)
  {
    chipmemWriteWord(word_under_head, floppy_DMA.dskpt);
    floppy_DMA.dskpt = chipsetMaskPtr(floppy_DMA.dskpt + 2);
    floppy_DMA.wordsleft--;
    if (floppy_DMA.wordsleft == 0)
    {

#ifdef FLOPPY_LOG
      floppyLogMessageWithTicks(((intena & 0x4002) != 0x4002) ? "DSKDONEIRQ (Read, IRQ not enabled)" : "DSKDONEIRQ (Read, IRQ enabled)", floppy[floppySelectedGet()].motor_ticks);
#endif

      wintreq_direct(0x8002, 0xdff09c, true);
      floppy_DMA_started = FALSE;
    }
  }
}

UWO floppyGetByteUnderHead(ULO sel_drv, ULO track)
{
  if ((track / 2) >= floppy[sel_drv].tracks)
  {
    return rand() % 256;
  }
  return floppy[sel_drv].trackinfo[track].mfm_data[floppy[sel_drv].motor_ticks];
}

void floppyNextByte(ULO sel_drv, ULO track)
{
  ULO modulo;
#ifdef FELLOW_SUPPORT_CAPS
  ULO previous_motor_ticks = floppy[sel_drv].motor_ticks;
  if (floppy[sel_drv].imagestatus == FLOPPY_STATUS_CODE::FLOPPY_STATUS_IPF_OK)
  {
    modulo = floppy[sel_drv].trackinfo[track].mfm_length;
  }
  else
#endif
  {
    modulo = (floppyDMAReadStarted() && floppy_DMA.dont_use_gap) ? ((11968 < floppy[sel_drv].trackinfo[track].mfm_length) ? 11968 : floppy[sel_drv].trackinfo[track].mfm_length)
                                                                 : floppy[sel_drv].trackinfo[track].mfm_length;
  }
  if (modulo == 0)
  {
    modulo = 1;
  }
  floppy[sel_drv].motor_ticks = (floppy[sel_drv].motor_ticks + 1) % modulo;
  if (floppyHasIndex(sel_drv))
  {
    ciaRaiseIndexIRQ();
  }

#ifdef FELLOW_SUPPORT_CAPS
  if (previous_motor_ticks > floppy[sel_drv].motor_ticks)
  {
    if (floppy[sel_drv].imagestatus == FLOPPY_STATUS_CODE::FLOPPY_STATUS_IPF_OK && floppy[sel_drv].flakey)
    {
      ULO track = floppy[sel_drv].track;
      capsLoadNextRevolution(sel_drv, floppy[sel_drv].track, floppy[sel_drv].trackinfo[track].mfm_data, &floppy[sel_drv].trackinfo[track].mfm_length);
    }
  }
#endif
}

UWO prev_byte_under_head = 0;
void floppyEndOfLine()
{
  LON sel_drv = floppySelectedGet();
  if (floppyDMAWriteStarted())
  {
    floppyDMAWrite();
    floppy_has_sync = FALSE;
    return;
  }
  if (sel_drv == -1)
  {
    floppy_has_sync = FALSE;
    return;
  }
  if (floppyIsSpinning(sel_drv))
  {
    ULO track = floppyGetLinearTrack(sel_drv);
    ULO words = (floppy_fast) ? FLOPPY_FAST_WORDS : 2;
    for (ULO i = 0; i < words; i++)
    {
      UWO tmpb1 = floppyGetByteUnderHead(sel_drv, track);
      UWO word_under_head = (prev_byte_under_head << 8) | tmpb1;
      BOOLE found_sync = floppyCheckSync(word_under_head);
      floppyNextByte(sel_drv, track);

      if (!found_sync) // Sync was found on a byte boundary, skip reading ahead to align.
      {
        UWO tmpb2 = floppyGetByteUnderHead(sel_drv, track);
        floppyNextByte(sel_drv, track);
        word_under_head = (tmpb1 << 8) | tmpb2;
        found_sync |= floppyCheckSync(word_under_head);
      }
#ifdef FLOPPY_LOG
      else
      {
        floppyLogMessageWithTicks("Sync was found on byte boundary", floppy[sel_drv].motor_ticks);
      }
#endif

      prev_byte_under_head = word_under_head & 0xff;
      dskbyt_tmp = word_under_head;
      dskbyt1_read = FALSE;
      dskbyt2_read = FALSE;

      if (floppyDMAReadStarted())
      {
        floppyReadWord(word_under_head, found_sync);
      }
    }
  }
  else
  {
    floppy_has_sync = FALSE;
  }
}

/*===========================================================================*/
/* Top level disk-emulation initialization                                   */
/*===========================================================================*/

void floppyHardReset()
{
  floppyIORegistersClear();
  floppyClearDMAState();
  floppyDriveTableReset();
}

void floppyEmulationStart()
{
  floppyIOHandlersInstall();
}

void floppyEmulationStop()
{
}

void floppyStartup()
{
  floppyIORegistersClear();
  floppyClearDMAState();
  floppyDriveTableInit();
#ifdef FLOPPY_LOG
  Service->Fileops.GetGenericFileName(floppylogfilename, "WinFellow", "floppy.log");
  floppyLogClear();
#endif
}

void floppyShutdown()
{
  for (ULO i = 0; i < 4; i++)
  {
    floppyImageRemove(i);
  }
  floppyMfmDataFree();
#ifdef FELLOW_SUPPORT_CAPS
  floppyTimeBufDataFree();
  Service->Log.AddLog("Unloading CAPS Image library...\n");
  capsShutdown();
#endif
#ifdef FLOPPY_LOG
  if (floppylogfile != 0)
  {
    fclose(floppylogfile);
  }
#endif
}