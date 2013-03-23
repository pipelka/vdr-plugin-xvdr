/*
 *      vdr-plugin-xvdr - XVDR server plugin for VDR
 *
 *      Copyright (C) 2013 Alexander Pipelka
 *
 *      https://github.com/pipelka/vdr-plugin-xvdr
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#include <map>
#include <set>

#include <vdr/timers.h>
#include <vdr/device.h>
#include "config/config.h"
#include "timerconflicts.h"
#include "xvdrchannels.h"

int CheckTimerConflicts(cTimer* timer) {
  XVDRChannels.Lock(false);

  // check for timer conflicts
  DEBUGLOG("Checking conflicts for: %s", (const char*)timer->ToText(true));

  // order active timers by starttime
  std::map<time_t, cTimer*> timeline;
  int numTimers = Timers.Count();
  for (int i = 0; i < numTimers; i++)
  {
    cTimer* t = Timers.Get(i);

    // same timer -> skip
    if (!t || timer->Index() == i)
      continue;

    // timer not active -> skip
    if(!(t->Flags() & tfActive))
      continue;

    // this one is earlier -> no match
    if(t->StopTime() <= timer->StartTime())
      continue;

    // this one is later -> no match
    if(t->StartTime() >= timer->StopTime())
      continue;

    timeline[t->StartTime()] = t;
  }

  std::set<int> transponders;
  transponders.insert(timer->Channel()->Transponder()); // we also count ourself
  cTimer* to_check = timer;

  std::map<time_t, cTimer*>::iterator i;
  for (i = timeline.begin(); i != timeline.end(); i++) {
    cTimer* t = i->second;

    // this one is earlier -> no match
    if(t->StopTime() <= to_check->StartTime())
      continue;

    // this one is later -> no match
    if(t->StartTime() >= to_check->StopTime())
      continue;

    // same transponder -> no conflict
    if(t->Channel()->Transponder() == to_check->Channel()->Transponder())
      continue;

    // different source -> no conflict
    if(t->Channel()->Source() != to_check->Channel()->Source())
      continue;

    DEBUGLOG("Possible conflict: %s", (const char*)t->ToText(true));
    transponders.insert(t->Channel()->Transponder());

    // now check conflicting timer
    to_check = t;
  }

  uint32_t number_of_devices_for_this_channel = 0;
  for(int i = 0; i < cDevice::NumDevices(); i++) {
    cDevice* device = cDevice::GetDevice(i);
    if(device != NULL && device->ProvidesTransponder(timer->Channel()))
      number_of_devices_for_this_channel++;
  }

  int cflags = 0;
  if(transponders.size() > number_of_devices_for_this_channel) {
    DEBUGLOG("ERROR - Not enough devices");
    cflags += 2048;
  }
  else if(transponders.size() > 1) {
    DEBUGLOG("Overlapping timers - Will record");
    cflags += 1024;
  }
  else {
    DEBUGLOG("No conflicts");
  }

  XVDRChannels.Unlock();

  return cflags;
}
