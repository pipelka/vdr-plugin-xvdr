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
#include "xvdr/xvdrchannels.h"
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

  if(channeluid != 0)
    m_cache[channeluid] = channel;

  Unlock();
}

void cChannelCache::AddToCache(const cChannel* channel) {
  cMutexLock lock(&m_access);

  uint32_t uid = CreateChannelUID(channel);

  // ignore invalid channels
  if(uid == 0)
    return;

  std::map<uint32_t, cChannelCache>::iterator i = m_cache.find(uid);

  // valid channel already in cache
  if(i != m_cache.end()) {
    if(i->second.size() != 0) {
      return;
    }
  }

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
   cStreamInfo stream(channel->Spid(i), cStreamInfo::stDVBSUB, channel->Slang(i));

   stream.SetSubtitlingDescriptor(
     channel->SubtitlingType(i),
     channel->CompositionPageId(i),
     channel->AncillaryPageId(i));

   item.AddStream(stream);
  }

  AddToCache(uid, item);
}

cChannelCache cChannelCache::GetFromCache(uint32_t channeluid) {
  static cChannelCache empty;

  Lock();

  std::map<uint32_t, cChannelCache>::iterator i = m_cache.find(channeluid);
  if(i == m_cache.end()) {
    Unlock();
    return empty;
  }

  cChannelCache result = m_cache[channeluid];
  Unlock();

  return result;
}

void cChannelCache::SaveChannelCacheData() {
  cString filename = AddDirectory(XVDRServerConfig.CacheDirectory, CHANNEL_CACHE_FILE".bak");

  int fd = open(*filename, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if(fd == -1) {
    ERRORLOG("Unable to open channel cache data file (%s) !", (const char*)filename);
    return;
  }

  Lock();

  MsgPacket* p = new MsgPacket;
  p->put_String("V2");
  p->put_U32(m_cache.size());

  for(std::map<uint32_t, cChannelCache>::iterator i = m_cache.begin(); i != m_cache.end(); i++) {
    p->put_U32(i->first);
    *p << i->second;
  }

  Unlock();

  p->write(fd, 1000);
  delete p;
  close(fd);

  cString filenamenew = AddDirectory(XVDRServerConfig.CacheDirectory, CHANNEL_CACHE_FILE);

  rename(filename, filenamenew);
}

void cChannelCache::gc() {
  cMutexLock lock(&m_access);
  std::map<uint32_t, cChannelCache> m_newcache;

  INFOLOG("channel cache garbage collection ...");
  INFOLOG("before: %zu channels in cache", m_cache.size());

  // remove orphaned cache entries
  XVDRChannels.Lock(false);
  cChannels *channels = XVDRChannels.Get();

  for (cChannel *channel = channels->First(); channel; channel = channels->Next(channel)) {
    uint32_t uid = CreateChannelUID(channel);

    // ignore invalid channels
    if(uid == 0)
      continue;

    // lookup channel in current cache
    std::map<uint32_t, cChannelCache>::iterator i = m_cache.find(uid);
    if(i == m_cache.end())
      continue;

    // add to new cache if it exists
    m_newcache[uid] = i->second;
  }
  XVDRChannels.Unlock();

  // regenerate cache
  m_cache.clear();
  std::map<uint32_t, cChannelCache>::iterator i;

  for(i = m_newcache.begin(); i != m_newcache.end(); i++) {
    m_cache[i->first] = i->second;
  }

  INFOLOG("after: %zu channels in cache", m_cache.size());
}

void cChannelCache::LoadChannelCacheData() {
  m_cache.clear();

  // load cache
  cString filename = AddDirectory(XVDRServerConfig.CacheDirectory, CHANNEL_CACHE_FILE);

  int fd = open(*filename, O_RDONLY);
  if(fd == -1) {
    ERRORLOG("Unable to open channel cache data file (%s) !", (const char*)filename);
    return;
  }

  MsgPacket* p = MsgPacket::read(fd, 1000);
  if(p == NULL) {
    ERRORLOG("Unable to load channel cache data file (%s) !", (const char*)filename);
    close(fd);
    return;
  }

  std::string version = p->get_String();
  if(version != "V2") {
    INFOLOG("old channel cache detected - skipped");
    return;
  }

  uint32_t c = p->get_U32();

  // sanity check
  if(c > 10000) {
    delete p;
    close(fd);
    return;
  }

  INFOLOG("Loading %u channels from cache", c);

  for(uint32_t i = 0; i < c; i++) {
    uint32_t uid = p->get_U32();

    cChannelCache cache;
    *p >> cache;

    if(uid != 0)
      m_cache[uid] = cache;
  }

  delete p;
  close(fd);

  gc();
}

MsgPacket& operator<< (MsgPacket& lhs, const cChannelCache& rhs) {
  lhs.put_U32((int)rhs.size());

  for(cChannelCache::const_iterator i = rhs.begin(); i != rhs.end(); i++) {
    lhs << i->second;
  }

  return lhs;
}

MsgPacket& operator>> (MsgPacket& lhs, cChannelCache& rhs) {
  rhs.clear();
  uint32_t c = lhs.get_U32();

  for(uint32_t i = 0; i < c; i++) {
    cStreamInfo s;
    lhs >> s;
    rhs.AddStream(s);
  }
  return lhs;
}
