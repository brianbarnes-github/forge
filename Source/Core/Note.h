#pragma once

namespace lotro
{

struct Note
{
    int  pitch         = 0;
    int  startTick     = 0;
    int  durationTicks = 0;
    int  velocity      = 0;
    bool isDrum        = false;
};

} // namespace lotro
