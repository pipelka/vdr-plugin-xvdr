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
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#include "config/config.h"
#include "livereceiver.h"
#include "livestreamer.h"

cLiveReceiver::cLiveReceiver(cLiveStreamer *Streamer, const cChannel* channel, int Priority)
 : cReceiver(channel, Priority)
 , m_Streamer(Streamer)
{
  DEBUGLOG("Starting live receiver");
}

cLiveReceiver::~cLiveReceiver()
{
  DEBUGLOG("Killing live receiver");
}

void cLiveReceiver::Receive(uchar *Data, int Length)
{
  int p = m_Streamer->Put(Data, Length);

  if (p != Length)
    m_Streamer->ReportOverflow(Length - p);
}

inline void cLiveReceiver::Activate(bool On)
{
  m_Streamer->Activate(On);
}


