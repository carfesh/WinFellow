#include <sstream>
#include <iomanip>

#include "fellow/api/defs.h"
#include "fellow/api/Drivers.h"
#include "fellow/automation/Automator.h"
#include "fellow/scheduler/Scheduler.h"
#include "fellow/api/drivers/IGraphicsDriver.h"

using namespace std;
using namespace fellow::api;

Automator automator;

void Automator::TakeSnapshot()
{
  if (SnapshotDirectory.empty())
  {
    return;
  }
  _snapshotCounter++;
  if (_snapshotCounter >= SnapshotFrequency)
  {
    _snapshotCounter = 0;
    _snapshotsTaken++;

    ostringstream filename;
    filename << SnapshotDirectory << '\\' << setfill('0') << setw(4) << _snapshotsTaken << '_' << scheduler.GetRasterFrameCount() + 1 << ".bmp";

    Driver->Graphics.SaveScreenshot(false, filename.str().c_str());
  }
}

void Automator::RecordKey(UBY keyCode)
{
  if (RecordScript)
  {
    _script.RecordKey(keyCode);
  }
}

void Automator::RecordMouse(gameport_inputs mousedev, LON x, LON y, BOOLE button1, BOOLE button2, BOOLE button3)
{
  if (RecordScript)
  {
    _script.RecordMouse(mousedev, x, y, button1, button2, button3);
  }
}

void Automator::RecordJoystick(gameport_inputs joydev, BOOLE left, BOOLE up, BOOLE right, BOOLE down, BOOLE button1, BOOLE button2)
{
  if (RecordScript)
  {
    _script.RecordJoystick(joydev, left, up, right, down, button1, button2);
  }
}

void Automator::RecordEmulatorAction(kbd_event action)
{
  if (RecordScript)
  {
    _script.RecordEmulatorAction(action);
  }
}

void Automator::EndOfLine()
{
  if (!RecordScript)
  {
    ULL frameNumber = scheduler.GetRasterFrameCount();
    ULO line = scheduler.GetRasterY();
    _script.ExecuteUntil(frameNumber, line);
  }
}

void Automator::EndOfFrame()
{
  if (SnapshotEnable)
  {
    TakeSnapshot();
  }
}

void Automator::Startup()
{
  if (!RecordScript)
  {
    _script.Load(ScriptFilename);
  }
}

void Automator::Shutdown()
{
  if (RecordScript)
  {
    _script.Save(ScriptFilename);
  }
}

Automator::Automator() : RecordScript(false), SnapshotFrequency(100), SnapshotEnable(false)
{
}
