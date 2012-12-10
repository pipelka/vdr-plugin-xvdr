/*
 *      vdr-plugin-xvdr - XVDR server plugin for VDR
 *
 *      Copyright (C) 2010 Alwin Esch (Team XBMC)
 *      Copyright (C) 2012 Aleaxander Pipelka
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

#include "demuxer_LATM.h"
#include "bitstream.h"
#include "aaccommon.h"

static uint32_t LATMGetValue(cBitstream *bs) {
  return bs->readBits(bs->readBits(2) * 8);
}

cParserLATM::cParserLATM(cTSDemuxer *demuxer) : cParser(demuxer, 64 * 1024, 8192)//, m_framelength(0)
{
}

bool cParserLATM::CheckAlignmentHeader(unsigned char* buffer, int& framesize) {
  if(!(buffer[0] == 0x56 && (buffer[1] & 0xe0) == 0xe0))
    return false;

  framesize = (buffer[1] & 0x1f) << 8 | buffer[2];
  framesize += 3; // add header size
  return true;
}

// dummy - handled in ParsePayload
void cParserLATM::SendPayload(unsigned char* payload, int length) {
}

void cParserLATM::ParsePayload(unsigned char* data, int len) {
  cBitstream bs(data, len * 8);

  bs.skipBits(24); // skip header

  if (!bs.readBits1())
    ReadStreamMuxConfig(&bs);

  int tmp;
  unsigned int slotLen = 0;
  do
  {
    tmp = bs.readBits(8);
    slotLen += tmp;
  } while (tmp == 255);

  if (slotLen * 8 > bs.remainingBits())
    return;

  if (m_curDTS == DVD_NOPTS_VALUE)
    return;

  // buffer for converted payload data
  int payloadlength = slotLen + 7;
  uint8_t* payload = (uint8_t*)malloc(payloadlength);

  // 7 bytes of ADTS header
  cBitstream out(payload, 56);

  out.putBits(0xfff, 12); // Sync marker
  out.putBits(0, 1);      // ID 0 = MPEG 4
  out.putBits(0, 2);      // Layer
  out.putBits(1, 1);      // Protection absent
  out.putBits(2, 2);      // AOT
  out.putBits(m_samplerateindex, 4);
  out.putBits(1, 1);      // Private bit
  out.putBits(m_channels, 3);
  out.putBits(1, 1);      // Original
  out.putBits(1, 1);      // Copy
  out.putBits(1, 1);      // Copyright identification bit
  out.putBits(1, 1);      // Copyright identification start
  out.putBits(slotLen, 13);
  out.putBits(0, 11);     // Buffer fullness
  out.putBits(0, 2);      // RDB in frame

  // copy AAC data
  uint8_t *buf = payload + 7;
  for (unsigned int i = 0; i < slotLen; i++)
    *buf++ = bs.readBits(8);

  // send converted payload packet
  cParser::SendPayload(payload, payloadlength);

  // free payload buffer
  free(payload);

  return;
}

void cParserLATM::ReadStreamMuxConfig(cBitstream *bs) {
  int AudioMuxVersion = bs->readBits(1);
  int AudioMuxVersion_A = 0;
  if (AudioMuxVersion)                       // audioMuxVersion
    AudioMuxVersion_A = bs->readBits(1);

  if(AudioMuxVersion_A)
    return;

  if (AudioMuxVersion)
    LATMGetValue(bs);                  // taraFullness

  bs->skipBits(1);                         // allStreamSameTimeFraming = 1
  bs->skipBits(6);                         // numSubFrames = 0
  bs->skipBits(4);                         // numPrograms = 0

  // for each program (which there is only on in DVB)
  bs->skipBits(3);                         // numLayer = 0

  // for each layer (which there is only on in DVB)
  if (!AudioMuxVersion)
    ReadAudioSpecificConfig(bs);
  else
    return;

  // these are not needed... perhaps
  int framelength = bs->readBits(3);
  switch (framelength)
  {
    case 0:
      bs->readBits(8);
      break;
    case 1:
      bs->readBits(9);
      break;
    case 3:
    case 4:
    case 5:
      bs->readBits(6);                 // celp_table_index
      break;
    case 6:
    case 7:
      bs->readBits(1);                 // hvxc_table_index
      break;
  }

  if (bs->readBits(1))
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
        esc = bs->readBits(1);
        bs->skipBits(8);
      } while (esc);
    }
  }

  if (bs->readBits(1))                   // crc present?
    bs->skipBits(8);                     // config_crc
}

void cParserLATM::ReadAudioSpecificConfig(cBitstream *bs) {
  bs->readBits(5); // audio object type

  m_samplerateindex = bs->readBits(4);

  if (m_samplerateindex == 0xf)
    return;

  m_samplerate = aac_samplerates[m_samplerateindex];
  m_duration = 1024 * 90000 / m_samplerate;

  int channelindex = bs->readBits(4);
  if(channelindex > 7) channelindex = 0;
  m_channels = aac_channels[channelindex];

  bs->skipBits(1);      //framelen_flag
  if (bs->readBits1())  // depends_on_coder
    bs->skipBits(14);

  if (bs->readBits(1))  // ext_flag
    bs->skipBits(1);    // ext3_flag

  m_demuxer->SetAudioInformation(m_channels, m_samplerate, 0, 0, 0);
}
