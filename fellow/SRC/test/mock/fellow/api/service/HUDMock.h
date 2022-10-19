#pragma once

#include "fellow/api/service/IHUD.h"

namespace test::mock::fellow::api
{
  class HUDMock : public ::fellow::api::IHUD
  {
  private:
    HudConfiguration _hudConfiguration;

  public:
    HudConfiguration &GetHudConfiguration() override;

    void NotifyFloppyLEDChanged(int driveIndex, bool active, bool write) override;
    void NotifyHarddiskLEDChanged(int deviceIndex, bool active, bool write) override;
  };
}