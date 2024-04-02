#pragma once

#include "Driver/ISoundDriver.h"

class WASAPISoundDriver : public ISoundDriver
{
private:
  SoundDriverRuntimeConfiguration _runtimeConfiguration;

public:
  void Play(int16_t *leftBuffer, int16_t *rightBuffer, uint32_t sampleCount) override;
  void PollBufferPosition() override;
  bool SetCurrentSoundDeviceVolume(int volume) override;

  void HardReset();
  bool EmulationStart(SoundDriverRuntimeConfiguration runtimeConfiguration) override;
  void EmulationStop() override;

  bool IsInitialized() override;

  WASAPISoundDriver();
  virtual ~WASAPISoundDriver();
};