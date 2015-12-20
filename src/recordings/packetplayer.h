/*
 *      vdr-plugin-xvdr - XVDR server plugin for VDR
 *
 *      Copyright (C) 2015 Alexander Pipelka
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

#ifndef XVDR_PACKETPLAYER_H
#define	XVDR_PACKETPLAYER_H

#include "recordings/recplayer.h"
#include "demuxer/demuxer.h"
#include "demuxer/demuxerbundle.h"
#include "net/msgpacket.h"

#include "vdr/remux.h"
#include <queue>

class cPacketPlayer : public cRecPlayer, protected cTSDemuxer::Listener {
public:

  cPacketPlayer(cRecording* rec);

  virtual ~cPacketPlayer();

  MsgPacket* getPacket();
  
  bool seek(uint64_t position);
  
protected:
  
  MsgPacket* getNextPacket();
  
  void sendStreamPacket(sStreamPacket* p);

  void RequestStreamChange();

  void clearQueue();
  
private:

  cPatPmtParser m_parser;

  cDemuxerBundle m_demuxers;

  uint64_t m_position = 0;

  bool m_requestStreamChange;
  
  bool m_firstKeyFrameSeen;
  
  int m_patVersion = -1;
  
  int m_pmtVersion = -1;

  std::queue<MsgPacket*> m_queue;
  
};

#endif	// XVDR_PACKETPLAYER_H
