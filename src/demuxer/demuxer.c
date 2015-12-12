/*
 *      vdr-plugin-xvdr - XVDR server plugin for VDR
 *
 *      Copyright (C) 2010 Alwin Esch (Team XBMC)
 *      Copyright (C) 2010 Alexander Pipelka
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

#include <vdr/remux.h>

#include "config/config.h"
#include "live/livestreamer.h"
#include "demuxer.h"
#include "parser.h"
#include "pes.h"
#include "demuxer_LATM.h"
#include "demuxer_ADTS.h"
#include "demuxer_AC3.h"
#include "demuxer_H264.h"
#include "demuxer_H265.h"
#include "demuxer_MPEGAudio.h"
#include "demuxer_MPEGVideo.h"
#include "demuxer_PES.h"
#include "demuxer_Subtitle.h"

#define DVD_TIME_BASE 1000000

cTSDemuxer::cTSDemuxer(cTSDemuxer::Listener* streamer, const cStreamInfo& info) : cStreamInfo(info), m_Streamer(streamer) {
  m_pesParser = CreateParser(m_type);
  SetContent();
}

cTSDemuxer::cTSDemuxer(cTSDemuxer::Listener* streamer, cStreamInfo::Type type, int pid) : cStreamInfo(pid, type), m_Streamer(streamer) {
  m_pesParser = CreateParser(m_type);
}

cParser* cTSDemuxer::CreateParser(cStreamInfo::Type type) {
  switch(type)
  {
    case stMPEG2VIDEO:
      return new cParserMPEG2Video(this);
    case stH264:
      return new cParserH264(this);
    case stH265:
      return new cParserH265(this);
    case stMPEG2AUDIO:
      return new cParserMPEG2Audio(this);
    case stAAC:
      return new cParserADTS(this);
    case stLATM:
      return new cParserLATM(this);
    case stAC3:
    case stEAC3:
      return new cParserAC3(this);
    case stTELETEXT:
      m_parsed = true;
      return new cParserPES(this);
    case stDVBSUB:
      return new cParserSubtitle(this);
    default:
      ERRORLOG("Unrecognized type %i", m_type);
      m_content = scNONE;
      m_type = stNONE;
      break;
  }

  return NULL;
}

cTSDemuxer::~cTSDemuxer()
{
  delete m_pesParser;
}

int64_t cTSDemuxer::Rescale(int64_t a)
{
  uint64_t b = DVD_TIME_BASE;
  uint64_t c = 90000;

  return (a * b)/c;
}

void cTSDemuxer::SendPacket(sStreamPacket *pkt)
{
  // raw pts/dts
  pkt->rawdts = pkt->dts;
  pkt->rawpts = pkt->pts;

  int64_t dts = (pkt->dts == DVD_NOPTS_VALUE) ? pkt->dts : Rescale(pkt->dts);
  int64_t pts = (pkt->pts == DVD_NOPTS_VALUE) ? pkt->pts : Rescale(pkt->pts);

  // Rescale
  pkt->type     = m_type;
  pkt->content  = m_content;
  pkt->pid      = GetPID();
  pkt->dts      = dts;
  pkt->pts      = pts;
  pkt->duration = Rescale(pkt->duration);

  m_Streamer->sendStreamPacket(pkt);
}

bool cTSDemuxer::ProcessTSPacket(unsigned char *data)
{
  if (data == NULL)
    return false;

  bool pusi  = TsPayloadStart(data);

  int bytes = TS_SIZE - TsPayloadOffset(data);

  if(bytes < 0 || bytes >= TS_SIZE)
    return false;

  if (TsIsScrambled(data)) {
    return false;
  }

  if (TsError(data))
  {
    ERRORLOG("transport error");
    return false;
  }

  if (!TsHasPayload(data))
  {
    DEBUGLOG("no payload, size %d", bytes);
    return true;
  }

  // strip ts header
  data += TS_SIZE - bytes;

  // valid packet ?
  if (pusi && !PesIsHeader(data))
    return false;

  /* Parse the data */
  if (m_pesParser)
    m_pesParser->Parse(data, bytes, pusi);

  return true;
}

void cTSDemuxer::SetLanguageDescriptor(const char *language, uint8_t atype)
{
  m_language[0] = language[0];
  m_language[1] = language[1];
  m_language[2] = language[2];
  m_language[3] = 0;
  m_audiotype = atype;
}

void cTSDemuxer::SetVideoInformation(int FpsScale, int FpsRate, int Height, int Width, float Aspect, int num, int den)
{
  // check for sane picture information
  if(Width < 320 || Height < 240 || num <= 0 || den <= 0 || Aspect < 0)
    return;

  // only register changed video information
  if(Width == m_width && Height == m_height && Aspect == m_aspect && FpsScale == m_fpsscale && FpsRate == m_fpsrate)
    return;

  INFOLOG("--------------------------------------");
  INFOLOG("NEW PICTURE INFORMATION:");
  INFOLOG("Picture Width: %i", Width);
  INFOLOG("Picture Height: %i", Height);

  if(num != 1 || den != 1)
    INFOLOG("Pixel Aspect: %i:%i", num, den);

  if(Aspect == 0)
    INFOLOG("Unknown Display Aspect Ratio");
  else 
    INFOLOG("Display Aspect Ratio: %.2f", Aspect);

  if(FpsScale != 0 && FpsRate != 0) {
    INFOLOG("Frames per second: %.2lf", (double)FpsRate / (double)FpsScale);
  }

  INFOLOG("--------------------------------------");

  m_fpsscale = FpsScale;
  m_fpsrate  = FpsRate;
  m_height   = Height;
  m_width    = Width;
  m_aspect   = Aspect;
  m_parsed   = true;

  m_Streamer->RequestStreamChange();
}

void cTSDemuxer::SetAudioInformation(int Channels, int SampleRate, int BitRate, int BitsPerSample, int BlockAlign)
{
  // only register changed audio information
  if(Channels == m_channels && SampleRate == m_samplerate && BitRate == m_bitrate)
    return;

  INFOLOG("--------------------------------------");
  INFOLOG("NEW AUDIO INFORMATION:");
  INFOLOG("Channels: %i", Channels);
  INFOLOG("Samplerate: %i Hz", SampleRate);
  if(BitRate > 0)
    INFOLOG("Bitrate: %i bps", BitRate);
  INFOLOG("--------------------------------------");

  m_channels      = Channels;
  m_samplerate    = SampleRate;
  m_blockalign    = BlockAlign;
  m_bitrate       = BitRate;
  m_bitspersample = BitsPerSample;
  m_parsed        = true;

  m_Streamer->RequestStreamChange();
}

void cTSDemuxer::SetVideoDecoderData(uint8_t* sps, int spsLength, uint8_t* pps, int ppsLength, uint8_t* vps, int vpsLength) {
  if(sps != NULL) {
    m_spsLength = spsLength;
    memcpy(m_sps, sps, spsLength);
  }

  if(pps != NULL) {
    m_ppsLength = ppsLength;
    memcpy(m_pps, pps, ppsLength);
  }
  
  if(vps != NULL) {
    m_vpsLength = vpsLength;
    memcpy(m_vps, vps, vpsLength);
  }
}

uint8_t* cTSDemuxer::GetVideoDecoderSPS(int& length) {
  length = m_spsLength;
  return m_spsLength == 0 ? NULL : m_sps;
}

uint8_t* cTSDemuxer::GetVideoDecoderPPS(int& length) {
  length = m_ppsLength;
  return m_ppsLength == 0 ? NULL : m_pps;
}

uint8_t* cTSDemuxer::GetVideoDecoderVPS(int& length) {
  length = m_vpsLength;
  return m_vpsLength == 0 ? NULL : m_vps;
}
