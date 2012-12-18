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

#include "parser.h"
#include "config/config.h"
#include "bitstream.h"
#include "pes.h"

cParser::cParser(cTSDemuxer *demuxer, int buffersize, int packetsize) : cRingBufferLinear(buffersize, packetsize), m_demuxer(demuxer), m_startup(true)
{
  m_samplerate = 0;
  m_bitrate = 0;
  m_channels = 0;
  m_duration = 0;
  m_headersize = 0;

  m_curPTS = DVD_NOPTS_VALUE;
  m_curDTS = DVD_NOPTS_VALUE;
}

cParser::~cParser()
{
}

int cParser::ParsePESHeader(uint8_t *buf, size_t len)
{
  // parse PES header
  unsigned int hdr_len = PesPayloadOffset(buf);

  // PTS / DTS
  int64_t pts = PesHasPts(buf) ? PesGetPts(buf) : DVD_NOPTS_VALUE;
  int64_t dts = PesHasDts(buf) ? PesGetDts(buf) : DVD_NOPTS_VALUE;

  if (dts == DVD_NOPTS_VALUE)
   dts = pts;

  if(pts != 0) m_curDTS = dts;
  if(dts != 0) m_curPTS = pts;

  return hdr_len;
}

void cParser::SendPayload(unsigned char* payload, int length)
{
  sStreamPacket pkt;
  pkt.data     = payload;
  pkt.size     = length;
  pkt.duration = m_duration;
  pkt.dts      = m_curDTS;
  pkt.pts      = m_curPTS;

  m_demuxer->SendPacket(&pkt);
}

void cParser::PutData(unsigned char* data, int length, bool pusi)
{
  // get PTS / DTS on PES start
  if (pusi)
  {
    int64_t pts = m_curPTS;
    int64_t dts = m_curDTS;

    int offset = ParsePESHeader(data, length);
    data += offset;
    length -= offset;

    if(pts > m_curPTS) m_curPTS = pts;
    if(dts > m_curDTS) m_curDTS = dts;

    m_startup = false;
  }

  // put data
  if(!m_startup && length > 0 && data != NULL)
  {
    Put(data, length);
  }
}

void cParser::Parse(unsigned char *data, int datasize, bool pusi)
{
  // get available data
  int length = 0;
  uint8_t* buffer = Get(length);

  // do we have a sync ?
  int framesize = 0;
  if(length > m_headersize && buffer != NULL && CheckAlignmentHeader(buffer, framesize))
  {
    if(framesize > 0 && length >= framesize)
    {
      ParsePayload(buffer, framesize);
      SendPayload(buffer, framesize);

      m_curPTS = PtsAdd(m_curPTS, m_duration);
      m_curDTS = PtsAdd(m_curDTS, m_duration);

      Del(framesize);
    }

    PutData(data, datasize, pusi);
    return;
  }

  // try to find sync
  int offset = FindAlignmentOffset(buffer, length, 0, framesize);
  if(offset != -1)
  {
    INFOLOG("sync found at offset %i", offset);
    Del(offset);
    Parse(NULL, 0, false);
  }

  PutData(data, datasize, pusi);
}

void cParser::ParsePayload(unsigned char* payload, int length)
{
}

int cParser::FindAlignmentOffset(unsigned char* buffer, int buffersize, int o, int& framesize) {
  framesize = 0;

  // seek sync
  while(o < (buffersize - m_headersize) && !CheckAlignmentHeader(buffer + o, framesize))
    o++;

  // not found
  if(o >= buffersize - m_headersize)
    return -1;

  return o;
}

bool cParser::CheckAlignmentHeader(unsigned char* buffer, int& framesize) {
  framesize = 0;
  return true;
}

int cParser::FindStartCode(unsigned char* buffer, int buffersize, int offset, uint32_t startcode, uint32_t mask) {
  uint32_t sc = 0xFFFFFFFF;

  while(offset < buffersize) {

    sc = (sc << 8) | buffer[offset++];

    if(sc == startcode)
      return offset - 4;
  }

  return -1;
}
