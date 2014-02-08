/*
 *      vdr-plugin-xvdr - XVDR server plugin for VDR
 *
 *      Copyright (C) 2012 Alexander Pipelka
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

#ifndef XVDR_LIVEQUEUE_H
#define XVDR_LIVEQUEUE_H

#include <string>
#include <queue>
#include <vdr/thread.h>

class MsgPacket;

class cLiveQueue : public cThread, protected std::queue<MsgPacket*>
{
public:

  cLiveQueue(int s);

  virtual ~cLiveQueue();

  bool Add(MsgPacket* p);

  void Request();

  bool Pause(bool on = true);

  bool IsPaused();

  bool TimeShiftMode();

  static void SetTimeShiftDir(const cString& dir);

  static void SetBufferSize(uint64_t s);

  static void RemoveTimeShiftFiles();

  void Cleanup();

protected:

  void Action();

  void CloseTimeShift();

  int m_socket;

  int m_readfd;

  int m_writefd;

  bool m_pause;

  cMutex m_lock;

  cCondWait m_cond;

  std::string m_storage;

  static std::string TimeShiftDir;

  static uint64_t BufferSize;
};

#endif // XVDR_LIVEQUEUE_H
