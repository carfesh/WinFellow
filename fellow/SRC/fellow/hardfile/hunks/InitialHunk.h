#pragma once

#include <memory>
#include "fellow/hardfile/hunks/HunkBase.h"

namespace fellow::hardfile::hunks
{
  class InitialHunk : public HunkBase
  {
  protected:
    ULO _allocateSizeInLongwords;
    ULO _contentSizeInLongwords;
    ULO _vmAddress;
    std::unique_ptr<UBY> _rawData;

  public:
    void Parse(RawDataReader &rawReader) override = 0;
    ULO GetAllocateSizeInLongwords();
    ULO GetAllocateSizeInBytes();
    ULO GetContentSizeInLongwords();
    ULO GetContentSizeInBytes();
    UBY *GetContent();
    void SetVMAddress(ULO vmAddress);
    ULO GetVMAddress();

    InitialHunk(ULO allocateSizeInLongwords);
  };
}
