/*============================================================================*/
/* Fellow Amiga Emulator                                                      */
/* Portable parts of the module ripper                                        */
/* Author: Torsten Enderling (carfesh@gmx.net)                                */
/*                                                                            */
/* This file is under the GNU Public License (GPL)                            */
/*============================================================================*/
/* Changelog:                                                                 */
/* ----------                                                                 */
/* 2000/12/14:                                                                */
/* - added support for ripping from floppies; currently this sometimes gives  */
/*   different results from a memory rip                                      */
/* - first release; it now features separate ripping and GUI code             */
/*   as well as the access being done via memory access wrapper functions     */
/*   formats detected are some ProTracker clones and SoundFX 1.3/2.0          */
/*============================================================================*/

/* fellow includes */
#include "defs.h"
#include "fmem.h"
#include "floppy.h"

/* own includes */
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <string.h>

#include "modrip.h"
#ifdef WIN32
#include "modrip_win32.h"
#else
#include "modrip_linux.h"
#endif

static unsigned modripModsFound;

/*===============================================================*/
/* saves mem for a detect module with a filled ModuleInfo struct */
/* gets the values via memory access function func               */
/*===============================================================*/

static BOOLE modripSaveMem(struct ModuleInfo *info, MemoryAccessFunc func)
{
  ULO i;
  FILE *modfile;

  if(info == NULL) return FALSE;

  if ((modfile = fopen(info->filename, "w+b")) == NULL)
    return FALSE;
  for (i = info->start; i <= info->end; i++)
    fputc((*func)(i), modfile);
  fclose(modfile);
  return TRUE;
}

/*=======================================*/
/* fills a ModuleInfo struct with zeroes */
/*=======================================*/

static void modripModuleInfoInitialize(struct ModuleInfo *info)
{
  if(info) memset(info, 0, sizeof(struct ModuleInfo));
}

/*======================================*/
/* detect ProTracker and clones         */
/* games: SWIV, Lotus II, First Samurai */
/*======================================*/

static void modripDetectProTracker(ULO address, MemoryAccessFunc func)
{
  ULO i;
  int type;
  char *searchstrings[6] = {
	  "M.K.", "4CHN", "6CHN", "8CHN", "FLT4", "FLT8" };
  int channels[6] = {
	  4,      4,      6,      8,      4,      8      };
  struct ModuleInfo info;

  for(type = 0; type < 6; type++) {
    if ( ((*func)(address + 0) == searchstrings[type][0])
      && ((*func)(address + 1) == searchstrings[type][1])
      && ((*func)(address + 2) == searchstrings[type][2])
      && ((*func)(address + 3) == searchstrings[type][3])
       ) {

      modripModuleInfoInitialize(&info);

      /* store general info */
      strcpy(info.typedesc, "ProTracker and clones");
      strncpy(info.typesig, searchstrings[type], 4);
      info.typesig[4] = '\0';
      info.channels = channels[type];

      /* searchstring found, now calc size */
      info.start = address - 0x438;

      /* get sample size */
      info.samplesize = 0;		

      for (i = 0; i <= 30; i++) {
        info.samplesize += (256 * (*func)(info.start + 0x2a + i * 0x1e)
                        +         (*func)(info.start + 0x2b + i * 0x1e)) * 2;
	  }

	  /* some disks like messing around :) */
      if(info.samplesize > MODRIP_MAXMODLEN) return;

      info.songlength = (*func)(info.start + 0x3b6);

      if(info.songlength > MODRIP_MAXMODLEN) return;

      /* scan for max. amount of patterns */
      info.maxpattern = 0;		

      for (i = 0; i <= info.songlength; i++) {
        if (info.maxpattern < (*func)(info.start + 0x3b8 + i)) {
          info.maxpattern = (*func)(info.start + 0x3b8 + i);
		}
	  }

	  if(info.maxpattern > MODRIP_MAXMODLEN) return;

      info.patternsize = (info.maxpattern + 1) * 64 * 4 * channels[type];

      if(info.patternsize > MODRIP_MAXMODLEN) return;

      info.end = info.start + info.samplesize + info.patternsize + 0x43b;

	  if(info.end < info.start) return;

      if ((info.end - info.start < MODRIP_MAXMODLEN)) {
      /* get module name */
      for (i = 0; i < 20; i++) {
        info.modname[i] = (*func)(info.start + i);
	  }
      info.modname[20] = 0;	

      /* set filename for the module file */
      if (strlen(info.modname) > 2) {
        if(!fsdb_name_invalid(info.modname)) {
          strcpy(info.filename, info.modname);
          strcat(info.filename, ".mod");
		}
		else
          sprintf(info.filename, "mod%d.mod", modripModsFound++);
	  }
      else {
        sprintf(info.filename, "mod%d.mod", modripModsFound++);
	  }

	  if(modripGuiSaveRequest(&info)) 
        if(!modripSaveMem(&info, func))
          modripGuiErrorSave(&info);
	  }
	}
  }
}

/*============================*/
/* detect SoundFX 1.3 and 2.0 */
/* games: TwinWorld (1.3)     */
/*============================*/

static void modripDetectSoundFX(ULO address, MemoryAccessFunc func)
{
  BOOLE match = FALSE;
  ULO i, size, offset;
  int patterns;
  struct ModuleInfo info;
    
  if (((*func)(address + 0) == 'S') && ((*func)(address + 1) == 'O')) {
    modripModuleInfoInitialize(&info);
    if (((*func)(address + 2) == 'N') && ((*func)(address + 3) == 'G')) {
      match = TRUE;
      info.start = address - 60;
      info.instruments = 15;
      strcpy(info.typedesc, "SoundFX 1.3");
      strcpy(info.typesig, "SONG");
	}
    if (((*func)(address + 2) == '3') && ((*func)(address + 3) == '1')) {
      match = TRUE;
      info.start = address - 124;
      info.instruments = 31;
      strcpy(info.typedesc, "SoundFX 2.0");
      strcpy(info.typesig, "SO31");
    }
    if (match) {
      offset = 0; size = 0;

      for (i = 0; i < info.instruments; i++)
        size += (*func)(info.start + i*4 + 0) << 8     
              | (*func)(info.start + i*4 + 1)
              | (*func)(info.start + i*4 + 2) << 8 
              | (*func)(info.start + i*4 + 3);

      /* calculate real instruments */
      if (info.instruments == 15) {
        /* SoundFX 1.3 */
	    offset += 80;
	    size += 80;
	  }
      else {
        /* SoundFX 2.0 */
        offset += 144;
        size += 144;
	  }

      for (i = 0; i < info.instruments; i++) {
        offset += 30;
        size += 30;
	  }

      patterns = (*func)(info.start + offset);

      offset += 2;
      info.maxpattern = (*func)(info.start + offset);

      for (i = 1; i < patterns; i++)
        info.maxpattern = max(info.maxpattern, (*func)(info.start + offset + i));

      size += 2 + 128 + ((info.maxpattern + 1) * 1024);
	  info.end = info.start + size;

	  if(info.end < info.start) return;
		
      if (size < MODRIP_MAXMODLEN) {
        /* set filename for the module file */
        sprintf(info.filename, "mod%d.sfx", modripModsFound++);

      if(modripGuiSaveRequest(&info)) 
        if(!modripSaveMem(&info, func))
          modripGuiErrorSave(&info);
      }
    }
  }
}

/*====================================================*/
/* from here on starts the actual fellow ripping code */
/*====================================================*/

/*===================================================*/
/* here we define the formats that are actually used */
/*===================================================*/

#define MODRIP_KNOWNFORMATS 2

static ModuleDetectFunc DetectFunctions[MODRIP_KNOWNFORMATS] = {
  modripDetectProTracker,
  modripDetectSoundFX
};

/*==============================================*/
/* scan the emulated amiga's memory for modules */
/*==============================================*/

void modripScanFellowMemory(void)
{
  ULO i;
  int j;

  if(modripGuiRipMemory())
    for(i = 0; i <= 0xffffff; i++)
      for(j = 0; j < MODRIP_KNOWNFORMATS; j++)
	    (*DetectFunctions[j])(i, fetb);
}

/*====================================*/
/* floppy cache memory access wrapper */
/*====================================*/

#define MODRIP_ADFSIZE   0xDC000
#define MODRIP_FLOPCACHE 0xFFFFF

/* meant to hold the read floppy cache */
static char *modripCurrentFloppyCache = NULL;

ULO modripFloppyCacheRead(ULO address)
{
  return(modripCurrentFloppyCache[address & MODRIP_FLOPCACHE]);
}

/*========================================*/
/* read a floppy image into a given cache */
/*========================================*/

BOOLE modripReadFloppyImage(char *filename, char *cache)
{
  FILE *f;
  char message[MODRIP_TEMPSTRLEN];
  char errdesc[MODRIP_TEMPSTRLEN];
  ULO i;
  int errcode;
  int readbytes;

  if(f = fopen(filename, "rb")) {
    if(readbytes = fread(cache, sizeof(char), MODRIP_ADFSIZE, f) != MODRIP_ADFSIZE) {
      fclose(f);
      sprintf(message, "The disk image %s is of a wrong size (%d bytes).", 
        filename, readbytes);
	  modripGuiError(message);
      return FALSE;
	}
/* damn windows, why is everything done differently here? */
#ifdef WIN32
	if((errcode = _doserrno) != 0) {
#else
	if((errcode = errno) != 0) {
#endif
      fclose(f);
	  sprintf(message, "An error occurred upon reading file %s:\n", filename);
	  sprintf(errdesc, strerror(errcode));
	  strcat(message, errdesc);
	  strcat(message, ".");
	  modripGuiError(message);
      return FALSE;
	}
	fclose(f);
    return TRUE;
  }
  else {
    sprintf(message, "Couldn't open file %s for reading.", filename);
    modripGuiError(message);
    return FALSE;
  }
}

/*====================================*/
/* scan inserted floppies for modules */
/*====================================*/

void modripScanFellowFloppies(void)
{
  int driveNo, j;
  ULO i;
  char cache[MODRIP_FLOPCACHE];
  BOOLE Read;

  modripCurrentFloppyCache = cache;

  for(driveNo = 0; driveNo < 4; driveNo++) { /* for each drive */
    if(floppy[driveNo].inserted) { /* check if a floppy is inserted */
      if(modripGuiRipFloppy(driveNo)) { /* does the user want to rip? */
        memset(cache, 0, MODRIP_FLOPCACHE);
		Read = FALSE;
        if(*floppy[driveNo].imagenamereal) {
			fellowAddLog("mod-ripper %s\n", floppy[driveNo].imagenamereal);
          if(modripReadFloppyImage(floppy[driveNo].imagenamereal, cache))
            Read = TRUE;
		}
		else if(*floppy[driveNo].imagename) {
			fellowAddLog("mod-ripper %s\n", floppy[driveNo].imagename);
          if(modripReadFloppyImage(floppy[driveNo].imagename, cache))
            Read = TRUE;
		}
        if(Read) {
          /* now really begin the ripping */
            for(i = 0; i <= MODRIP_ADFSIZE; i++)
              for(j = 0; j < MODRIP_KNOWNFORMATS; j++)
                (*DetectFunctions[j])(i, modripFloppyCacheRead);
		}
	  }
	}
  }
}

/*============================================================================*/
/* Invokes the mod-ripping                                                    */
/*============================================================================*/

void modripRIP(void)
{
  modripGuiInitialize();

  modripGuiSetBusy();
  modripScanFellowMemory();
  modripScanFellowFloppies();
  modripGuiUnSetBusy();

  modripGuiUnInitialize();
}