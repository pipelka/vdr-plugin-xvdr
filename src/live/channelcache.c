#include "channelcache.h"
#include "livestreamer.h"
#include "livereceiver.h"

cMutex cChannelCache::m_access;
std::map<uint32_t, cChannelCache> cChannelCache::m_cache;

cChannelCache::cChannelCache() : m_bChanged(false) {
}

void cChannelCache::AddStream(const struct StreamInfo& s) {
  if(s.pid == 0 || s.type == stNONE)
    return;

  StreamInfo old = (*this)[s.pid];
  (*this)[s.pid] = s;

  m_bChanged = (old != s);
}

void cChannelCache::CreateDemuxers(cLiveStreamer* streamer) {
  // remove old demuxers
  for (std::list<cTSDemuxer*>::iterator i = streamer->m_Demuxers.begin(); i != streamer->m_Demuxers.end(); i++)
    delete *i;

  streamer->m_Demuxers.clear();
  streamer->m_Receiver->SetPids(NULL);

  // create new stream demuxers
  for (iterator i = begin(); i != end(); i++)
  {
    StreamInfo& info = i->second;
    cTSDemuxer* dmx = CreateDemuxer(streamer, info);
    if (dmx != NULL)
      streamer->m_Demuxers.push_back(dmx);
      streamer->m_Receiver->AddPid(info.pid);
  }
}

cTSDemuxer* cChannelCache::CreateDemuxer(cLiveStreamer* streamer, const struct StreamInfo& info) const {
  cTSDemuxer* stream = NULL;

  switch (info.type)
  {
    // hande video streams
    case stMPEG2VIDEO:
    case stH264:
      stream = new cTSDemuxer(streamer, info.type, info.pid);
      break;

    // handle audio streams
    case stMPEG2AUDIO:
    case stAC3:
    case stEAC3:
    case stDTS:
    case stAAC:
    case stLATM:
      stream = new cTSDemuxer(streamer, info.type, info.pid);
      stream->SetLanguageDescriptor(info.lang, info.audioType);
      break;

    // subtitles
    case stDVBSUB:
      stream = new cTSDemuxer(streamer, info.type, info.pid);
      stream->SetLanguageDescriptor(info.lang, info.audioType);
      stream->SetSubtitlingDescriptor(info.subtitlingType, info.compositionPageId, info.ancillaryPageId);
      break;

    // teletext
    case stTELETEXT:
      stream = new cTSDemuxer(streamer, info.type, info.pid);

      // add teletext pid if there is a CAM connected
      // (some broadcasters encrypt teletext data)
      /*cCamSlot* cam = streamer->m_Device->CamSlot();
      if(cam != NULL)
        cam->AddPid(m_Channel->Sid(), info.pid, 0x06);*/

      break;

    // unsupported stream
    default:
      break;
  }

  return stream;
}

bool cChannelCache::operator ==(const cChannelCache& c) const {
  if (size() != c.size())
    return false;

  for(const_iterator i = begin(); i != end(); i++)
    if(!c.contains(i->second))
      return false;

  return true;
}

bool cChannelCache::contains(const struct StreamInfo& s) const {
  const_iterator i = find(s.pid);

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
