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

#include "demuxer_MPEGAudio.h"
#include "config/config.h"

#define MPA_MONO 3

static const uint16_t FrequencyTable[3] = { 44100, 48000, 32000 };

static const uint16_t BitrateTable[2][3][15] =
{
  {
    {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448 },
    {0, 32, 48, 56, 64,  80,  96,  112, 128, 160, 192, 224, 256, 320, 384 },
    {0, 32, 40, 48, 56,  64,  80,  96,  112, 128, 160, 192, 224, 256, 320 }
  },
  {
    {0, 32, 48, 56, 64,  80,  96,  112, 128, 144, 160, 176, 192, 224, 256},
    {0, 8,  16, 24, 32,  40,  48,  56,  64,  80,  96,  112, 128, 144, 160},
    {0, 8,  16, 24, 32,  40,  48,  56,  64,  80,  96,  112, 128, 144, 160}
  }
};


cParserMPEG2Audio::cParserMPEG2Audio(cTSDemuxer *demuxer) : cParser(demuxer, 64 * 1024, 2048)
{
  m_headersize = 4;
}

bool cParserMPEG2Audio::ParseAudioHeader(uint8_t* buffer, int& channels, int& samplerate, int &bitrate, int& framesize)
{
  uint32_t header = ((buffer[0] << 24) | (buffer[1] << 16) | (buffer[2] <<  8) | buffer[3]);

  // syncword FFE
  if ((header & 0xfff00000) != 0xfff00000)
    return false;

  int lsf = 0;
  int mpeg25 = 0;

  // MPEG 2.5 ?
  if (header & (1<<20))
  {
    lsf = (header & (1<<19)) ? 0 : 1;
    mpeg25 = 0;
  }
  else
  {
    lsf = 1;
    mpeg25 = 1;
  }

  // get header properties
  int layer             = 4 - ((header >> 17) & 3);
  int sample_rate_index = (header >> 10) & 3;
  bool padding          = (header >> 9) & 1;
  samplerate            = FrequencyTable[sample_rate_index] >> (lsf + mpeg25);
  int bitrate_index     = (header >> 12) & 0xf;
  int mode              = (header >> 6) & 3;

  // valid layer ?
  if (layer == 0 || layer == 4)
    return false;

  // number of channels
  channels = 2 - (mode == MPA_MONO);

  // valid bit rate ?
  if (bitrate_index >= 15)
    return false;

  bitrate = BitrateTable[lsf][layer - 1][bitrate_index] * 1000;

  if(bitrate == 0 || samplerate == 0)
    return false;

  // frame size in bytes
  if(layer == 1)
    framesize = (12 * bitrate / samplerate + padding) * 4;
  else
    framesize = 144 * bitrate / samplerate + padding;

  return true;
}

bool cParserMPEG2Audio::CheckAlignmentHeader(unsigned char* buffer, int& framesize) {
  int channels, samplerate, bitrate;
  return ParseAudioHeader(buffer, channels, samplerate, bitrate, framesize);
}

void cParserMPEG2Audio::ParsePayload(unsigned char* payload, int length) {
  int framesize = 0;

  if(!ParseAudioHeader(payload, m_channels, m_samplerate, m_bitrate, framesize))
    return;

  m_duration = (framesize * 8 * 1000 * 90) / m_bitrate;

  m_demuxer->SetAudioInformation(m_channels, m_samplerate, m_bitrate, 0, 0);
}
