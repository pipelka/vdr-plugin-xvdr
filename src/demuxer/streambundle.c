/*
 *      vdr-plugin-xvdr - XVDR server plugin for VDR
 *
 *      Copyright (C) 2015 Alexander Pipelka
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

#include "demuxer/streambundle.h"

cStreamBundle::cStreamBundle() : m_bChanged(false) {
}

void cStreamBundle::AddStream(const cStreamInfo& s) {
  if(s.GetPID() == 0 || s.GetType() == cStreamInfo::stNONE)
    return;

  // allow only one video stream
  if(s.GetContent() == cStreamInfo::scVIDEO) {
    for(iterator i = begin(); i != end(); i++) {
      if(i->second.GetContent() == cStreamInfo::scVIDEO && i->second.GetPID() != s.GetPID()) {
        return;
      }
    }
  }

  cStreamInfo old = (*this)[s.GetPID()];
  (*this)[s.GetPID()] = s;

  m_bChanged = (old != s);
}

bool cStreamBundle::IsParsed() {
  if(empty())
    return false;

  for (iterator i = begin(); i != end(); i++)
    if(!i->second.IsParsed())
      return false;

 return true;
}

bool cStreamBundle::operator ==(const cStreamBundle& c) const {
  if (size() != c.size())
    return false;

  for(const_iterator i = begin(); i != end(); i++)
    if(!c.contains(i->second))
      return false;

  return true;
}

bool cStreamBundle::ismetaof(const cStreamBundle& c) const {
  if (size() != c.size())
    return false;

  for(const_iterator i = begin(); i != end(); i++) {
    const_iterator it = c.find(i->second.GetPID());
    if(it == c.end())
      return false;

    if(!i->second.ismetaof(it->second))
      return false;
  }

  return true;
}

bool cStreamBundle::contains(const cStreamInfo& s) const {
  const_iterator i = find(s.GetPID());

  if (i == end())
    return false;

  return (i->second == s);
}

cStreamBundle cStreamBundle::FromChannel(const cChannel* channel) {
  cStreamBundle item;

  // add video stream
  int vpid = channel->Vpid();
  int vtype = channel->Vtype();

  item.AddStream(cStreamInfo(vpid, vtype == 0x02 ? cStreamInfo::stMPEG2VIDEO : vtype == 0x1b ? cStreamInfo::stH264 : cStreamInfo::stNONE));

  // add (E)AC3 streams
  for(int i=0; channel->Dpid(i) != 0; i++) {
    int dtype = channel->Dtype(i);
    item.AddStream(cStreamInfo(channel->Dpid(i), 
      dtype == 0x6A ? cStreamInfo::stAC3 :
      dtype == 0x7A ? cStreamInfo::stEAC3 :
      cStreamInfo::stNONE,
      channel->Dlang(i)));
  }

  // add audio streams
  for(int i=0; channel->Apid(i) != 0; i++) {
    int atype = channel->Atype(i);
    item.AddStream(cStreamInfo(channel->Apid(i), 
      atype == 0x04 ? cStreamInfo::stMPEG2AUDIO :
      atype == 0x03 ? cStreamInfo::stMPEG2AUDIO :
      atype == 0x0f ? cStreamInfo::stAAC :
      atype == 0x11 ? cStreamInfo::stLATM :
      cStreamInfo::stNONE,
      channel->Alang(i)));
  }

  // add teletext stream
  if(channel->Tpid() != 0) {
    item.AddStream(cStreamInfo(channel->Tpid(), cStreamInfo::stTELETEXT));
  }

  // add subtitle streams
  for(int i=0; channel->Spid(i) != 0; i++) {
   cStreamInfo stream(channel->Spid(i), cStreamInfo::stDVBSUB, channel->Slang(i));

   stream.SetSubtitlingDescriptor(
     channel->SubtitlingType(i),
     channel->CompositionPageId(i),
     channel->AncillaryPageId(i));

   item.AddStream(stream);
  }

  return item;
}

MsgPacket& operator<< (MsgPacket& lhs, const cStreamBundle& rhs) {
  lhs.put_U32((int)rhs.size());

  for(cStreamBundle::const_iterator i = rhs.begin(); i != rhs.end(); i++) {
    lhs << i->second;
  }

  return lhs;
}

MsgPacket& operator>> (MsgPacket& lhs, cStreamBundle& rhs) {
  rhs.clear();
  uint32_t c = lhs.get_U32();

  for(uint32_t i = 0; i < c; i++) {
    cStreamInfo s;
    lhs >> s;
    rhs.AddStream(s);
  }
  return lhs;
}
