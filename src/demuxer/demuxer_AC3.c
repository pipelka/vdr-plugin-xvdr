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

#include "demuxer_AC3.h"
#include "vdr/tools.h"
#include "ac3common.h"

cParserAC3::cParserAC3(cTSDemuxer *demuxer) : cParser(demuxer, 64 * 1024, 4096)
{
  m_headersize = AC3_HEADER_SIZE;
  m_enhanced = false;
}

bool cParserAC3::CheckAlignmentHeader(unsigned char* buffer, int& framesize) {
  cBitStream bs(buffer, AC3_HEADER_SIZE * 8);

  if (bs.GetBits(16) != 0x0B77)
    return false;

  bs.SkipBits(24); // FFWD to bsid
  int bsid = bs.GetBits(5); // bsid

  m_enhanced = (bsid > 10);
  
  bs.Reset(); // rewind to start
  bs.SkipBits(16); // skip syncword
  
  if(m_enhanced) {
    bs.SkipBits(2); // frametype
    bs.SkipBits(3); // substream id
    framesize = (bs.GetBits(11) + 1) << 1;
    return true;
  }
  
  bs.SkipBits(16); // CRC
  int fscod = bs.GetBits(2); // fscod
  int frmsizcod = bs.GetBits(6); // frmsizcod

  if (fscod == 3 || frmsizcod > 37)
    return false;


  framesize = AC3FrameSizeTable[frmsizcod][fscod] * 2;
  return true;
}

void cParserAC3::ParsePayload(unsigned char* payload, int length) {
  cBitStream bs(payload, AC3_HEADER_SIZE * 8);

  if (bs.GetBits(16) != 0x0B77)
    return;

  // EAC-3
  if(m_enhanced) {
    int frametype = bs.GetBits(2);
    if (frametype == EAC3_FRAME_TYPE_RESERVED)
      return;

    bs.SkipBits(3);

    int framesize = (bs.GetBits(11) + 1) << 1;
    if (framesize < AC3_HEADER_SIZE)
      return;

    int numBlocks = 6;
    int sr_code = bs.GetBits(2);
    if (sr_code == 3) {
      int sr_code2 = bs.GetBits(2);
      if (sr_code2 == 3)
        return;

      m_samplerate = AC3SampleRateTable[sr_code2] / 2;
    }
    else {
      numBlocks = EAC3Blocks[bs.GetBits(2)];
      m_samplerate = AC3SampleRateTable[sr_code];
    }

    int channelMode = bs.GetBits(3);
    int lfeon = bs.GetBits(1);

    m_bitrate  = (uint32_t)(8.0 * framesize * m_samplerate / (numBlocks * 256.0));
    m_channels = AC3ChannelsTable[channelMode] + lfeon;
    m_duration = (framesize * 8 * 1000 * 90) / m_bitrate;
  }
  
  // AC-3
  else {
    bs.SkipBits(16); // CRC
    int fscod = bs.GetBits(2);
    int frmsizecod = bs.GetBits(6);
    bs.GetBits(5); // bsid

    bs.SkipBits(3); // bitstream mode
    int acmod = bs.GetBits(3);

    if (fscod == 3 || frmsizecod > 37)
      return;

    if (acmod == AC3_CHMODE_STEREO) {
      bs.SkipBits(2); // skip dsurmod
    }
    else {
      if ((acmod & 1) && acmod != AC3_CHMODE_MONO)
        bs.SkipBits(2);
      if (acmod & 4)
        bs.SkipBits(2);
    }
    int lfeon = bs.GetBits(1);

    m_samplerate = AC3SampleRateTable[fscod];
    m_bitrate = (AC3BitrateTable[frmsizecod>>1] * 1000);
    m_channels = AC3ChannelsTable[acmod] + lfeon;

    int framesize = AC3FrameSizeTable[frmsizecod][fscod] * 2;

    m_duration = (framesize * 8 * 1000 * 90) / m_bitrate;
  }
  
  m_demuxer->SetAudioInformation(m_channels, m_samplerate, m_bitrate, 0, 0);
}
