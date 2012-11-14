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
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#ifndef XVDR_CHANNELCACHEITEM_H
#define XVDR_CHANNELCACHEITEM_H

#include <vdr/thread.h>
#include "demuxer/demuxer.h"
#include <list>
#include <map>
#include <string.h>

class cLiveStreamer;

struct StreamInfo {
  StreamInfo() {
    pid = 0;
    type = stNONE;
    memset(lang, 0, sizeof(lang));
    audioType = 0;
    subtitlingType = 0;
    compositionPageId = 0;
    ancillaryPageId = 0;
    width = 0;
    height = 0;
    dar = 0.0;
  }

  bool operator ==(const struct StreamInfo& b) const {
    return 
      (pid == b.pid) && 
      (type == b.type) && 
      (strcmp(lang, b.lang) == 0) &&
      (audioType == b.audioType) &&
      (subtitlingType == b.subtitlingType) &&
      (ancillaryPageId == b.ancillaryPageId);/* &&
      (width == b.width) &&
      (height == b.height) &&
      (dar == b.dar);*/
  }

  bool operator !=(const struct StreamInfo& b) const {
    return !((*this) == b);
  }

  int pid;
  eStreamType type;
  char lang[4];
  int audioType;
  int subtitlingType;
  int compositionPageId;
  int ancillaryPageId;
  int width;
  int height;
  double dar;
};

class cChannelCache : public std::map<int, struct StreamInfo> {
public:

  cChannelCache();

  void AddStream(const struct StreamInfo& s);

  void CreateDemuxers(cLiveStreamer* streamer);

  cTSDemuxer* CreateDemuxer(cLiveStreamer* streamer, const struct StreamInfo& s) const;

  bool operator ==(const cChannelCache& c) const;

  bool contains(const struct StreamInfo& s) const;

  bool changed() const { return m_bChanged; }

  static void AddToCache(uint32_t channeluid, const cChannelCache& channel);

  static cChannelCache GetFromCache(uint32_t channeluid);

private:

  static void Lock() { m_access.Lock(); }

  static void Unlock() { m_access.Unlock(); }

  static std::map<uint32_t, cChannelCache> m_cache;

  static cMutex m_access;

  bool m_bChanged;
};

#endif // XVDR_CHANNELCACHEITEM_H
