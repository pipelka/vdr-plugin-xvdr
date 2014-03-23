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

#ifndef XVDR_CHANNELCACHEITEM_H
#define XVDR_CHANNELCACHEITEM_H

#include <vdr/thread.h>
#include <vdr/channels.h>

#include "demuxer/demuxer.h"

#include <list>
#include <map>
#include <fstream>
#include <string.h>

class cLiveStreamer;

class cChannelCache : public std::map<int, cStreamInfo> {
public:

  cChannelCache();

  void AddStream(const cStreamInfo& s);

  void CreateDemuxers(cLiveStreamer* streamer);

  bool operator ==(const cChannelCache& c) const;

  bool ismetaof(const cChannelCache& c) const;

  bool contains(const cStreamInfo& s) const;

  bool changed() const { return m_bChanged; }

  bool IsParsed();

  void SetCaID(int caid);

  int GetCaID() const;

  static bool SetRealCaID(int caid, int ecmpid);

  static void LoadChannelCacheData();

  static void SaveChannelCacheData();

  static void AddToCache(uint32_t channeluid, const cChannelCache& channel);

  static void AddToCache(const cChannel* channel);

  static cChannelCache GetFromCache(uint32_t channeluid);

  static uint32_t FindByECM(int ecmpid, cChannelCache& result);

  static void AddECM(int ecmpid, uint32_t channeluid);

  static void gc();

private:

  static void Lock() { m_access.Lock(); }

  static void Unlock() { m_access.Unlock(); }

  static std::map<uint32_t, cChannelCache> m_cache;

  static std::map<int, uint32_t> m_ecm;

  static cMutex m_access;

  bool m_bChanged;

  int m_caid;
};

MsgPacket& operator<< (MsgPacket& lhs, const cChannelCache& rhs);
MsgPacket& operator>> (MsgPacket& lhs, cChannelCache& rhs);

#endif // XVDR_CHANNELCACHEITEM_H
