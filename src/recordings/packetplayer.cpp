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

#include "config/config.h"
#include "packetplayer.h"

cPacketPlayer::cPacketPlayer(cRecording* rec) : cRecPlayer(rec), m_demuxers(this) {
  m_requestStreamChange = true;
  m_firstKeyFrameSeen = false;
}

cPacketPlayer::~cPacketPlayer() {
    clearQueue();
}

void cPacketPlayer::sendStreamPacket(sStreamPacket* p) {
  // check if we've got a key frame
  if(p->frametype == cStreamInfo::ftIFRAME && !m_firstKeyFrameSeen) {
    INFOLOG("got first key frame");
    m_firstKeyFrameSeen = true;
  }
  
  // streaming starts with a key frame
  if(!m_firstKeyFrameSeen) {
    return;
  }
  
  // initialise stream packet
  MsgPacket* packet = new MsgPacket(XVDR_STREAM_MUXPKT, XVDR_CHANNEL_STREAM);
  packet->disablePayloadCheckSum();

  // write stream data
  packet->put_U16(p->pid);

  packet->put_S64(p->rawpts);
  packet->put_S64(p->rawdts);
  packet->put_U32(p->duration);

  // write frame type into unused header field clientid
  packet->setClientID((uint16_t)p->frametype);

  // write payload into stream packet
  packet->put_U32(p->size);
  packet->put_Blob(p->data, p->size);
  packet->put_U64(m_position);
  packet->put_U64(m_totalLength);
  
  m_queue.push(packet);
}

void cPacketPlayer::RequestStreamChange() {
  INFOLOG("stream change requested");
  m_requestStreamChange = true;
}

MsgPacket* cPacketPlayer::getNextPacket() {
  int pmtVersion = 0;
  int patVersion = 0;
  
  int packet_count = 20;
  int packet_size = TS_SIZE * packet_count;

  unsigned char buffer[packet_size];

  // get next block (TS packets)
  if(getBlock(buffer, m_position, packet_size) != packet_size) {
    return NULL;
  }
  
  // advance to next block
  m_position += packet_size;

  // new PAT / PMT found ?
  if(m_parser.ParsePatPmt(buffer, TS_SIZE)) {
    m_parser.GetVersions(m_patVersion, pmtVersion);
    if(pmtVersion > m_pmtVersion) {
      INFOLOG("found new PMT version (%i)", pmtVersion);
      m_pmtVersion = pmtVersion;
      
     // update demuxers from new PMT
      INFOLOG("updating demuxers");
      cStreamBundle streamBundle = cStreamBundle::FromPatPmt(&m_parser);
      m_demuxers.updateFrom(&streamBundle);

      m_requestStreamChange = true;
    }
  }

  // put packets into demuxer
  uint8_t* p = buffer;
  for(int i = 0; i < packet_count; i++) {
    m_demuxers.processTsPacket(p);
    p += TS_SIZE;
  }
  
  // stream change needed / requested
  if(m_requestStreamChange && m_parser.GetVersions(patVersion, pmtVersion)) {   
    // demuxers need to be ready
    if(!m_demuxers.isReady()) {
      return NULL;
    }

    INFOLOG("demuxers ready");
    for(auto i: m_demuxers) {
      i->info();
    }

    INFOLOG("create streamchange packet");
    m_requestStreamChange = false;
    return m_demuxers.createStreamChangePacket();
  }
  
  // get next packet from queue (if any)
  if(m_queue.size() == 0) {
    return NULL;
  }
  
  MsgPacket* packet = m_queue.front();
  m_queue.pop();
  
  return packet;
}

MsgPacket* cPacketPlayer::getPacket() {
  MsgPacket* p = NULL;
  
  // process data until the next packet drops out
  while(p == NULL && m_position < m_totalLength) {
    p = getNextPacket();
  }
  
  return p;
}

void cPacketPlayer::clearQueue() {
  MsgPacket* p = NULL;

  while(m_queue.size() > 0) {
    p = m_queue.front();
    m_queue.pop();
    delete p;
  }
}

bool cPacketPlayer::seek(uint64_t position) {
  // adujst position to TS packet borders
  m_position = (position / TS_SIZE) * TS_SIZE;
  
  // invalid position ?
  if(m_position >= m_totalLength) {
    return false;
  }

  INFOLOG("seek: %llu / %llu", m_position, m_totalLength);

  // reset parser
  m_parser.Reset();
  m_requestStreamChange = true;
  m_firstKeyFrameSeen = false;
  m_patVersion = -1;
  m_pmtVersion = -1;
  
  // remove pending packets
  clearQueue();

  return true;
}
