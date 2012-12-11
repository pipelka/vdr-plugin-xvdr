/*
 *      vdr-plugin-xvdr - XVDR server plugin for VDR
 *
 *      Copyright (C) 2010 Alwin Esch (Team XBMC)
 *      Copyright (C) 2010, 2011 Alexander Pipelka
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

#include <stdlib.h>
#include <sys/ioctl.h>
#include <time.h>
#include <string.h>
#include <map>
#include <vdr/i18n.h>
#include <vdr/remux.h>
#include <vdr/channels.h>
#include <vdr/timers.h>

#ifdef __FreeBSD__
#include <sys/endian.h>
#else
#include <endian.h>
#endif

#include "config/config.h"
#include "net/msgpacket.h"
#include "xvdr/xvdrcommand.h"
#include "tools/hash.h"

#include "livestreamer.h"
#include "livepatfilter.h"
#include "livequeue.h"
#include "channelcache.h"

cLiveStreamer::cLiveStreamer(int priority, uint32_t timeout)
 : cThread("cLiveStreamer stream processor")
 , cRingBufferLinear(MEGABYTE(10), TS_SIZE * 2, true)
 , cReceiver(NULL, priority)
 , m_scanTimeout(timeout)
{
  m_Priority        = priority;
  m_Device          = NULL;
  m_Queue           = NULL;
  m_PatFilter       = NULL;
  m_startup         = true;
  m_SignalLost      = false;
  m_LangStreamType  = stMPEG2AUDIO;
  m_LanguageIndex   = -1;
  m_uid             = 0;
  m_ready           = false;

  m_requestStreamChange = false;


  if(m_scanTimeout == 0)
    m_scanTimeout = XVDRServerConfig.stream_timeout;

  SetTimeouts(0, 100);
}

cLiveStreamer::~cLiveStreamer()
{
  DEBUGLOG("Started to delete live streamer");

  // clear buffer
  Clear();

  cTimeMs t;
  Cancel(-1);

  if (m_Device)
  {
    Detach();

    if (m_PatFilter)
    {
      DEBUGLOG("Detaching Live Filter");
      m_Device->Detach(m_PatFilter);
    }
    else
    {
      DEBUGLOG("No live filter present");
    }

    if (m_PatFilter)
    {
      DEBUGLOG("Deleting Live Filter");
      DELETENULL(m_PatFilter);
    }

    for (std::list<cTSDemuxer*>::iterator i = m_Demuxers.begin(); i != m_Demuxers.end(); i++)
    {
      if ((*i) != NULL)
      {
        DEBUGLOG("Deleting stream demuxer for pid=%i and type=%i", (*i)->GetPID(), (*i)->Type());
        delete (*i);
      }
    }
    m_Demuxers.clear();

  }

  delete m_Queue;

  DEBUGLOG("Finished to delete live streamer (took %llu ms)", t.Elapsed());
}

void cLiveStreamer::RequestStreamChange()
{
  m_requestStreamChange = true;
}

void cLiveStreamer::Action(void)
{
  int size              = 0;
  int used              = 0;
  unsigned char *buf    = NULL;
  m_startup             = true;

  cTimeMs last_info;
  last_info.Set(0);

  while (Running())
  {
    size = 0;
    used = 0;
    buf = Get(size);

    if (!IsAttached())
    {
      INFOLOG("returning from streamer thread, receiver is no more attached");
      break;
    }

    if(!IsStarting() && (m_last_tick.Elapsed() > (uint64_t)(m_scanTimeout*1000)) && !m_SignalLost)
    {
      INFOLOG("timeout. signal lost!");
      sendStatus(XVDR_STREAM_STATUS_SIGNALLOST);
      m_SignalLost = true;
    }

    DEBUGLOG("cLiveStreamer: Got %i bytes", size);

    // no data
    if (buf == NULL || size <= TS_SIZE)
      continue;

    // Sync to TS packet
    while (size > TS_SIZE)
    {
      if (buf[0] == TS_SYNC_BYTE && buf[TS_SIZE] == TS_SYNC_BYTE)
        break;
      used++;
      buf++;
      size--;
    }

    // TS packet sync not found !
    if (buf[0] != TS_SYNC_BYTE)
    {
      Del(used);
      continue;
    }

    while (size >= TS_SIZE)
    {
      if(!Running())
      {
        break;
      }

      unsigned int ts_pid = TsPid(buf);

      m_FilterMutex.Lock();
      cTSDemuxer *demuxer = FindStreamDemuxer(ts_pid);
      //INFOLOG("TS PID: %i", ts_pid);
      if (demuxer)
      {
        demuxer->ProcessTSPacket(buf);
      }
      m_FilterMutex.Unlock();

      buf += TS_SIZE;
      size -= TS_SIZE;
      used += TS_SIZE;
    }
    Del(used);

    if(last_info.Elapsed() >= 10*1000 && IsReady())
    {
      last_info.Set(0);
      sendStreamInfo();
      sendSignalInfo();
    }
  }
}

int cLiveStreamer::StreamChannel(const cChannel *channel, int sock)
{
  if (channel == NULL)
  {
    ERRORLOG("Starting streaming of channel without valid channel");
    return XVDR_RET_ERROR;
  }

  m_uid      = CreateChannelUID(channel);

  // check if any device is able to decrypt the channel - code taken from VDR
  int NumUsableSlots = 0;

  if (channel->Ca() >= CA_ENCRYPTED_MIN) {
    for (cCamSlot *CamSlot = CamSlots.First(); CamSlot; CamSlot = CamSlots.Next(CamSlot)) {
      if (CamSlot->ModuleStatus() == msReady) {
        if (CamSlot->ProvidesCa(channel->Caids())) {
          if (!ChannelCamRelations.CamChecked(channel->GetChannelID(), CamSlot->SlotNumber())) {
            NumUsableSlots++;
          }
       }
      }
    }
    if (!NumUsableSlots) {
      ERRORLOG("Unable to decrypt channel %i - %s", channel->Number(), channel->Name());
      return XVDR_RET_ENCRYPTED;
    }
  }

  // get device for this channel
  m_Device = cDevice::GetDevice(channel, m_Priority, true);

  // try a bit harder if we can't find a device
  if(m_Device == NULL)
    m_Device = cDevice::GetDevice(channel, m_Priority, false);

  INFOLOG("--------------------------------------");
  INFOLOG("Channel streaming request: %i - %s", channel->Number(), channel->Name());

  if (m_Device == NULL)
  {
    ERRORLOG("Can't get device for channel %i - %s", channel->Number(), channel->Name());

    // return status "recording running" if there is an active timer
    time_t now = time(NULL);
    if(Timers.GetMatch(now) != NULL)
      return XVDR_RET_RECRUNNING;

    return XVDR_RET_DATALOCKED;
  }

  INFOLOG("Found available device %d", m_Device->DeviceNumber() + 1);

  if (!m_Device->SwitchChannel(channel, false))
  {
    ERRORLOG("Can't switch to channel %i - %s", channel->Number(), channel->Name());
    return XVDR_RET_ERROR;
  }

  // create send queue
  if (m_Queue == NULL)
  {
    m_Queue = new cLiveQueue(sock);
    m_Queue->Start();
  }

  m_PatFilter = new cLivePatFilter(this, channel);

  // get cached demuxer data
  DEBUGLOG("Creating demuxers");
  cChannelCache cache = cChannelCache::GetFromCache(m_uid);
  if(cache.size() != 0) {
    cache.CreateDemuxers(this);
    RequestStreamChange();
  }

  DEBUGLOG("Starting PAT scanner");
  m_Device->AttachFilter(m_PatFilter);
  Attach();

  INFOLOG("Successfully switched to channel %i - %s", channel->Number(), channel->Name());
  return XVDR_RET_OK;
}

cTSDemuxer *cLiveStreamer::FindStreamDemuxer(int Pid)
{
  for (std::list<cTSDemuxer*>::iterator i = m_Demuxers.begin(); i != m_Demuxers.end(); i++)
    if ((*i) != NULL && (*i)->GetPID() == Pid)
      return (*i);

  return NULL;
}

void cLiveStreamer::Activate(bool On)
{
  if (On)
  {
    DEBUGLOG("VDR active, sending stream start message");
    Start();
  }
  else
  {
    DEBUGLOG("VDR inactive, sending stream end message");
    Cancel(5);
  }
}

void cLiveStreamer::Attach(void)
{
  DEBUGLOG("%s", __FUNCTION__);
  if (m_Device)
  {
    m_Device->Detach(this);
    m_Device->AttachReceiver(this);
  }
}

void cLiveStreamer::Detach(void)
{
  DEBUGLOG("%s", __FUNCTION__);
  if (m_Device)
  {
    m_Device->Detach(this);
  }
}

void cLiveStreamer::sendStreamPacket(sStreamPacket *pkt)
{
  bool bReady = IsReady();

  if(!bReady || pkt == NULL || pkt->size == 0)
    return;

  // Send stream information as the first packet on startup
  if (IsStarting() && bReady)
  {
    INFOLOG("streaming of channel started");
    m_last_tick.Set(0);
    m_requestStreamChange = true;
    m_startup = false;
  }

  // send stream change on demand
  if(m_requestStreamChange)
    sendStreamChange();

  // if a audio or video packet was sent, the signal is restored
  if(m_SignalLost && (pkt->content == scVIDEO || pkt->content == scAUDIO)) {
    INFOLOG("signal restored");
    sendStatus(XVDR_STREAM_STATUS_SIGNALRESTORED);
    m_SignalLost = false;
    m_requestStreamChange = true;
    m_last_tick.Set(0);
    return;
  }

  if(m_SignalLost)
    return;

  // initialise stream packet
  MsgPacket* packet = new MsgPacket(XVDR_STREAM_MUXPKT, XVDR_CHANNEL_STREAM);
  packet->disablePayloadCheckSum();

  // write stream data
  packet->put_U16(pkt->pid);
  packet->put_S64(pkt->pts);
  packet->put_S64(pkt->dts);

  // write payload into stream packet
  packet->put_U32(pkt->size);
  packet->put_Blob(pkt->data, pkt->size);

  m_Queue->Add(packet);
  m_last_tick.Set(0);
}

void cLiveStreamer::sendStreamChange()
{
  MsgPacket* resp = new MsgPacket(XVDR_STREAM_CHANGE, XVDR_CHANNEL_STREAM);

  DEBUGLOG("sendStreamChange");

  // reorder streams as preferred
  reorderStreams(m_LanguageIndex, m_LangStreamType);

  for (std::list<cTSDemuxer*>::iterator idx = m_Demuxers.begin(); idx != m_Demuxers.end(); idx++)
  {
    cTSDemuxer* stream = (*idx);

    if (stream == NULL)
      continue;

    int streamid = stream->GetPID();
    resp->put_U32(streamid);

    switch(stream->Type())
    {
      case stMPEG2AUDIO:
        resp->put_String("MPEG2AUDIO");
        resp->put_String(stream->GetLanguage());
        // for future protocol versions: add audio_type
        //resp->put_U8(stream->GetAudioType());
        DEBUGLOG("MPEG2AUDIO: %i (%s)", streamid, stream->GetLanguage());
        break;

      case stMPEG2VIDEO:
        resp->put_String("MPEG2VIDEO");
        resp->put_U32(stream->GetFpsScale());
        resp->put_U32(stream->GetFpsRate());
        resp->put_U32(stream->GetHeight());
        resp->put_U32(stream->GetWidth());
        resp->put_S64(stream->GetAspect() * 10000.0);
        DEBUGLOG("MPEG2VIDEO: %i", streamid);
        break;

      case stAC3:
        resp->put_String("AC3");
        resp->put_String(stream->GetLanguage());
        // for future protocol versions: add audio_type
        //resp->put_U8(stream->GetAudioType());
        DEBUGLOG("AC3: %i (%s)", streamid, stream->GetLanguage());
        break;

      case stH264:
        resp->put_String("H264");
        resp->put_U32(stream->GetFpsScale());
        resp->put_U32(stream->GetFpsRate());
        resp->put_U32(stream->GetHeight());
        resp->put_U32(stream->GetWidth());
        resp->put_S64(stream->GetAspect() * 10000.0);
        DEBUGLOG("H264: %i", streamid);
        break;

      case stDVBSUB:
        resp->put_String("DVBSUB");
        resp->put_String(stream->GetLanguage());
        resp->put_U32(stream->CompositionPageId());
        resp->put_U32(stream->AncillaryPageId());
        DEBUGLOG("DVBSUB: %i", streamid);
        break;

      case stTELETEXT:
        resp->put_String("TELETEXT");
        DEBUGLOG("TELETEXT: %i", streamid);
        break;

      case stAAC:
        resp->put_String("AAC");
        resp->put_String(stream->GetLanguage());
        // for future protocol versions: add audio_type
        //resp->put_U8(stream->GetAudioType());
        DEBUGLOG("AAC: %i (%s)", streamid, stream->GetLanguage());
        break;

      case stLATM:
        resp->put_String("AAC");
        resp->put_String(stream->GetLanguage());
        // for future protocol versions: add audio_type
        //resp->put_U8(stream->GetAudioType());
        DEBUGLOG("LATM: %i (%s)", streamid, stream->GetLanguage());
        break;

      case stEAC3:
        resp->put_String("EAC3");
        resp->put_String(stream->GetLanguage());
        // for future protocol versions: add audio_type
        //resp->put_U8(stream->GetAudioType());
        DEBUGLOG("EAC3: %i (%s)", streamid, stream->GetLanguage());
        break;

      case stDTS:
        resp->put_String("DTS");
        resp->put_String(stream->GetLanguage());
        // for future protocol versions: add audio_type
        //resp->put_U8(stream->GetAudioType());
        DEBUGLOG("DTS: %i (%s)", streamid, stream->GetLanguage());
        break;

      default:
        break;
    }
  }

  m_Queue->Add(resp);
  m_requestStreamChange = false;

  sendStreamInfo();
}

void cLiveStreamer::sendStatus(int status)
{
  MsgPacket* packet = new MsgPacket(XVDR_STREAM_STATUS, XVDR_CHANNEL_STREAM);
  packet->put_U32(status);
  m_Queue->Add(packet);
}

void cLiveStreamer::sendSignalInfo()
{
  MsgPacket* resp = new MsgPacket(XVDR_STREAM_SIGNALINFO, XVDR_CHANNEL_STREAM);

  int DeviceNumber = m_Device->DeviceNumber() + 1;
  int Strength = m_Device->SignalStrength();
  int Quality = m_Device->SignalQuality();

  resp->put_String(*cString::sprintf("%s #%d - %s", 
#if VDRVERSNUM < 10728
#warning "VDR versions < 1.7.28 do not support all features"
			"Unknown",
			DeviceNumber,
			"Unknown"));
#else
			(const char*)m_Device->DeviceType(),
			DeviceNumber,
			(const char*)m_Device->DeviceName()));
#endif

  // Quality:
  // 4 - NO LOCK
  // 3 - NO SYNC
  // 2 - NO VITERBI
  // 1 - NO CARRIER
  // 0 - NO SIGNAL

  if(Quality == -1)
  {
    resp->put_String("Unknown (Incompatible device)");
    Quality = 0;
  }
  else
    resp->put_String(*cString::sprintf("%s:%s:%s:%s:%s", 
			(Quality > 4) ? "LOCKED" : "-",
			(Quality > 0) ? "SIGNAL" : "-",
			(Quality > 1) ? "CARRIER" : "-",
			(Quality > 2) ? "VITERBI" : "-",
			(Quality > 3) ? "SYNC" : "-"));

  resp->put_U32((Quality << 16 ) / 100); // TODO: remove need to scale
  resp->put_U32((Strength << 16 ) / 100); // TODO: remove need to scale
  resp->put_U32(0);
  resp->put_U32(0);

  DEBUGLOG("sendSignalInfo");
  m_Queue->Add(resp);
}

void cLiveStreamer::sendStreamInfo()
{
  if(m_Demuxers.size() == 0)
    return;

  MsgPacket* resp = new MsgPacket(XVDR_STREAM_CONTENTINFO, XVDR_CHANNEL_STREAM);

  // reorder streams as preferred
  reorderStreams(m_LanguageIndex, m_LangStreamType);

  for (std::list<cTSDemuxer*>::iterator idx = m_Demuxers.begin(); idx != m_Demuxers.end(); idx++)
  {
    cTSDemuxer* stream = (*idx);

    if (stream == NULL)
      continue;

    switch (stream->Content())
    {
      case scAUDIO:
        resp->put_U32(stream->GetPID());
        resp->put_String(stream->GetLanguage());
        resp->put_U32(stream->GetChannels());
        resp->put_U32(stream->GetSampleRate());
        resp->put_U32(stream->GetBlockAlign());
        resp->put_U32(stream->GetBitRate());
        resp->put_U32(stream->GetBitsPerSample());
        break;

      case scVIDEO:
        resp->put_U32(stream->GetPID());
        resp->put_U32(stream->GetFpsScale());
        resp->put_U32(stream->GetFpsRate());
        resp->put_U32(stream->GetHeight());
        resp->put_U32(stream->GetWidth());
        resp->put_S64(stream->GetAspect() * 10000.0);
        break;

      case scSUBTITLE:
        resp->put_U32(stream->GetPID());
        resp->put_String(stream->GetLanguage());
        resp->put_U32(stream->CompositionPageId());
        resp->put_U32(stream->AncillaryPageId());
        break;

      default:
        break;
    }
  }

  DEBUGLOG("sendStreamInfo");
  m_Queue->Add(resp);
}

void cLiveStreamer::reorderStreams(int lang, eStreamType type)
{
  // do not reorder if there isn't any preferred language
  if (lang == -1 && type == stNONE)
    return;

  std::map<int, cTSDemuxer*> weight;

  // compute weights
  int i = 0;
  for (std::list<cTSDemuxer*>::iterator idx = m_Demuxers.begin(); idx != m_Demuxers.end(); idx++, i++)
  {
    cTSDemuxer* stream = (*idx);
    if (stream == NULL)
      continue;

    int w = i;

    // video streams rule
    if(stream->Content() == scVIDEO)
      w = 100000;

    // only for audio streams
    if(stream->Content() != scAUDIO)
    {
      weight[w] = stream;
      continue;
    }

    // weight of language (10000)
    int streamLangIndex = I18nLanguageIndex(stream->GetLanguage());
    w += (streamLangIndex == lang) ? 10000 : 0;

    // weight of streamtype (1000)
    w += (stream->Type() == type) ? 1000 : 0;

    // weight of languagedescriptor (100)
    int ldw = stream->GetAudioType() * 100;
    w += 400 - ldw;

    // summed weight
    weight[w] = stream;
  }

  // lock processing
  m_FilterMutex.Lock();

  // reorder streams on weight
  int idx = 0;
  m_Demuxers.clear();
  for(std::map<int, cTSDemuxer*>::reverse_iterator i = weight.rbegin(); i != weight.rend(); i++, idx++)
  {
    cTSDemuxer* stream = i->second;
    DEBUGLOG("Stream : Type %i / %s Weight: %i", stream->Type(), stream->GetLanguage(), i->first);
    m_Demuxers.push_back(stream);
  }

  // unlock processing
  m_FilterMutex.Unlock();
}

void cLiveStreamer::SetLanguage(int lang, eStreamType streamtype)
{
  if(lang == -1)
    return;

  m_LanguageIndex = lang;
  m_LangStreamType = streamtype;
}

bool cLiveStreamer::IsReady()
{
  if(m_ready)
    return true;

  bool bAllParsed = true;

  for (std::list<cTSDemuxer*>::iterator i = m_Demuxers.begin(); i != m_Demuxers.end(); i++)
  {
    if ((*i)->IsParsed())
    {
      if ((*i)->Content() == scVIDEO)
      {
        cChannelCache cache = cChannelCache::GetFromCache(m_uid);
        cChannelCache::iterator info = cache.find((*i)->GetPID());
        if(info != cache.end())
        {
          info->second.width = (*i)->GetWidth();
          info->second.height = (*i)->GetHeight();
          info->second.dar = (*i)->GetAspect();

          // update cache information
          cChannelCache::AddToCache(m_uid, cache);
        }
        m_ready = true;
        return true;
      }
    }
    else
      bAllParsed = false;
  }

  m_ready = bAllParsed;

  return bAllParsed;
}

void cLiveStreamer::Pause(bool on) {
  if(m_Queue == NULL)
    return;

  m_Queue->Pause(on);
}

void cLiveStreamer::RequestPacket()
{
  if(m_Queue == NULL)
    return;

  m_Queue->Request();
}

void cLiveStreamer::Receive(uchar *Data, int Length)
{
  int p = Put(Data, Length);

  if (p != Length)
    ReportOverflow(Length - p);
}
