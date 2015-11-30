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

#ifndef XVDR_DEMUXER_H
#define XVDR_DEMUXER_H

#include <stdint.h>
#include <list>
#include "streaminfo.h"

class cParser;

#define DVD_NOPTS_VALUE    (-1LL<<52) // should be possible to represent in both double and __int64

struct sStreamPacket
{
  sStreamPacket() {
    type = cStreamInfo::stNONE;
    content = cStreamInfo::scNONE;
    frametype = cStreamInfo::ftUNKNOWN;
  }

  cStreamInfo::FrameType frametype;
  cStreamInfo::Type type;
  cStreamInfo::Content content;

  int64_t   pid;
  int64_t   dts;
  int64_t   pts;
  int64_t   rawdts;
  int64_t   rawpts;  
  int       duration;

  uint8_t  *data;
  int       size;
};

class cTSDemuxer : public cStreamInfo
{
public:

  class Listener {
  public:

    virtual ~Listener() {};

    virtual void sendStreamPacket(sStreamPacket* p) = 0;

    virtual void RequestStreamChange() = 0;

  };

private:

  Listener* m_Streamer;
  cParser* m_pesParser;

  int64_t Rescale(int64_t a);

public:
  cTSDemuxer(Listener *streamer, cStreamInfo::Type type, int pid);
  cTSDemuxer(Listener *streamer, const cStreamInfo& info);
  virtual ~cTSDemuxer();

  bool ProcessTSPacket(unsigned char *data);
  void SendPacket(sStreamPacket *pkt);

  void SetLanguageDescriptor(const char *language, uint8_t atype);
  const char *GetLanguage() { return m_language; }
  uint8_t GetAudioType() { return m_audiotype; }
  bool IsParsed() const { return m_parsed; }

  /* Video Stream Information */
  void SetVideoInformation(int FpsScale, int FpsRate, int Height, int Width, float Aspect, int num, int den);
  uint32_t GetFpsScale() const { return m_fpsscale; }
  uint32_t GetFpsRate() const { return m_fpsrate; }
  uint32_t GetHeight() const { return m_height; }
  uint32_t GetWidth() const { return m_width; }
  double GetAspect() const { return m_aspect; }

  /* Audio Stream Information */
  void SetAudioInformation(int Channels, int SampleRate, int BitRate, int BitsPerSample, int BlockAlign);
  uint32_t GetChannels() const { return m_channels; }
  uint32_t GetSampleRate() const { return m_samplerate; }
  uint32_t GetBlockAlign() const { return m_blockalign; }
  uint32_t GetBitRate() const { return m_bitrate; }
  uint32_t GetBitsPerSample() const { return m_bitspersample; }

  /* Subtitle related stream information */
  unsigned char SubtitlingType() const { return m_subtitlingtype; }
  uint16_t CompositionPageId() const { return m_compositionpageid; }
  uint16_t AncillaryPageId() const { return m_ancillarypageid; }

  /* Decoder specific data */
  void SetVideoDecoderData(uint8_t* sps, int spsLength, uint8_t* pps, int ppsLength);
  uint8_t* GetVideoDecoderSPS(int& length);
  uint8_t* GetVideoDecoderPPS(int& length);

private:

  cParser* CreateParser(cStreamInfo::Type type);

};

#endif // XVDR_DEMUXER_H
