/*
 *      vdr-plugin-xvdr - XVDR server plugin for VDR
 *
 *      Copyright (C) 20120 Alexander Pipelka
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

#include "demuxer_MPEGVideo.h"
#include "vdr/tools.h"
#include "pes.h"

#define MPEG2_SEQUENCE_START 0x000001B3
#define MPEG2_PICTURE_START  0x00000100

// frame durations
static const unsigned int framedurations[16] = {
  0, 3753, 3750, 3600, 3003, 3000, 1800, 1501, 1500, 0, 0, 0, 0, 0, 0, 0
};

// frame rates
static const unsigned int framerates[16][2] = {
  {0, 0}, {24000, 1001}, {24, 1}, {25, 1}, {30000, 1001}, {30, 1}, {50, 1}, {60000, 1001}, {60, 1},
  {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}
};

// aspect ratios
static const double aspectratios[16] = {
  0, 1.0, 1.333333333, 1.777777778, 2.21, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static int GetFrameType(unsigned char* data, int length) {
  cBitStream bs(data, length * 8);
  bs.SkipBits(32); // skip picture start code
  bs.SkipBits(10); // skip temporal reference

  return bs.GetBits(3);
}

static cStreamInfo::FrameType ConvertFrameType(int frametype) {
  switch (frametype) {
    case 1:
      return cStreamInfo::ftIFRAME;
    case 2:
      return cStreamInfo::ftPFRAME;
    case 3:
      return cStreamInfo::ftBFRAME;
    case 4:
      return cStreamInfo::ftDFRAME;
    default:
      break;
  }

  return cStreamInfo::ftUNKNOWN;
}

cParserMPEG2Video::cParserMPEG2Video(cTSDemuxer *demuxer) : cParserPES(demuxer, 512* 1024), m_pdiff(0), m_lastDTS(DVD_NOPTS_VALUE) {
}

cStreamInfo::FrameType cParserMPEG2Video::ParsePicture(unsigned char* data, int length) {
  int frametype = GetFrameType(data, length);

  // get I,P frames distance
  if(frametype < 3 && m_curDTS != DVD_NOPTS_VALUE && m_curPTS != DVD_NOPTS_VALUE) {
    m_pdiff = m_curPTS - m_curDTS;
    m_lastDTS = m_curDTS;
    return ConvertFrameType(frametype);
  }

  // extrapolate DTS
  if(m_curDTS == DVD_NOPTS_VALUE && m_duration != 0) {
    m_curDTS = PtsAdd(m_lastDTS, m_duration);
    m_lastDTS = m_curDTS;
  }

  // B frames have DTS = PTS
  if(frametype == 3 && m_curPTS == DVD_NOPTS_VALUE)
    m_curPTS = m_curDTS;

  // extrapolate PTS of I/P frame
  if(frametype < 3 && m_curPTS == DVD_NOPTS_VALUE)
    m_curPTS = PtsAdd(m_curDTS, m_pdiff);

  return ConvertFrameType(frametype);
}

void cParserMPEG2Video::ParsePayload(unsigned char* data, int length) {
  // lookup sequence start code
  int o = FindStartCode(data, length, 0, MPEG2_SEQUENCE_START);
  if (o >= 0) {
    // skip sequence start code
    o += 4;

    // parse picture sequence (width, height, aspect, duration)
    ParseSequenceStart(data + o, length - 4);
  }

  // just to be sure, exit if there's isn't any duration
  if(m_duration == 0)
    return;

  // check for picture start codes
  int s = FindStartCode(data, length, 0, MPEG2_PICTURE_START);
  int e = FindStartCode(data, length, s + 4, MPEG2_PICTURE_START);
  o = s;
  s = 0;

  // divide this packet into frames
  while(e != -1) {

    // parse and send payload data
    m_frametype = ParsePicture(data + o, e - o);
    cParser::SendPayload(data + s, e - s);

    // get next picture offsets
    s = e;
    o = s;
    e = FindStartCode(data, length, s + 4, MPEG2_PICTURE_START);

    // increment timestamps
    m_curPTS = DVD_NOPTS_VALUE;
    m_curDTS = PtsAdd(m_curDTS, m_duration);
  }

  // append last part
  m_frametype = ParsePicture(data + o, length - o);
  cParser::SendPayload(data + s, length - s);
}

void cParserMPEG2Video::SendPayload(unsigned char* payload, int length) {
}

void cParserMPEG2Video::ParseSequenceStart(unsigned char* data, int length) {
  cBitStream bs(data, length * 8);

  if (bs.Length() < 32)
    return;

  int width  = bs.GetBits(12);
  int height = bs.GetBits(12);

  // display aspect ratio
  double DAR = aspectratios[bs.GetBits(4)];

  // frame rate / duration
  int index = bs.GetBits(4);
  m_duration = framedurations[index];

  m_demuxer->SetVideoInformation(framerates[index][1], framerates[index][0], height, width, DAR, 1, 1);
}
