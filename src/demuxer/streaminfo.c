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

#include "streaminfo.h"
#include "config/config.h"
#include <string.h>

static const char* contentnames[] = {
  "NONE", "VIDEO", "AUDIO", "SUBTITLE", "TELETEXT"
};

static const char* typenames[] = {
  "NONE", "MPEG2AUDIO", "AC3", "EAC3", "AAC", "AAC", "MPEG2VIDEO", "H264", "DVBSUB", "TELETEXT"
};


cStreamInfo::cStreamInfo() {
  Initialize();
}

cStreamInfo::cStreamInfo(int pid, Type type, const char* lang) {
  Initialize();

  m_pid = pid;
  m_type = type;
  m_parsed = false;

  if(lang != NULL)
    strncpy(m_language, lang, 4);

  memchr(m_sps, 0, sizeof(m_sps));
  memchr(m_pps, 0, sizeof(m_pps));

  SetContent();
}

cStreamInfo::~cStreamInfo() {
}

void cStreamInfo::Initialize() {
  m_language[0]       = 0;
  m_audiotype         = 0;
  m_fpsscale          = 0;
  m_fpsrate           = 0;
  m_height            = 0;
  m_width             = 0;
  m_aspect            = 0.0f;
  m_channels          = 0;
  m_samplerate        = 0;
  m_bitrate           = 0;
  m_bitspersample     = 0;
  m_blockalign        = 0;
  m_subtitlingtype    = 0;
  m_compositionpageid = 0;
  m_ancillarypageid   = 0;
  m_pid               = 0;
  m_type              = stNONE;
  m_content           = scNONE;
  m_parsed            = false;
  m_spsLength         = 0;
  m_ppsLength         = 0;
  m_vpsLength         = 0;
}

bool cStreamInfo::operator ==(const cStreamInfo& rhs) const {
  // general comparison
  if(!ismetaof(rhs)) {
    return false;
  }

  switch(m_content) {
    case scNONE:
      return false;
    case scAUDIO:
      return
        (strcmp(m_language, rhs.m_language) == 0) &&
        (m_audiotype == rhs.m_audiotype) &&
        (m_channels == rhs.m_channels) &&
        (m_samplerate == rhs.m_samplerate);
    case scVIDEO:
      return
        (m_width == rhs.m_width) &&
        (m_height == rhs.m_height) &&
        (m_aspect == rhs.m_aspect) &&
        (m_fpsscale == rhs.m_fpsscale) &&
        (m_fpsrate == rhs.m_fpsrate);
    case scSUBTITLE:
      return
        (strcmp(m_language, rhs.m_language) == 0) &&
        (m_subtitlingtype == rhs.m_subtitlingtype) &&
        (m_compositionpageid == rhs.m_compositionpageid) &&
        (m_ancillarypageid == rhs.m_ancillarypageid);
    case scTELETEXT:
      return true;
  }
  return false;
}

bool cStreamInfo::ismetaof(const cStreamInfo& rhs) const {
  if(m_content != rhs.m_content) {
    return false;
  }

  if(m_type != rhs.m_type && (m_type != stAC3 && rhs.m_type != stEAC3)) {
    return false;
  }

  return (m_pid == rhs.m_pid);
}

bool cStreamInfo::operator !=(const cStreamInfo& rhs) const {
  return !((*this) == rhs);
}

void cStreamInfo::SetContent() {
  m_content = GetContent(m_type);
}

const cStreamInfo::Content cStreamInfo::GetContent(Type type) {
  if(type == stMPEG2AUDIO || type == stAC3 || type == stEAC3  || type == stAAC || type == stLATM) {
    return scAUDIO;
  }
  else if(type == stMPEG2VIDEO || type == stH264) {
    return scVIDEO;
  }
  else if(type == stDVBSUB) {
    return scSUBTITLE;
  }
  else if(type == stTELETEXT) {
    return scTELETEXT;
  }

  return scNONE;
}

const char* cStreamInfo::TypeName() {
  return TypeName(m_type);
}

const char* cStreamInfo::TypeName(const cStreamInfo::Type& type) {
  return typenames[type];
}

const char* cStreamInfo::ContentName(const cStreamInfo::Content& content) {
  return contentnames[content];
}

void cStreamInfo::info() const {
  char buffer[100];
  buffer[0] = 0;

  int scale = m_fpsscale;
  if(scale == 0) {
    scale = 1;
  }

  if(m_content == scAUDIO) snprintf(buffer, sizeof(buffer), "%i Hz, %i channels, Lang: %s", m_samplerate, m_channels, m_language);
  else if (m_content == scVIDEO) snprintf(buffer, sizeof(buffer), "%ix%i DAR: %.2f FPS: %.3f SPS/PPS: %i/%i bytes", m_width, m_height , m_aspect, (double)m_fpsrate / (double)scale, m_spsLength, m_ppsLength);
  else if (m_content == scSUBTITLE) snprintf(buffer, sizeof(buffer), "Lang: %s", m_language);
  else if (m_content == scTELETEXT) snprintf(buffer, sizeof(buffer), "TXT");
  else if (m_content == scNONE) snprintf(buffer, sizeof(buffer), "None");
  else snprintf(buffer, sizeof(buffer), "Unknown");

  INFOLOG("Stream: %s PID: %i %s (parsed: %s)", TypeName(m_type), m_pid, buffer, (m_parsed ? "yes" : "no"));
}

void cStreamInfo::SetSubtitlingDescriptor(unsigned char SubtitlingType, uint16_t CompositionPageId, uint16_t AncillaryPageId)
{
  m_subtitlingtype    = SubtitlingType;
  m_compositionpageid = CompositionPageId;
  m_ancillarypageid   = AncillaryPageId;
  m_parsed            = true;
}

MsgPacket& operator<< (MsgPacket& lhs, const cStreamInfo& rhs) {
  // write item sync
  lhs.put_U32(0xFEFEFEFE);

  // write general data
  lhs.put_U8(rhs.m_type);
  lhs.put_U8(rhs.m_content);
  lhs.put_U16(rhs.m_pid);
  lhs.put_U8(rhs.m_parsed);

  const char* lang = (rhs.m_language[0] == 0 ? "XXX" : rhs.m_language);

  // write specific data
  switch(rhs.m_content) {
    case cStreamInfo::scAUDIO:
      lhs.put_String(lang);
      lhs.put_U8(rhs.m_audiotype);
      lhs.put_U8(rhs.m_channels);
      lhs.put_U32(rhs.m_samplerate);
      lhs.put_U32(rhs.m_bitrate);
      lhs.put_U8(rhs.m_bitspersample);
      lhs.put_U32(rhs.m_blockalign);
      break;
    case cStreamInfo::scVIDEO:
      lhs.put_U32(rhs.m_fpsscale);
      lhs.put_U32(rhs.m_fpsrate);
      lhs.put_U16(rhs.m_height);
      lhs.put_U16(rhs.m_width);
      lhs.put_U64((unsigned long long)(rhs.m_aspect * 1000000000));
      lhs.put_U8(rhs.m_spsLength);
      if(rhs.m_spsLength > 0) {
        lhs.put_Blob((uint8_t*)rhs.m_sps, rhs.m_spsLength);
      }
      lhs.put_U8(rhs.m_ppsLength);
      if(rhs.m_ppsLength > 0) {
        lhs.put_Blob((uint8_t*)rhs.m_pps, rhs.m_ppsLength);
      }
      lhs.put_U8(rhs.m_vpsLength);
      if(rhs.m_vpsLength > 0) {
        lhs.put_Blob((uint8_t*)rhs.m_vps, rhs.m_vpsLength);
      }
      break;
    case cStreamInfo::scSUBTITLE:
      lhs.put_String(lang);
      lhs.put_U8(rhs.m_subtitlingtype);
      lhs.put_U16(rhs.m_compositionpageid);
      lhs.put_U16(rhs.m_ancillarypageid);
      break;
    case cStreamInfo::scTELETEXT:
      break;
    default:
      break;
  }

  return lhs;
}

MsgPacket& operator>> (MsgPacket& lhs, cStreamInfo& rhs) {
  unsigned int check = 0;
  check = lhs.get_U32();

  if(check != 0xFEFEFEFE)
    return lhs;

  rhs.Initialize();

  // read general data
  rhs.m_type = static_cast<cStreamInfo::Type>(lhs.get_U8());
  rhs.m_content = static_cast<cStreamInfo::Content>(lhs.get_U8());
  rhs.m_pid = lhs.get_U16();
  rhs.m_parsed = lhs.get_U8();

  // read specific data
  int at = 0;
  std::string lang;

  switch(rhs.m_content) {
    case cStreamInfo::scAUDIO:
      lang = lhs.get_String();
      rhs.m_audiotype = lhs.get_U8();
      rhs.m_channels = lhs.get_U8();
      rhs.m_samplerate = lhs.get_U32();
      rhs.m_bitrate = lhs.get_U32();
      rhs.m_bitspersample = lhs.get_U8();
      rhs.m_blockalign = lhs.get_U32();
      break;
    case cStreamInfo::scVIDEO:
      rhs.m_fpsscale = lhs.get_U32();
      rhs.m_fpsrate = lhs.get_U32();
      rhs.m_height = lhs.get_U16();
      rhs.m_width = lhs.get_U16();
      rhs.m_aspect = (double)lhs.get_U64() / 1000000000.0;
      rhs.m_spsLength = lhs.get_U8();
      if(rhs.m_spsLength > 0) {
        lhs.get_Blob(rhs.m_sps, rhs.m_spsLength);
      }
      rhs.m_ppsLength = lhs.get_U8();
      if(rhs.m_ppsLength > 0) {
        lhs.get_Blob(rhs.m_pps, rhs.m_ppsLength);
      }
      rhs.m_vpsLength = lhs.get_U8();
      if(rhs.m_vpsLength > 0) {
        lhs.get_Blob(rhs.m_vps, rhs.m_vpsLength);
      }
      break;
    case cStreamInfo::scSUBTITLE:
      lang = lhs.get_String();
      rhs.m_subtitlingtype = lhs.get_U8();
      rhs.m_compositionpageid = lhs.get_U16();
      rhs.m_ancillarypageid = lhs.get_U16();
      break;
    case cStreamInfo::scTELETEXT:
      break;
    default:
      break;
  }

  strncpy(rhs.m_language, lang.c_str(), sizeof(rhs.m_language));

  return lhs;
}
