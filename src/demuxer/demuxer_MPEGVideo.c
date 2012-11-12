/*
 *      vdr-plugin-xvdr - XVDR server plugin for VDR
 *
 *      Copyright (C) 2010 Alwin Esch (Team XBMC)
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

#include <stdlib.h>
#include <assert.h>

#include "config/config.h"
#include "live/livestreamer.h"
#include "bitstream.h"
#include "demuxer_MPEGVideo.h"

using namespace std;

#define MPEG_PICTURE_START      0x00000100
#define MPEG_SEQUENCE_START     0x000001b3
#define MPEG_SEQUENCE_EXTENSION 0x000001b5
#define MPEG_SLICE_S            0x00000101
#define MPEG_SLICE_E            0x000001af

/**
 * MPEG2VIDEO frame duration table (in 90kHz clock domain)
 */
const unsigned int mpeg2video_framedurations[16] = {
  0,
  3753,
  3750,
  3600,
  3003,
  3000,
  1800,
  1501,
  1500,
};

cParserMPEG2Video::cParserMPEG2Video(cTSDemuxer *demuxer)
 : cParser(demuxer)
{
  m_pictureBuffer     = NULL;
  m_pictureBufferSize = 0;
  m_pictureBufferPtr  = 0;
  m_StartCond         = 0;
  m_StartCode         = 0;
  m_StartCodeOffset   = 0;
  m_FrameDuration     = 0;
  m_vbvDelay          = -1;
  m_vbvSize           = 0;
  m_StreamPacket      = NULL;
  m_FoundFrame        = false;
}

cParserMPEG2Video::~cParserMPEG2Video()
{
  if (m_pictureBuffer)
  {
    free(m_pictureBuffer);
    m_pictureBuffer = NULL;
  }
}

void cParserMPEG2Video::Parse(unsigned char *data, int size, bool pusi)
{
  uint32_t startcode = m_StartCond;

  if (m_pictureBuffer == NULL)
  {
    m_pictureBufferSize   = 4000;
    m_pictureBuffer       = (uint8_t*)malloc(m_pictureBufferSize);
  }

  if (m_pictureBufferPtr + size + 4 >= m_pictureBufferSize)
  {
    m_pictureBufferSize  += size * 4;
    m_pictureBuffer       = (uint8_t*)realloc(m_pictureBuffer, m_pictureBufferSize);
  }

  for (int i = 0; i < size; i++)
  {
    if (!m_pictureBuffer)
      break;

    m_pictureBuffer[m_pictureBufferPtr++] = data[i];
    startcode = startcode << 8 | data[i];

    if ((startcode & 0xffffff00) != 0x00000100)
      continue;

    bool reset = true;
    if (m_pictureBufferPtr - 4 > 0 && m_StartCode != 0)
    {
      reset = Parse_MPEG2Video(m_pictureBufferPtr - 4, startcode, m_StartCodeOffset);
    }

    if (reset)
    {
      /* Reset packet parser upon length error or if parser tells us so */
      m_pictureBufferPtr = 0;
      m_pictureBuffer[m_pictureBufferPtr++] = startcode >> 24;
      m_pictureBuffer[m_pictureBufferPtr++] = startcode >> 16;
      m_pictureBuffer[m_pictureBufferPtr++] = startcode >> 8;
      m_pictureBuffer[m_pictureBufferPtr++] = startcode >> 0;
    }
    m_StartCode = startcode;
    m_StartCodeOffset = m_pictureBufferPtr - 4;
  }
  m_StartCond = startcode;
}

bool cParserMPEG2Video::Parse_MPEG2Video(size_t len, uint32_t next_startcode, int sc_offset)
{
  int frametype;
  uint8_t *buf = m_pictureBuffer + sc_offset;
  cBitstream bs(buf + 4, (len - 4) * 8);

  switch (m_StartCode)
  {
    case 0x000001e0 ... 0x000001ef:
      /* System start codes for video */
      if (len >= 9)
        ParsePESHeader(buf, len);
      return true;

    case 0x00000100:
      /* Picture start code */
      if (m_FrameDuration == 0 || m_curDTS == DVD_NOPTS_VALUE)
        return true;

      if (Parse_MPEG2Video_PicStart(&frametype, &bs))
        return true;

      if (m_StreamPacket != NULL)
      {
        ERRORLOG("MPEG2 Video got a new picture start code with already openend steam packed");
      }

      m_StreamPacket = new sStreamPacket;
      m_StreamPacket->pts       = m_curPTS;
      m_StreamPacket->dts       = m_curDTS;
      m_StreamPacket->frametype = frametype;
      m_StreamPacket->duration  = 0;
      m_FoundFrame = true;
      break;

    case 0x000001b3:
      /* Sequence start code */
      if (Parse_MPEG2Video_SeqStart(&bs))
        return true;

      break;

    case 0x000001b5:
      if(len < 5)
        return true;
      switch(buf[4] >> 4) {
      case 0x1:
        /* sequence extension */
        if(len < 10)
          return true;
        break;
      }
      break;

    case 0x00000101 ... 0x000001af:
      /* Slices */

      if (next_startcode == 0x100 || next_startcode > 0x1af)
      {
        /* Last picture slice (because next not a slice) */
        if(m_StreamPacket == NULL)
        {
          /* no packet, may've been discarded by sanity checks here */
          return true;
        }

        m_StreamPacket->data      = m_pictureBuffer;
        m_StreamPacket->size      = m_pictureBufferPtr - 4;
        m_StreamPacket->duration  = m_FrameDuration;

        // check if packet has a valid PTS
        if(m_StreamPacket->pts == DVD_NOPTS_VALUE)
          m_StreamPacket->pts = m_StreamPacket->dts;

        m_demuxer->SendPacket(m_StreamPacket);

        // remove packet
        free(m_StreamPacket->data);
        delete m_StreamPacket;
        m_StreamPacket = NULL;

        m_pictureBuffer = (uint8_t*)malloc(m_pictureBufferSize);

        /* If we know the frame duration, increase DTS accordingly */
        m_curDTS += m_FrameDuration;

        /* PTS cannot be extrapolated (it's not linear) */
        m_curPTS = DVD_NOPTS_VALUE;
        return true;
      }
      break;

    default:
      break;
  }

  return false;
}

bool cParserMPEG2Video::Parse_MPEG2Video_SeqStart(cBitstream *bs)
{
  if (bs->length() < 61)
    return true;

  int width  = bs->readBits(12);
  int height = bs->readBits(12);

  // figure out Display Aspect Ratio
  double DAR = 0;
  uint8_t aspect = bs->readBits(4);

  switch(aspect)
  {
    case 0:
    default:
      ERRORLOG("invalid / forbidden DAR in sequence header !");
      break;
    case 1:
      DAR = 1.0;
      break;
    case 2:
      DAR = 4.0/3.0;
      break;
    case 3:
      DAR = 16.0/9.0;
      break;
    case 4:
      DAR = 2.21;
      break;
  }

  m_FrameDuration = mpeg2video_framedurations[bs->readBits(4)];
  bs->skipBits(18);
  bs->skipBits(1);

  m_vbvSize = bs->readBits(10) * 16 * 1024 / 8;
  m_demuxer->SetVideoInformation(0,0, height, width, DAR, 1, 1);

  return false;
}

bool cParserMPEG2Video::Parse_MPEG2Video_PicStart(int *frametype, cBitstream *bs)
{
  if (bs->length() < 29)
    return true;

  bs->skipBits(10); /* temporal reference */

  int pct = bs->readBits(3);
  if (pct < PKT_I_FRAME || pct > PKT_B_FRAME)
    return true; /* Illegal picture_coding_type */

  *frametype = pct;

  /*int vbvDelay =*/ bs->readBits(16); /* vbv_delay */
  /*if (vbvDelay  == 0xffff)
    m_vbvDelay = -1;
  else
    m_vbvDelay = Rescale(vbvDelay);*/

  return false;
}
