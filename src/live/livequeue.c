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

#include "config/config.h"
#include "net/msgpacket.h"
#include "livequeue.h"

cLiveQueue::cLiveQueue(int sock) : m_socket(sock)
{
}

cLiveQueue::~cLiveQueue()
{
  DEBUGLOG("Deleting LiveQueue");
  m_cond.Signal();
  Cancel(3);
  Cleanup();
}

void cLiveQueue::Cleanup()
{
  cMutexLock lock(&m_lock);
  while(!empty())
  {
    delete front();
    pop();
  }
}

bool cLiveQueue::Add(MsgPacket* p)
{
  cMutexLock lock(&m_lock);

  // queue too long ?
  if (size() > 100) {
    delete p;
    return false;
  }

  // add packet to queue
  push(p);
  m_cond.Signal();

  return true;
}

void cLiveQueue::Action()
{
  INFOLOG("LiveQueue started");

  // wait for first packet
  m_cond.Wait(0);

  while(Running())
  {
    // check packet queue
    MsgPacket* p = NULL;
    m_lock.Lock();

    if(size() > 0)
      p = front();

    m_lock.Unlock();

    // no packets to send
    if(p == NULL)
    {
      m_cond.Wait(3000);
      continue;
    }

    // send packet
    if(!write(p))
      continue;

    // remove packet from queue
    m_lock.Lock();
    pop();
    delete p;
    m_lock.Unlock();

  }

  INFOLOG("LiveQueue stopped");
}

bool cLiveQueue::write(MsgPacket* packet)
{
  while(!packet->write(m_socket, 50) && Running())
    ;
  return true;
}
