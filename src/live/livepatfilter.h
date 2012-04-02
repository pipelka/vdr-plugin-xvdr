/*
 *      vdr-plugin-xvdr - XBMC server plugin for VDR
 *
 *      Copyright (C) 2010 Alwin Esch (Team XBMC)
 *      Copyright (C) 2010, 2011 Alexander Pipelka
 *
 *      http://www.xbmc.org
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
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#ifndef XVDR_LIVEPATFILTER_H
#define XVDR_LIVEPATFILTER_H

#include <vdr/filter.h>
#include <libsi/section.h>
#include <libsi/descriptor.h>

#include "demuxer/demuxer.h"

class cLiveStreamer;

class cLivePatFilter : public cFilter
{
private:
  int             m_pmtPid;
  int             m_pmtSid;
  int             m_pmtVersion;
  const cChannel *m_Channel;
  cLiveStreamer  *m_Streamer;

  int GetPid(SI::PMT::Stream& stream, eStreamType *type, char *langs, int& atype, int *subtitlingType, int *compositionPageId, int *ancillaryPageId);
  void GetLanguage(SI::PMT::Stream& stream, char *langs, int& type);
  virtual void Process(u_short Pid, u_char Tid, const u_char *Data, int Length);

public:
  cLivePatFilter(cLiveStreamer *Streamer, const cChannel *Channel);
};

#endif // XVDR_LIVEPATFILTER_H
