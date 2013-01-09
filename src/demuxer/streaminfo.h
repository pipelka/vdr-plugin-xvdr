/*
 *      vdr-plugin-xvdr - XVDR server plugin for VDR
 *
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

#ifndef XVDR_STREAMINFO_H
#define XVDR_STREAMINFO_H

#include <stdint.h>
#include <fstream>
#include <string>

class cStreamInfo {
public:

  enum Content
  {
    scNONE,
    scVIDEO,
    scAUDIO,
    scSUBTITLE,
    scTELETEXT
  };

  enum Type
  {
    stNONE,
    stMPEG2AUDIO,
    stAC3,
    stEAC3,
    stAAC,
    stLATM,
    stMPEG2VIDEO,
    stH264,
    stDVBSUB,
    stTELETEXT,
  };

public:

  cStreamInfo();

  cStreamInfo(int pid, Type type);

  bool operator ==(const cStreamInfo& rhs) const;

  bool ismetaof(const cStreamInfo& rhs) const;

  bool operator !=(const cStreamInfo& rhs) const;

  const int GetPID() const { return m_pid; }

  void SetContent();

  static const Content GetContent(Type type);

  const Content GetContent() const { return m_content; }

  const Type GetType() const { return m_type; }

  const char* TypeName();

  static const char* TypeName(const cStreamInfo::Type& type);

  static const char* ContentName(const cStreamInfo::Content& content);

  void info() const;

protected:

  Content m_content;   // stream content (e.g. scVIDEO)
  Type m_type;         // stream type (e.g. stAC3)
  int m_pid;           // transport stream pid

  char m_language[4];  // ISO 639 3-letter language code (empty string if undefined)
  uint8_t m_audiotype; // ISO 639 audio type

  int m_fpsscale;      // scale of 1000 and a rate of 29970 will result in 29.97 fps
  int m_fpsrate;
  int m_height;        // height of the stream reported by the demuxer
  int m_width;         // width of the stream reported by the demuxer
  float m_aspect;      // display aspect of stream

  int m_channels;      // number of audio channels (e.g. 6 for 5.1)
  int m_samplerate;    // number of audio samples per second (e.g. 48000)
  int m_bitrate;       // audio bitrate (e.g. 160000)
  int m_bitspersample; // number of bits per audio sample (e.g. 16)
  int m_blockalign;    // number of bytes per audio block

  bool m_parsed;       // stream parsed flag (if all stream data is known)

  unsigned char m_subtitlingtype; // subtitling type
  uint16_t m_compositionpageid;   // composition page id
  uint16_t m_ancillarypageid;     // ancillary page id

  friend class cLivePatFilter;

  friend std::fstream& operator<< (std::fstream& lhs, const cStreamInfo& rhs);

  friend std::fstream& operator>> (std::fstream& lhs, cStreamInfo& rhs);

private:

  void Initialize();

};

std::fstream& operator<< (std::fstream& lhs, const cStreamInfo& rhs);

std::fstream& operator>> (std::fstream& lhs, cStreamInfo& rhs);

#endif // XVDR_STREAMINFO_H
