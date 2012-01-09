/*
 *      vdr-plugin-xvdr - XBMC server plugin for VDR
 *
 *      Copyright (C) 2012 Alexander Pipelka
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

#ifndef XVDR_LIVEQUEUE_H
#define XVDR_LIVEQUEUE_H

#include <queue>
#include <vdr/thread.h>

class cxSocket;
class cResponsePacket;

class cLiveQueue : public cThread, protected std::queue<cResponsePacket*>
{
public:

  cLiveQueue(cxSocket* s);

  virtual ~cLiveQueue();

  bool Add(cResponsePacket* p);

protected:

  void Action();

  void Cleanup();

  bool write(cResponsePacket* packet);

  cxSocket* m_socket;

  cMutex m_lock;

  cCondWait m_cond;
};

#endif // XVDR_LIVEQUEUE_H
