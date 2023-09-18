#include "DEFS.H"

#include "GraphicsEventQueue.h"

uint32_t GraphicsEvent::MakeArriveTime(uint32_t rasterY, uint32_t cylinder)
{
  return rasterY*GraphicsEventQueue::GetCylindersPerLine() + cylinder;
}

GraphicsEvent::GraphicsEvent(void)
  : _arriveTime(GraphicsEventQueue::GRAPHICS_ARRIVE_TIME_NONE),
    _next(0),
    _prev(0),
    _priority(0)
{
}
