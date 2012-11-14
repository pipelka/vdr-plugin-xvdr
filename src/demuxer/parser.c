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

#include "parser.h"
#include "config/config.h"
#include "bitstream.h"
#include "pes.h"
#include "vdr/remux.h"

cParser::cParser(cTSDemuxer *demuxer, int buffersize, int packetsize) : cRingBufferLinear(buffersize, packetsize), m_demuxer(demuxer), m_startup(true)
{
  m_samplerate = 0;
  m_bitrate = 0;
  m_channels = 0;
  m_duration = 0;
  m_headersize = 0;

  m_curPTS = DVD_NOPTS_VALUE;
  m_curDTS = DVD_NOPTS_VALUE;
  m_PTS = DVD_NOPTS_VALUE;
  m_DTS = DVD_NOPTS_VALUE;
}

cParser::~cParser()
{
}

int64_t cParser::PesGetPTS(const uint8_t *buf, int len)
{
  /* assume mpeg2 pes header ... */
  if (PesIsVideoPacket(buf) || PesIsAudioPacket(buf)) {

    if ((buf[6] & 0xC0) != 0x80)
      return DVD_NOPTS_VALUE;
    if ((buf[6] & 0x30) != 0)
      return DVD_NOPTS_VALUE;

    if ((len > 13) && (buf[7] & 0x80)) { /* pts avail */
      int64_t pts;
      pts  = ((int64_t)(buf[ 9] & 0x0E)) << 29 ;
      pts |= ((int64_t) buf[10])         << 22 ;
      pts |= ((int64_t)(buf[11] & 0xFE)) << 14 ;
      pts |= ((int64_t) buf[12])         <<  7 ;
      pts |= ((int64_t)(buf[13] & 0xFE)) >>  1 ;
      return pts;
    }
  }
  return DVD_NOPTS_VALUE;
}

int64_t cParser::PesGetDTS(const uint8_t *buf, int len)
{
  if (PesIsVideoPacket(buf) || PesIsAudioPacket(buf))
  {
    if ((buf[6] & 0xC0) != 0x80)
      return DVD_NOPTS_VALUE;
    if ((buf[6] & 0x30) != 0)
      return DVD_NOPTS_VALUE;

    if (len > 18 && (buf[7] & 0x40)) { /* dts avail */
      int64_t dts;
      dts  = ((int64_t)( buf[14] & 0x0E)) << 29 ;
      dts |=  (int64_t)( buf[15]         << 22 );
      dts |=  (int64_t)((buf[16] & 0xFE) << 14 );
      dts |=  (int64_t)( buf[17]         <<  7 );
      dts |=  (int64_t)((buf[18] & 0xFE) >>  1 );
      return dts;
    }
  }
  return DVD_NOPTS_VALUE;
}

int cParser::ParsePESHeader(uint8_t *buf, size_t len)
{
  // parse PES header
  unsigned int hdr_len = PesPayloadOffset(buf);

  // PTS / DTS
  int64_t pts = PesGetPTS(buf, len);
  int64_t dts = PesGetDTS(buf, len);

  if (dts == DVD_NOPTS_VALUE)
   dts = pts;

  dts = dts & PTS_MASK;
  pts = pts & PTS_MASK;

  if(pts != 0) m_curDTS = dts;
  if(dts != 0) m_curPTS = pts;

  if (m_DTS == DVD_NOPTS_VALUE)
    m_DTS = m_curDTS;

  if (m_PTS == DVD_NOPTS_VALUE)
    m_PTS = m_curPTS;

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
    int offset = ParsePESHeader(data, length);
    data += offset;
    length -= offset;
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

  if(length < m_headersize || buffer == NULL)
  {
    PutData(data, datasize, pusi);
    return;
  }

  // do we have a sync ?
  int framesize = 0;
  if(CheckAlignmentHeader(buffer, framesize))
  {
    if(framesize > 0 && length >= framesize)
    {
      ParsePayload(buffer, framesize);
      SendPayload(buffer, framesize);

      m_curPTS += m_duration;
      m_curDTS += m_duration;

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
