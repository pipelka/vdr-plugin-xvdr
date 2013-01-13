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
}

bool cParserAC3::CheckAlignmentHeader(unsigned char* buffer, int& framesize) {
  cBitStream bs(buffer, AC3_HEADER_SIZE * 8);

  if (bs.GetBits(16) != 0x0B77)
    return false;

  bs.SkipBits(16);                // CRC
  int fscod = bs.GetBits(2);     // fscod
  int frmsizcod = bs.GetBits(6); // frmsizcod

  if (fscod == 3 || frmsizcod > 37)
    return false;

  int bsid = bs.GetBits(5); // bsid

  if(bsid > 8)
    return false;

  framesize = AC3FrameSizeTable[frmsizcod][fscod] * 2;
  return true;
}

void cParserAC3::ParsePayload(unsigned char* payload, int length) {
  cBitStream bs(payload, AC3_HEADER_SIZE * 8);

  if (bs.GetBits(16) != 0x0B77)
    return;

  bs.SkipBits(16); // CRC
  int fscod = bs.GetBits(2);
  int frmsizecod = bs.GetBits(6);
  int bsid = bs.GetBits(5); // bsid

  if(bsid > 8)
    return;

  bs.SkipBits(3); // bitstream mode
  int acmod = bs.GetBits(3);

  if (fscod == 3 || frmsizecod > 37)
    return;

  if (acmod == AC3_CHMODE_STEREO)
  {
    bs.SkipBits(2); // skip dsurmod
  }
  else
  {
    if ((acmod & 1) && acmod != AC3_CHMODE_MONO)
      bs.SkipBits(2);
    if (acmod & 4)
      bs.SkipBits(2);
  }
  int lfeon = bs.GetBits(1);

  m_samplerate = AC3SampleRateTable[fscod];
  m_bitrate    = (AC3BitrateTable[frmsizecod>>1] * 1000);
  m_channels   = AC3ChannelsTable[acmod] + lfeon;

  int framesize = AC3FrameSizeTable[frmsizecod][fscod] * 2;

  m_duration = (framesize * 8 * 1000 * 90) / m_bitrate;

  m_demuxer->SetAudioInformation(m_channels, m_samplerate, m_bitrate, 0, 0);
}
