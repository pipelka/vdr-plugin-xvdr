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

cStreamInfo::cStreamInfo(int pid, Type type) {
  Initialize();

  m_pid = pid;
  m_type = type;
  m_parsed = false;

  SetContent();
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
  return (m_pid == rhs.m_pid && m_type == rhs.m_type && m_content == rhs.m_content);
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

  if(m_content == scAUDIO) snprintf(buffer, sizeof(buffer), "%i Hz, %i channels, Lang: %s", m_samplerate, m_channels, m_language);
  else if (m_content == scVIDEO) snprintf(buffer, sizeof(buffer), "%ix%i DAR: %.2f", m_width, m_height , m_aspect);
  else if (m_content == scSUBTITLE) snprintf(buffer, sizeof(buffer), "Lang: %s", m_language);
  else if (m_content == scTELETEXT) snprintf(buffer, sizeof(buffer), "TXT");
  else if (m_content == scNONE) snprintf(buffer, sizeof(buffer), "None");
  else snprintf(buffer, sizeof(buffer), "Unknown");

  INFOLOG("Stream: %s PID: %i %s", TypeName(m_type), m_pid, buffer);
}

std::fstream& operator<< (std::fstream& lhs, const cStreamInfo& rhs) {
  // write item sync
  lhs << (int)0xFEFEFEFE << std::endl;

  // write general data
  lhs << rhs.m_type << std::endl;
  lhs << rhs.m_content << std::endl;
  lhs << rhs.m_pid << std::endl;

  // write specific data
  switch(rhs.m_content) {
    case cStreamInfo::scAUDIO:
      lhs << rhs.m_language << std::endl;
      lhs << rhs.m_audiotype << std::endl;
      lhs << rhs.m_channels << std::endl;
      lhs << rhs.m_samplerate << std::endl;
      lhs << rhs.m_bitrate << std::endl;
      lhs << rhs.m_bitspersample << std::endl;
      lhs << rhs.m_blockalign << std::endl;
      break;
    case cStreamInfo::scVIDEO:
      lhs << rhs.m_fpsscale << std::endl;
      lhs << rhs.m_fpsrate << std::endl;
      lhs << rhs.m_height << std::endl;
      lhs << rhs.m_width << std::endl;
      lhs << (unsigned long long)(rhs.m_aspect * 1000000000) << std::endl;
      break;
    case cStreamInfo::scSUBTITLE:
      lhs << rhs.m_language << std::endl;
      lhs << rhs.m_subtitlingtype << std::endl;
      lhs << rhs.m_compositionpageid << std::endl;
      lhs << rhs.m_ancillarypageid << std::endl;
      break;
    case cStreamInfo::scTELETEXT:
      break;
    default:
      break;
  }

  return lhs;
}

std::fstream& operator>> (std::fstream& lhs, cStreamInfo& rhs) {
  unsigned long long a;
  unsigned int check = 0;
  lhs >> check;

  if(check != 0xFEFEFEFE)
    return lhs;

  // read general data
  int t = 0;
  lhs >> t;
  rhs.m_type = static_cast<cStreamInfo::Type>(t);

  int c = 0;
  lhs >> c;
  rhs.m_content = static_cast<cStreamInfo::Content>(c);

  lhs >> rhs.m_pid;

  // read specific data
  switch(rhs.m_content) {
    case cStreamInfo::scAUDIO:
      lhs >> rhs.m_language;
      lhs >> rhs.m_audiotype;
      lhs >> rhs.m_channels;
      lhs >> rhs.m_samplerate;
      lhs >> rhs.m_bitrate;
      lhs >> rhs.m_bitspersample;
      lhs >> rhs.m_blockalign;
      break;
    case cStreamInfo::scVIDEO:
      lhs >> rhs.m_fpsscale;
      lhs >> rhs.m_fpsrate;
      lhs >> rhs.m_height;
      lhs >> rhs.m_width;
      lhs >> a;
      rhs.m_aspect = (double)a / 1000000000.0;
      break;
    case cStreamInfo::scSUBTITLE:
      lhs >> rhs.m_language;
      lhs >> rhs.m_subtitlingtype;
      lhs >> rhs.m_compositionpageid;
      lhs >> rhs.m_ancillarypageid;
      break;
    case cStreamInfo::scTELETEXT:
      break;
    default:
      break;
  }

  rhs.m_parsed = true;

  return lhs;
}
