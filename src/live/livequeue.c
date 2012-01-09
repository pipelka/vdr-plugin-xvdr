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
#include "livequeue.h"

#include "net/cxsocket.h"
#include "net/responsepacket.h"

cLiveQueue::cLiveQueue(cxSocket* socket) : m_socket(socket)
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

bool cLiveQueue::Add(cResponsePacket* p)
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
    cResponsePacket* p = NULL;
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

bool cLiveQueue::write(cResponsePacket* packet)
{
  int fd = m_socket->fd();
  ssize_t size = (ssize_t)packet->getLen();
  const unsigned char *ptr = (const unsigned char *)packet->getPtr();

  fd_set set;
  struct timeval to;

  FD_ZERO(&set);
  FD_SET(fd, &set);

  to.tv_sec = 0;
  to.tv_usec = 10 * 1000;

  if(select(fd + 1, NULL, &set, NULL, &to) <= 0 || !Running())
    return false;

  while (size > 0)
  {
    ssize_t p = ::send(fd, ptr, size, MSG_NOSIGNAL | MSG_DONTWAIT);

    if (!Running())
      return false;

    if (p <= 0)
    {
      if (errno == EWOULDBLOCK || errno == EAGAIN)
      {
        DEBUGLOG("cxSocket::write: blocked, retrying");
        continue;
      }
      ERRORLOG("cxSocket::write: write() error");
      return false;
    }

    ptr  += p;
    size -= p;
  }

  return true;
}
