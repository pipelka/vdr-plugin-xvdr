/*
 *      vdr-plugin-xvdr - XVDR server plugin for VDR
 *
 *      Copyright (C) 2010 Alwin Esch (Team XBMC)
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

#include "demuxer_EAC3.h"
#include "bitstream.h"
#include "ac3common.h"

static const uint8_t EAC3Blocks[4] = {
  1, 2, 3, 6
};

typedef enum {
  EAC3_FRAME_TYPE_INDEPENDENT = 0,
  EAC3_FRAME_TYPE_DEPENDENT,
  EAC3_FRAME_TYPE_AC3_CONVERT,
  EAC3_FRAME_TYPE_RESERVED
} EAC3FrameType;

cParserEAC3::cParserEAC3(cTSDemuxer *demuxer) : cParser(demuxer, 64 * 1024, 4096)
{
  m_headersize = AC3_HEADER_SIZE;
}

bool cParserEAC3::CheckAlignmentHeader(unsigned char* buffer, int& framesize) {
  cBitstream bs(buffer, AC3_HEADER_SIZE * 8);

  if (bs.readBits(16) != 0x0B77)
    return false;

  bs.skipBits(2);  // frametype
  bs.skipBits(3);  // substream id

  framesize = (bs.readBits(11) + 1) << 1;
  return true;
}

void cParserEAC3::ParsePayload(unsigned char* payload, int length) {
  cBitstream bs(payload, AC3_HEADER_SIZE * 8);

  if (bs.readBits(16) != 0x0B77)
    return;

  int frametype = bs.readBits(2);
  if (frametype == EAC3_FRAME_TYPE_RESERVED)
    return;

  bs.skipBits(3);

  int framesize = (bs.readBits(11) + 1) << 1;
  if (framesize < AC3_HEADER_SIZE)
   return;

  int numBlocks = 6;
  int sr_code = bs.readBits(2);
  if (sr_code == 3)
  {
    int sr_code2 = bs.readBits(2);
    if (sr_code2 == 3)
      return;

    m_samplerate = AC3SampleRateTable[sr_code2] / 2;
  }
  else
  {
    numBlocks = EAC3Blocks[bs.readBits(2)];
    m_samplerate = AC3SampleRateTable[sr_code];
  }

  int channelMode = bs.readBits(3);
  int lfeon = bs.readBits(1);

  m_bitrate  = (uint32_t)(8.0 * framesize * m_samplerate / (numBlocks * 256.0));
  m_channels = AC3ChannelsTable[channelMode] + lfeon;

  m_duration = (framesize * 8 * 1000 * 90) / m_bitrate;

  m_demuxer->SetAudioInformation(m_channels, m_samplerate, m_bitrate, 0, 0);
}
