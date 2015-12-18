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

#include "channelcache.h"
#include "config/config.h"
#include "tools/hash.h"
#include "xvdr/xvdrchannels.h"

cMutex cChannelCache::m_access;
std::map<uint32_t, cStreamBundle> cChannelCache::m_cache;

void cChannelCache::AddToCache(uint32_t channeluid, const cStreamBundle& channel) {
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

  auto i = m_cache.find(uid);

  // valid channel already in cache
  if(i != m_cache.end()) {
    if(i->second.size() != 0) {
      return;
    }
  }

  // create new cache item
  cStreamBundle item = cStreamBundle::FromChannel(channel);

  AddToCache(uid, item);
}

cStreamBundle cChannelCache::GetFromCache(uint32_t channeluid) {
  static cStreamBundle empty;

  Lock();

  auto i = m_cache.find(channeluid);
  if(i == m_cache.end()) {
    Unlock();
    return empty;
  }

  cStreamBundle result = m_cache[channeluid];
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

  for(std::map<uint32_t, cStreamBundle>::iterator i = m_cache.begin(); i != m_cache.end(); i++) {
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
  std::map<uint32_t, cStreamBundle> m_newcache;

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
    std::map<uint32_t, cStreamBundle>::iterator i = m_cache.find(uid);
    if(i == m_cache.end())
      continue;

    // add to new cache if it exists
    m_newcache[uid] = i->second;
  }
  XVDRChannels.Unlock();

  // regenerate cache
  m_cache.clear();

  for(auto i = m_newcache.begin(); i != m_newcache.end(); i++) {
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

    cStreamBundle bundle;
    *p >> bundle;

    if(uid != 0)
      m_cache[uid] = bundle;
  }

  delete p;
  close(fd);

  gc();
}
