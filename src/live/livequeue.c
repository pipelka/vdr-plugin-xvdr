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

#include <sys/types.h>
#include <unistd.h>

#include "config/config.h"
#include "net/msgpacket.h"
#include "livequeue.h"

cString cLiveQueue::TimeShiftDir = "/video";
uint64_t cLiveQueue::BufferSize = 1024*1024*1024;

cLiveQueue::cLiveQueue(int sock) : m_socket(sock), m_readfd(-1), m_writefd(-1)
{
  m_pause = false;
}

cLiveQueue::~cLiveQueue()
{
  DEBUGLOG("Deleting LiveQueue");
  m_cond.Signal();
  Cancel(3);
  Cleanup();
  CloseTimeShift();
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

void cLiveQueue::Request()
{
  cMutexLock lock(&m_lock);

  // read packet from storage
  MsgPacket* p = MsgPacket::read(m_readfd, 1000);

  // check for buffer overrun
  if(p == NULL)
  {
    // ring-buffer overrun ?
    off_t pos = lseek(m_readfd, 0, SEEK_CUR);
    if(pos < BufferSize)
      return;

    lseek(m_readfd, 0, SEEK_SET);
    p = MsgPacket::read(m_readfd, 1000);
  }

  // no packet
  if(p == NULL)
    return;

  // put packet into queue
  push(p);

  m_cond.Signal();
}

bool cLiveQueue::Add(MsgPacket* p)
{
  cMutexLock lock(&m_lock);

  // in timeshift mode ?
  if(m_pause || (!m_pause && m_writefd != -1))
  {
    // write packet
    if(!p->write(m_writefd, 1000))
    {
      DEBUGLOG("Unable to write packet into timeshift ringbuffer !");
      return false;
    }

    // ring-buffer overrun ?
    off_t length = lseek(m_writefd, 0, SEEK_CUR);
    if(length >= BufferSize)
    {
      ftruncate(m_readfd, length);
      lseek(m_writefd, 0, SEEK_SET);
    }
    return true;
  }

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
    MsgPacket* p = NULL;

    m_lock.Lock();

    // just wait if we are paused
    if(m_pause)
    {
      m_lock.Unlock();
      m_cond.Wait(0);
      m_lock.Lock();
    }

    // check packet queue
    if(size() > 0)
    {
      p = front();
      pop();
    }

    m_lock.Unlock();

    // no packets to send
    if(p == NULL)
    {
      m_cond.Wait(3000);
      continue;
    }

    // send packet
    write(p);
    delete p;
  }

  INFOLOG("LiveQueue stopped");
}

bool cLiveQueue::write(MsgPacket* packet)
{
  while(!packet->write(m_socket, 50) && Running())
    ;
  return true;
}

void cLiveQueue::CloseTimeShift()
{
  close(m_readfd);
  m_readfd = -1;
  close(m_writefd);
  m_writefd = -1;

  unlink(m_storage);
}

bool cLiveQueue::Pause(bool on)
{
  cMutexLock lock(&m_lock);

  // deactivate timeshift
  if(!on)
  {
    m_pause = false;
    m_cond.Signal();
    return true;
  }

  if(m_pause)
    return false;

  // create offline storage
  if(m_readfd == -1)
  {
    m_storage = cString::sprintf("%s/xvdr-ringbuffer-%05i.data", (const char*)TimeShiftDir, m_socket);
    DEBUGLOG("FILE: %s", (const char*)m_storage);

    m_readfd = open(m_storage, O_CREAT | O_RDONLY, 0644);
    m_writefd = open(m_storage, O_CREAT | O_WRONLY, 0644);
    lseek(m_readfd, 0, SEEK_SET);
    lseek(m_writefd, 0, SEEK_SET);

    if(m_readfd == -1) {
      ERRORLOG("Failed to create timeshift ringbuffer !");
    }
  }

  m_pause = true;

  // push all packets from the queue to the offline storage
  DEBUGLOG("Writing %i packets into timeshift buffer", size());

  while(!empty())
  {
    MsgPacket* p = front();

    p->write(m_writefd, 1000);
    delete p;

    pop();
  }

  return true;
}

void cLiveQueue::SetTimeShiftDir(const cString& dir)
{
  TimeShiftDir = dir;
  DEBUGLOG("TIMESHIFTDIR: %s", (const char*)TimeShiftDir);
}

void cLiveQueue::SetBufferSize(uint64_t s)
{
  BufferSize = s;
  DEBUGLOG("BUFFSERIZE: %llu bytes", BufferSize);
}
