#pragma once

#include "Driver/ISoundDriver.h"
#include "mmdeviceapi.h"
#include "Audioclient.h"

class WASAPISoundDriver : public ISoundDriver
{
private:
  SoundDriverRuntimeConfiguration _runtimeConfiguration;

  bool _isInitialized = false;

  const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
  const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
  const IID IID_IAudioClient = __uuidof(IAudioClient);
  const IID IID_IAudioRenderClient = __uuidof(IAudioRenderClient);

  IMMDeviceEnumerator *pEnumerator = NULL;
  IMMDevice *pDevice = NULL;
  IAudioClient *pAudioClient = NULL;
  IAudioRenderClient *pRenderClient = NULL;
  WAVEFORMATEX *pwfx = NULL;
  UINT32 bufferFrameCount;
  UINT32 numFramesAvailable;
  UINT32 numFramesPadding;
  BYTE *pData;
  DWORD flags = 0;

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