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
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#ifndef XVDR_DEMUXER_H
#define XVDR_DEMUXER_H

#include <stdint.h>

class cLiveStreamer;
class cParser;

#define DVD_NOPTS_VALUE    (-1LL<<52) // should be possible to represent in both double and __int64

enum eStreamContent
{
  scNONE,
  scVIDEO,
  scAUDIO,
  scSUBTITLE,
  scTELETEXT,
  scPROGRAMM
};

enum eStreamType
{
  stNONE = -1,
  stMPEG2AUDIO = 0,
  stAC3,
  stEAC3,
  stAAC,
  stLATM,
  stDTS,
  stMPEG2VIDEO = 10,
  stH264,
  stDVBSUB = 20,
  stTEXTSUB,
  stTELETEXT,
};

#define PKT_I_FRAME 1
#define PKT_P_FRAME 2
#define PKT_B_FRAME 3
#define PKT_NTYPES  4
struct sStreamPacket
{
  sStreamPacket() {
    frametype = 0;
    type = stNONE;
    content = scNONE;
  }

  eStreamType type;
  eStreamContent content;

  int64_t   pid;
  int64_t   dts;
  int64_t   pts;
  int       duration;

  uint8_t   commercial;
  uint8_t   componentindex;
  uint8_t   frametype;

  uint8_t  *data;
  int       size;
};

class cTSDemuxer
{
private:
  cLiveStreamer        *m_Streamer;
  eStreamContent        m_streamContent;
  eStreamType           m_streamType;
  int                   m_PID;
  bool                  m_parsed;

  cParser              *m_pesParser;

  char                  m_language[4];  // ISO 639 3-letter language code (empty string if undefined)
  uint8_t               m_audiotype;    // ISO 639 audio type

  int                   m_FpsScale;     // scale of 1000 and a rate of 29970 will result in 29.97 fps
  int                   m_FpsRate;
  int                   m_Height;       // height of the stream reported by the demuxer
  int                   m_Width;        // width of the stream reported by the demuxer
  float                 m_Aspect;       // display aspect of stream

  int                   m_Channels;
  int                   m_SampleRate;
  int                   m_BitRate;
  int                   m_BitsPerSample;
  int                   m_BlockAlign;

  unsigned char         m_subtitlingType;
  uint16_t              m_compositionPageId;
  uint16_t              m_ancillaryPageId;

  int64_t Rescale(int64_t a);

public:
  cTSDemuxer(cLiveStreamer *streamer, eStreamType type, int pid);
  virtual ~cTSDemuxer();

  bool ProcessTSPacket(unsigned char *data);
  void SendPacket(sStreamPacket *pkt);

  void SetLanguageDescriptor(const char *language, uint8_t atype);
  const char *GetLanguage() { return m_language; }
  uint8_t GetAudioType() { return m_audiotype; }
  const eStreamContent Content() const { return m_streamContent; }
  const eStreamType Type() const { return m_streamType; }
  const int GetPID() const { return m_PID; }
  bool IsParsed() const { return m_parsed; }

  /* Video Stream Information */
  void SetVideoInformation(int FpsScale, int FpsRate, int Height, int Width, float Aspect, int num, int den);
  uint32_t GetFpsScale() const { return m_FpsScale; }
  uint32_t GetFpsRate() const { return m_FpsRate; }
  uint32_t GetHeight() const { return m_Height; }
  uint32_t GetWidth() const { return m_Width; }
  double GetAspect() const { return m_Aspect; }

  /* Audio Stream Information */
  void SetAudioInformation(int Channels, int SampleRate, int BitRate, int BitsPerSample, int BlockAlign);
  uint32_t GetChannels() const { return m_Channels; }
  uint32_t GetSampleRate() const { return m_SampleRate; }
  uint32_t GetBlockAlign() const { return m_BlockAlign; }
  uint32_t GetBitRate() const { return m_BitRate; }
  uint32_t GetBitsPerSample() const { return m_BitsPerSample; }

  /* Subtitle related stream information */
  void SetSubtitlingDescriptor(unsigned char SubtitlingType, uint16_t CompositionPageId, uint16_t AncillaryPageId);
  unsigned char SubtitlingType() const { return m_subtitlingType; }
  uint16_t CompositionPageId() const { return m_compositionPageId; }
  uint16_t AncillaryPageId() const { return m_ancillaryPageId; }
};

#endif // XVDR_DEMUXER_H
