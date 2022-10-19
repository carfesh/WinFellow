/*=========================================================================*/
/* Fellow                                                                  */
/* Keeps track of sprite state                                             */
/*                                                                         */
/* Authors: Petter Schau                                                   */
/*                                                                         */
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

#pragma once

#include "fellow/api/defs.h"
#include "SPRITE.H"
#include "BitplaneUtility.h"

typedef union SpriteDecodedUnion_ {
  UBY barray[16];
  ByteLongUnion blu[4];
} SpriteDecodedUnion;

typedef struct Sprite_
{
  bool armed;
  bool attached;
  ULO x;
  ByteWordUnion dat_hold[4];      // Sprite image "armed" for output at x.
  SpriteDecodedUnion dat_decoded; // The armed image, decoded
  bool serializing;
  ULO pixels_output;
} Sprite;

class CycleExactSprites : public Sprites
{
private:
  Sprite SpriteState[8];

  void Arm(const unsigned int spriteNo);

  void MergeLores(unsigned int spriteNo, ULO sourcePixelIndex, ULO pixelIndex, ULO pixelCount);
  void MergeHires(unsigned int spriteNo, ULO sourcePixelIndex, ULO pixelIndex, ULO pixelCount);
  void MergeDualLores(unsigned int spriteNo, ULO sourcePixelIndex, ULO pixelIndex, ULO pixelCount);
  void MergeDualHires(unsigned int spriteNo, ULO sourcePixelIndex, ULO pixelIndex, ULO pixelCount);
  void MergeHam(unsigned int spriteNo, ULO sourcePixelIndex, ULO pixelIndex, ULO pixelCount);
  void Merge(const unsigned int spriteNo, ULO sourcePixelIndex, ULO pixelIndex, ULO pixelCount);

  bool StartsInRange(const unsigned int spriteNo, ULO startX, ULO pixelCount) const;
  bool Is16Color(const unsigned int spriteNo) const;

  void ClearState();

  void OutputPartialSprite(unsigned int spriteNo, Sprite &state, ULO pixelIndex, ULO pixelCount);
  void OutputSprite(const unsigned int spriteNo, ULO startX, ULO pixelCount);

public:
  virtual void NotifySprpthChanged(UWO data, const unsigned int spriteNumber);
  virtual void NotifySprptlChanged(UWO data, const unsigned int spriteNumber);
  virtual void NotifySprposChanged(UWO data, const unsigned int spriteNumber);
  virtual void NotifySprctlChanged(UWO data, const unsigned int spriteNumber);
  virtual void NotifySprdataChanged(UWO data, const unsigned int spriteNumber);
  virtual void NotifySprdatbChanged(UWO data, const unsigned int spriteNumber);

  void OutputSprites(ULO startX, ULO pixelCount);

  virtual void HardReset();
  virtual void EndOfLine(ULO line);
  virtual void EndOfFrame();
  virtual void EmulationStart();
  virtual void EmulationStop();

  CycleExactSprites();
  virtual ~CycleExactSprites();
};