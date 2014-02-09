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

#include "demuxer_LATM.h"
#include "vdr/tools.h"
#include "aaccommon.h"

static uint32_t LATMGetValue(cBitStream *bs) {
  return bs->GetBits(bs->GetBits(2) * 8);
}

static void putBits(uint8_t* buffer, int &offset, int val, int num) {
  while(num > 0) {
    num--;

    if(val & (1 << num))
      buffer[offset / 8] |= 1 << (7 - (offset & 7));
    else
      buffer[offset / 8] &= ~(1 << (7 - (offset & 7)));

    offset++;
  }
}

cParserLATM::cParserLATM(cTSDemuxer *demuxer) : cParser(demuxer, 64 * 1024, 8192)//, m_framelength(0)
{
}

bool cParserLATM::CheckAlignmentHeader(unsigned char* buffer, int& framesize) {
  cBitStream bs(buffer, 24 * 8);

  // read sync
  if(bs.GetBits(11) != 0x2B7) {
    return false;
  }

  // read frame size
  framesize = bs.GetBits(13);
  framesize += 3; // add header size

  return true;
}

// dummy - handled in ParsePayload
void cParserLATM::SendPayload(unsigned char* payload, int length) {
}

void cParserLATM::ParsePayload(unsigned char* data, int len) {
  cBitStream bs(data, len * 8);

  bs.SkipBits(24); // skip header

  if (!bs.GetBit())
    ReadStreamMuxConfig(&bs);

  int tmp;
  unsigned int slotLen = 0;
  do
  {
    tmp = bs.GetBits(8);
    slotLen += tmp;
  } while (tmp == 255);

  if (slotLen * 8 > (bs.Length() - (unsigned)bs.Index()))
    return;

  if (m_curDTS == DVD_NOPTS_VALUE)
    return;

  // buffer for converted payload data
  int payloadlength = slotLen + 7;
  uint8_t* payload = (uint8_t*)malloc(payloadlength);

  // 7 bytes of ADTS header
  int offset = 0;
  putBits(payload, offset, 0xfff, 12); // Sync marker
  putBits(payload, offset, 0, 1);      // ID 0 = MPEG 4
  putBits(payload, offset, 0, 2);      // Layer
  putBits(payload, offset, 1, 1);      // Protection absent
  putBits(payload, offset, 2, 2);      // AOT
  putBits(payload, offset, m_samplerateindex, 4);
  putBits(payload, offset, 1, 1);      // Private bit
  putBits(payload, offset, m_channels, 3);
  putBits(payload, offset, 1, 1);      // Original
  putBits(payload, offset, 1, 1);      // Copy
  putBits(payload, offset, 1, 1);      // Copyright identification bit
  putBits(payload, offset, 1, 1);      // Copyright identification start
  putBits(payload, offset, slotLen, 13);
  putBits(payload, offset, 0, 11);     // Buffer fullness
  putBits(payload, offset, 0, 2);      // RDB in frame

  // copy AAC data
  uint8_t *buf = payload + 7;
  for (unsigned int i = 0; i < slotLen; i++)
    *buf++ = bs.GetBits(8);

  // send converted payload packet
  cParser::SendPayload(payload, payloadlength);

  // free payload buffer
  free(payload);

  return;
}

void cParserLATM::ReadStreamMuxConfig(cBitStream *bs) {
  int AudioMuxVersion = bs->GetBits(1);
  int AudioMuxVersion_A = 0;
  if (AudioMuxVersion)                       // audioMuxVersion
    AudioMuxVersion_A = bs->GetBits(1);

  if(AudioMuxVersion_A)
    return;

  if (AudioMuxVersion)
    LATMGetValue(bs);                  // taraFullness

  bs->SkipBits(1);                         // allStreamSameTimeFraming = 1
  bs->SkipBits(6);                         // numSubFrames = 0
  bs->SkipBits(4);                         // numPrograms = 0

  // for each program (which there is only on in DVB)
  bs->SkipBits(3);                         // numLayer = 0

  // for each layer (which there is only on in DVB)
  if (!AudioMuxVersion)
    ReadAudioSpecificConfig(bs);
  else
    return;

  // these are not needed... perhaps
  int framelength = bs->GetBits(3);
  switch (framelength)
  {
    case 0:
      bs->GetBits(8);
      break;
    case 1:
      bs->GetBits(9);
      break;
    case 3:
    case 4:
    case 5:
      bs->GetBits(6);                 // celp_table_index
      break;
    case 6:
    case 7:
      bs->GetBits(1);                 // hvxc_table_index
      break;
  }

  if (bs->GetBits(1))
  {                   // other data?
    if (AudioMuxVersion)
    {
      LATMGetValue(bs);              // other_data_bits
    }
    else
    {
      int esc;
      do
      {
        esc = bs->GetBits(1);
        bs->SkipBits(8);
      } while (esc);
    }
  }

  if (bs->GetBits(1))                   // crc present?
    bs->SkipBits(8);                     // config_crc
}

void cParserLATM::ReadAudioSpecificConfig(cBitStream *bs) {
  bs->GetBits(5); // audio object type

  m_samplerateindex = bs->GetBits(4);

  if (m_samplerateindex == 0xf)
    return;

  m_samplerate = aac_samplerates[m_samplerateindex];
  m_duration = 1024 * 90000 / m_samplerate;

  int channelindex = bs->GetBits(4);
  if(channelindex > 7) channelindex = 0;
  m_channels = aac_channels[channelindex];

  bs->SkipBits(1);      //framelen_flag
  if (bs->GetBit())  // depends_on_coder
    bs->SkipBits(14);

  if (bs->GetBits(1))  // ext_flag
    bs->SkipBits(1);    // ext3_flag

  m_demuxer->SetAudioInformation(m_channels, m_samplerate, 0, 0, 0);
}
