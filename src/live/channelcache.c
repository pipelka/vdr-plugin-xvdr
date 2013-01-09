/*
 *      vdr-plugin-xvdr - XVDR server plugin for VDR
 *
 *      Copyright (C) 2011 Alexander Pipelka
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

#include "config/config.h"
#include "tools/hash.h"
#include "channelcache.h"
#include "livestreamer.h"

cMutex cChannelCache::m_access;
std::map<uint32_t, cChannelCache> cChannelCache::m_cache;

cChannelCache::cChannelCache() : m_bChanged(false) {
}

void cChannelCache::AddStream(const cStreamInfo& s) {
  if(s.GetPID() == 0 || s.GetType() == cStreamInfo::stNONE)
    return;

  cStreamInfo old = (*this)[s.GetPID()];
  (*this)[s.GetPID()] = s;

  m_bChanged = (old != s);
}

bool cChannelCache::IsParsed() {
  if(empty())
    return false;

  for (iterator i = begin(); i != end(); i++)
    if(!i->second.IsParsed())
      return false;

 return true;
}


void cChannelCache::CreateDemuxers(cLiveStreamer* streamer) {
  cChannelCache old;

  // remove old demuxers
  for (std::list<cTSDemuxer*>::iterator i = streamer->m_Demuxers.begin(); i != streamer->m_Demuxers.end(); i++) {
    old.AddStream(*(*i));
    delete *i;
  }

  streamer->m_Demuxers.clear();
  streamer->SetPids(NULL);

  // create new stream demuxers
  for (iterator i = begin(); i != end(); i++)
  {
    cStreamInfo& infonew = i->second;
    cStreamInfo& infoold = old[i->first];

    // reuse previous stream information
    if(infonew.GetPID() == infoold.GetPID() && infonew.GetType() == infoold.GetType()) {
      infonew = infoold;
    }

    cTSDemuxer* dmx = new cTSDemuxer(streamer, infonew);
    if (dmx != NULL)
    {
      dmx->info();
      streamer->m_Demuxers.push_back(dmx);
      streamer->AddPid(infonew.GetPID());
    }
  }
}

bool cChannelCache::operator ==(const cChannelCache& c) const {
  if (size() != c.size())
    return false;

  for(const_iterator i = begin(); i != end(); i++)
    if(!c.contains(i->second))
      return false;

  return true;
}

bool cChannelCache::ismetaof(const cChannelCache& c) const {
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

bool cChannelCache::contains(const cStreamInfo& s) const {
  const_iterator i = find(s.GetPID());

  if (i == end())
    return false;

  return (i->second == s);
}

void cChannelCache::AddToCache(uint32_t channeluid, const cChannelCache& channel) {
  Lock();
  m_cache[channeluid] = channel;
  Unlock();
}

void cChannelCache::AddToCache(const cChannel* channel) {
  cMutexLock lock(&m_access);

  uint32_t uid = CreateChannelUID(channel);
  std::map<uint32_t, cChannelCache>::iterator i = m_cache.find(uid);

  // channel already in cache
  if(i != m_cache.end())
    return;

  // create new cache item
  cChannelCache item;

  // add video stream
  int vpid = channel->Vpid();
  int vtype = channel->Vtype();

  item.AddStream(cStreamInfo(vpid, vtype == 0x02 ? cStreamInfo::stMPEG2VIDEO : vtype == 0x1b ? cStreamInfo::stH264 : cStreamInfo::stNONE));

  // add AC3 streams
  for(int i=0; channel->Dpid(i) != 0; i++) {
    item.AddStream(cStreamInfo(channel->Dpid(i), cStreamInfo::stAC3, channel->Dlang(i)));
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
   item.AddStream(cStreamInfo(channel->Spid(i), cStreamInfo::stDVBSUB, channel->Slang(i)));
  }

  AddToCache(uid, item);
}

cChannelCache cChannelCache::GetFromCache(uint32_t channeluid) {
  Lock();
  cChannelCache result = m_cache[channeluid];
  Unlock();

  return result;
}

void cChannelCache::SaveChannelCacheData() {
  std::fstream out;
  cString filename = AddDirectory(XVDRServerConfig.ConfigDirectory, CHANNEL_CACHE_FILE".bak");


  out.open(filename, std::ios_base::out | std::ios_base::binary | std::ios_base::trunc);

  if(!out.is_open()) {
    return;
  }

  Lock();

  out << m_cache.size() << std::endl;

  for(std::map<uint32_t, cChannelCache>::iterator i = m_cache.begin(); i != m_cache.end(); i++) {
    out << i->first << std::endl;
    out << i->second;
  }

  Unlock();

  cString filenamenew = AddDirectory(XVDRServerConfig.ConfigDirectory, CHANNEL_CACHE_FILE);

  rename(filename, filenamenew);
}

void cChannelCache::LoadChannelCacheData() {
  m_cache.clear();

  // preload cache with VDR channel entries
  Channels.Lock(false);
  for (cChannel *channel = Channels.First(); channel; channel = Channels.Next(channel)) {
    AddToCache(channel);
  }
  Channels.Unlock();

  // load cache
  std::fstream in;
  cString filename = AddDirectory(XVDRServerConfig.ConfigDirectory, CHANNEL_CACHE_FILE);

  in.open(filename, std::ios_base::in | std::ios_base::binary);

  if(!in.is_open()) {
    ERRORLOG("Unable to open channel cache data file (%s) !", (const char*)filename);
    return;
  }

  int c = 0;
  in >> c;

  // sanity check
  if(c > 10000)
    return;

  INFOLOG("Loading %i channels from cache", c);

  for(int i = 0; i < c; i++) {
    int uid = 0;
    in  >> uid;

    cChannelCache cache;
    in >> cache;

    m_cache[uid] = cache;
  }
}


std::fstream& operator<< (std::fstream& lhs, const cChannelCache& rhs) {
  lhs << (int)rhs.size() << std::endl;

  for(cChannelCache::const_iterator i = rhs.begin(); i != rhs.end(); i++) {
    lhs << i->second;
  }

  return lhs;
}

std::fstream& operator>> (std::fstream& lhs, cChannelCache& rhs) {
  rhs.clear();
  int c = 0;
  lhs >> c;

  for(int i = 0; i < c; i++) {
    cStreamInfo s;
    lhs >> s;
    rhs.AddStream(s);
  }
  return lhs;
}

