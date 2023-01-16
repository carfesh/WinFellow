#pragma once

#include <functional>

#include "fellow/time/Timestamps.h"
#include "fellow/debug/log/DebugLogHandler.h"

typedef void (*ChipBusReadCallback)(const ChipBusTimestamp &timestamp, UWO value);
typedef void (*ChipBusWriteCallback)();
typedef void (*ChipBusNullCallback)(const ChipBusTimestamp &timestamp);

enum class DMAChannelMode
{
  ReadMode = 0,
  WriteMode = 1,
  NullMode = 2
};

enum class DMAChannelType
{
  BitplaneChannel = 0,
  CopperChannel = 1,
  SpriteChannel = 2
};

struct DMAChannel
{
  DMAChannelType Source;
  ChipBusTimestamp Timestamp;
  ChipBusTimestamp OriginalTimestamp;
  std::function<void(const ChipBusTimestamp &timestamp, uint16_t value)> ReadCallback;
  ChipBusWriteCallback WriteCallback;
  ChipBusNullCallback NullCallback;
  bool IsRequested;
  DMAChannelMode Mode;
  ULO Channel;
  ULO Pt;
  UWO Value;

  DMAChannel();
};

class Scheduler;
class BitplaneDMA;

class DMAController
{
private:
  Scheduler *_scheduler;
  BitplaneDMA *_bitplaneDMA;

  bool _refreshCycle[227];

  DMAChannel _bitplane;
  DMAChannel _copper;
  DMAChannel _sprite;

  ChipBusTimestamp _nextTime;

  void UpdateNextTime(const ChipBusTimestamp &timestamp);
  void UpdateNextTimeFullScan();
  void SetEventTime() const;

  void Execute(const DMAChannel *operation);
  DMAChannel *GetPrioritizedDMAChannel();

  void Clear();
  void InitializeRefreshCycleTable();

  DebugLogSource MapDMAChannelTypeToDebugLogSource(DMAChannelType dmaChannelType) const;
  DebugLogChipBusMode MapDMAChannelModeToDebugLogChipBusMode(DMAChannelMode dmaChannelMode) const;
  void AddLogEntry(const DMAChannel *operation, UWO value);

public:
  bool UseEventQueue;

  void ScheduleBitplaneDMA(const ChipBusTimestamp &timestamp, ULO pt, ULO channel);
  void ScheduleCopperDMARead(const ChipBusTimestamp &timestamp, ULO pt);
  void ScheduleCopperDMANull(const ChipBusTimestamp &timestamp);
  void ScheduleSpriteDMA(const ChipBusTimestamp &timestamp, ULO pt, ULO channel);

  void RemoveCopperDMA();

  static void HandleEvent();
  void Handler();

  void SoftReset();
  void HardReset();
  void EndOfFrame();
  void EmulationStart();
  void EmulationStop();
  void Startup();
  void Shutdown();

  DMAController(Scheduler *scheduler, BitplaneDMA *bitplaneDMA);
  ~DMAController() = default;
};

extern DMAController dma_controller;
