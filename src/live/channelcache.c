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

void cChannelCache::CreateDemuxers(cLiveStreamer* streamer) {
  streamer->Detach();

  // remove old demuxers
  for (std::list<cTSDemuxer*>::iterator i = streamer->m_Demuxers.begin(); i != streamer->m_Demuxers.end(); i++)
    delete *i;

  streamer->m_Demuxers.clear();
  streamer->SetPids(NULL);

  // create new stream demuxers
  for (iterator i = begin(); i != end(); i++)
  {
    cStreamInfo& info = i->second;
    cTSDemuxer* dmx = new cTSDemuxer(streamer, info);
    if (dmx != NULL)
    {
      dmx->info();
      streamer->m_Demuxers.push_back(dmx);
      streamer->AddPid(info.GetPID());
    }
  }

  streamer->Attach();
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

cChannelCache cChannelCache::GetFromCache(uint32_t channeluid) {
  Lock();
  cChannelCache result = m_cache[channeluid];
  Unlock();

  return result;
}
