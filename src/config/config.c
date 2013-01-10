/*
 *      vdr-plugin-xvdr - XVDR server plugin for VDR
 *
 *      Copyright (C) 2010 Alwin Esch (Team XBMC)
 *      Copyright (C) 2010, 2011 Alexander Pipelka
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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <vdr/plugin.h>
#include <vdr/tools.h>
#include <vdr/videodir.h>

#include "config.h"
#include "live/channelcache.h"
#include "live/livequeue.h"
#include "recordings/recordingscache.h"

cXVDRServerConfig::cXVDRServerConfig()
{
  listen_port         = LISTEN_PORT;
  ConfigDirectory     = NULL;
  stream_timeout      = 3;
  ReorderCmd          = NULL;
}

void cXVDRServerConfig::Load() {
  cLiveQueue::SetTimeShiftDir(VideoDirectory);
  cRecordingsCache::GetInstance().LoadResumeData();

  if(!cConfig<cSetupLine>::Load(AddDirectory(ConfigDirectory, GENERAL_CONFIG_FILE), true, false))
    return;

  for (cSetupLine* l = First(); l; l = Next(l))
  {
    if(!Parse(l->Name(), l->Value()))
      ERRORLOG("Unknown config parameter %s = %s in %s", l->Name(), l->Value(), GENERAL_CONFIG_FILE);
  }

  cLiveQueue::RemoveTimeShiftFiles();
  cChannelCache::LoadChannelCacheData();
}

bool cXVDRServerConfig::Parse(const char* Name, const char* Value)
{
  if     (!strcasecmp(Name, "TimeShiftDir")) cLiveQueue::SetTimeShiftDir(Value);
  else if(!strcasecmp(Name, "MaxTimeShiftSize")) cLiveQueue::SetBufferSize(strtoull(Value, NULL, 10));
  else if(!strcasecmp(Name, "PiconsURL")) PiconsURL = Value;
  else if(!strcasecmp(Name, "ReorderCmd")) ReorderCmd = Value;
  else return false;

  return true;
}

/* Global instance */
cXVDRServerConfig XVDRServerConfig;
