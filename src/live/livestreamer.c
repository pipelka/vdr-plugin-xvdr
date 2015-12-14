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
#include "xvdr/xvdrclient.h"
#include "tools/hash.h"

#include "livestreamer.h"
#include "livequeue.h"
#include "channelcache.h"

cLiveStreamer::cLiveStreamer(cXVDRClient* parent, const cChannel *channel, int priority, bool rawPTS)
 : cThread("cLiveStreamer stream processor")
 , cRingBufferLinear(MEGABYTE(10), TS_SIZE * 2, true)
 , cReceiver(NULL, priority)
 , m_scanTimeout(10)
 , m_parent(parent)
{
  m_Device          = NULL;
  m_Queue           = NULL;
  m_startup         = true;
  m_SignalLost      = false;
  m_LangStreamType  = cStreamInfo::stMPEG2AUDIO;
  m_LanguageIndex   = -1;
  m_uid             = CreateChannelUID(channel);
  m_protocolVersion = XVDR_PROTOCOLVERSION;
  m_waitforiframe   = false;
  m_rawPTS          = rawPTS;

  m_requestStreamChange = false;

  if(m_scanTimeout == 0)
    m_scanTimeout = XVDRServerConfig.stream_timeout;

  // create send queue
  m_Queue = new cLiveQueue(m_parent->GetSocket());
  m_Queue->Start();

  SetTimeouts(0, 10);
  Start();
}

cLiveStreamer::~cLiveStreamer()
{
  DEBUGLOG("Started to delete live streamer");

  cTimeMs t;

  DEBUGLOG("Stopping streamer thread ...");
  Cancel(5);
  DEBUGLOG("Done.");

  DEBUGLOG("Detaching");

  if (IsAttached()) {
    Detach();
  }

  m_Demuxers.clear();

  delete m_Queue;

  m_uid = 0;

  {
    cMutexLock lock(&m_DeviceMutex);
    m_Device = NULL;
  }

  DEBUGLOG("Finished to delete live streamer (took %llu ms)", t.Elapsed());
}

void cLiveStreamer::SetTimeout(uint32_t timeout) {
  m_scanTimeout = timeout;
}

void cLiveStreamer::SetProtocolVersion(uint32_t protocolVersion) {
  m_protocolVersion = protocolVersion;
}

void cLiveStreamer::SetWaitForIFrame(bool waitforiframe) {
  m_waitforiframe = waitforiframe;
}

void cLiveStreamer::RequestStreamChange()
{
  m_requestStreamChange = true;
}

void cLiveStreamer::TryChannelSwitch() {
  // find channel from uid
  const cChannel* channel = FindChannelByUID(m_uid);

  // try to switch channel
  int rc = SwitchChannel(channel);

  // succeeded -> exit
  if(rc == XVDR_RET_OK) {
    return;
  }

  // time limit not exceeded -> relax & exit
  if(m_last_tick.Elapsed() < (uint64_t)(m_scanTimeout*1000)) {
    cCondWait::SleepMs(10);
    return;
  }

  // push notification after timeout
  switch(rc) {
    case XVDR_RET_ENCRYPTED:
      ERRORLOG("Unable to decrypt channel %i - %s", channel->Number(), channel->Name());
      m_parent->StatusMessage(tr("Unable to decrypt channel"));
      break;
    case XVDR_RET_DATALOCKED:
      ERRORLOG("Can't get device for channel %i - %s", channel->Number(), channel->Name());
      m_parent->StatusMessage(tr("All tuners busy"));
      break;
    case XVDR_RET_RECRUNNING:
      ERRORLOG("Active recording blocking channel %i - %s", channel->Number(), channel->Name());
      m_parent->StatusMessage(tr("Blocked by active recording"));
      break;
    case XVDR_RET_ERROR:
      ERRORLOG("Error switching to channel %i - %s", channel->Number(), channel->Name());
      m_parent->StatusMessage(tr("Failed to switch"));
      break;
  }

  m_last_tick.Set(0);
}

void cLiveStreamer::Action(void)
{
  int size = 0;
  unsigned char *buf = NULL;
  m_startup = true;

  // reset timer
  m_last_tick.Set(0);

  INFOLOG("streamer thread started.");

  while (Running())
  {
    size = 0;
    buf = Get(size);

    // try to switch channel if we aren't attached yet
    {
      if (!IsAttached()) {
        TryChannelSwitch();
      }
    }

    if(!IsStarting() && (m_last_tick.Elapsed() > (uint64_t)(m_scanTimeout*1000)) && !m_SignalLost)
    {
      INFOLOG("timeout. signal lost!");
      sendStatus(XVDR_STREAM_STATUS_SIGNALLOST);
      m_SignalLost = true;

      if(IsAttached()) {
        Detach();
      }
    }

    // no data
    if (buf == NULL || size <= TS_SIZE)
      continue;

    // Sync to TS packet
    int used = 0;
    while (size > TS_SIZE)
    {
      if (buf[0] == TS_SYNC_BYTE && buf[TS_SIZE] == TS_SYNC_BYTE)
        break;
      used++;
      buf++;
      size--;
    }
    Del(used);

    while (size >= TS_SIZE)
    {
      if(!Running())
        break;

      // TS packet sync not found !
      if (buf[0] != TS_SYNC_BYTE)
        break;

      unsigned int ts_pid = TsPid(buf);

      {
        cMutexLock lock(&m_FilterMutex);
        cTSDemuxer *demuxer = m_Demuxers.findDemuxer(ts_pid);

        if (demuxer)
          demuxer->ProcessTSPacket(buf);
      }

      buf += TS_SIZE;
      size -= TS_SIZE;
      Del(TS_SIZE);
    }
  }

  INFOLOG("streamer thread ended.");
}

int cLiveStreamer::SwitchChannel(const cChannel *channel)
{
  if (channel == NULL) {
    return XVDR_RET_ERROR;
  }

  if(IsAttached()) {
    Detach();
  }

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
      return XVDR_RET_ENCRYPTED;
    }
  }

  // get device for this channel
  {
    cMutexLock lock(&m_DeviceMutex);
    m_Device = cDevice::GetDevice(channel, LIVEPRIORITY, false);
  }

  if (m_Device == NULL)
  {
    // return status "recording running" if there is an active timer
    time_t now = time(NULL);

    for (cTimer *ti = Timers.First(); ti; ti = Timers.Next(ti)) {
      if (ti->Recording() && ti->Matches(now)) {
        return XVDR_RET_RECRUNNING;
      }
    }

    return XVDR_RET_DATALOCKED;
  }

  INFOLOG("Found available device %d", m_Device->DeviceNumber() + 1);

  if (!m_Device->SwitchChannel(channel, false))
  {
    ERRORLOG("Can't switch to channel %i - %s", channel->Number(), channel->Name());
    return XVDR_RET_ERROR;
  }

  // get cached demuxer data
  cStreamBundle bundle = cChannelCache::GetFromCache(m_uid);

  // channel already in cache
  if(bundle.size() != 0) {
    INFOLOG("Channel information found in cache");
  }
  // channel not found in cache -> add it from vdr
  else {
    INFOLOG("adding channel to cache");
    cChannelCache::AddToCache(channel);
    bundle = cChannelCache::GetFromCache(m_uid);
  }

  // recheck cache item
  cStreamBundle currentitem = cStreamBundle::FromChannel(channel);
  if(!currentitem.ismetaof(bundle)) {
    INFOLOG("current channel differs from cache item - updating");
    bundle = currentitem;
    cChannelCache::AddToCache(m_uid, bundle);
  }

  if(bundle.size() != 0) {
    INFOLOG("Creating demuxers");
    CreateDemuxers(&bundle);
  }

  RequestStreamChange();

  INFOLOG("Successfully switched to channel %i - %s", channel->Number(), channel->Name());

  if(m_waitforiframe) {
    INFOLOG("Will wait for first I-Frame ...");
  }

  // clear cached data
  Clear();
  m_Queue->Cleanup();

  m_uid = CreateChannelUID(channel);

  if(!Attach()) {
    INFOLOG("Unable to attach receiver !");
    return XVDR_RET_DATALOCKED;
  }

  INFOLOG("done switching.");
  return XVDR_RET_OK;
}

bool cLiveStreamer::Attach(void)
{
  cMutexLock lock(&m_DeviceMutex);
  
  if (m_Device == NULL) {
    return false;
  }

  return m_Device->AttachReceiver(this);
}

void cLiveStreamer::Detach(void)
{
  cMutexLock lock(&m_DeviceMutex);

  if (m_Device) {
    m_Device->Detach(this);
  }
}

void cLiveStreamer::sendStreamPacket(sStreamPacket *pkt)
{
  bool bReady = m_Demuxers.isReady();

  if(!bReady || pkt == NULL || pkt->size == 0)
    return;

  // Send stream information as the first packet on startup
  if (IsStarting() && bReady)
  {
    // wait for AV frames (we start with an audio or video packet)
    if(!(pkt->content == cStreamInfo::scAUDIO || pkt->content == cStreamInfo::scVIDEO)) {
      return;
    }

    INFOLOG("streaming of channel started");
    m_last_tick.Set(0);
    m_requestStreamChange = true;
    m_startup = false;
  }

  // send stream change on demand
  if(m_requestStreamChange)
    sendStreamChange();

  // wait for first I-Frame (if enabled)
  if(m_waitforiframe && pkt->frametype != cStreamInfo::ftIFRAME) {
    return;
  }

  m_waitforiframe = false;

  // if a audio or video packet was sent, the signal is restored
  if(m_SignalLost && (pkt->content == cStreamInfo::scVIDEO || pkt->content == cStreamInfo::scAUDIO)) {
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

  if(m_rawPTS) {
    packet->put_S64(pkt->rawpts);
    packet->put_S64(pkt->rawdts);
  }
  else {
    packet->put_S64(pkt->pts);
    packet->put_S64(pkt->dts);
  }

  if(m_protocolVersion >= 5) {
    packet->put_U32(pkt->duration);
  }

  // write frame type into unused header field clientid
  packet->setClientID((uint16_t)pkt->frametype);

  // write payload into stream packet
  packet->put_U32(pkt->size);
  packet->put_Blob(pkt->data, pkt->size);

  m_Queue->Add(packet, pkt->content);
  m_last_tick.Set(0);
}

void cLiveStreamer::sendDetach() {
  INFOLOG("sending detach message");
  MsgPacket* resp = new MsgPacket(XVDR_STREAM_DETACH, XVDR_CHANNEL_STREAM);
  m_parent->QueueMessage(resp);
}

void cLiveStreamer::sendStreamChange()
{
  MsgPacket* resp = new MsgPacket(XVDR_STREAM_CHANGE, XVDR_CHANNEL_STREAM);

  DEBUGLOG("sendStreamChange");

  cStreamBundle cache;
  INFOLOG("Stored channel information in cache:");
  for (auto i = m_Demuxers.begin(); i != m_Demuxers.end(); i++) {
    cache.AddStream(*(*i));
    (*i)->info();
  }
  cChannelCache::AddToCache(m_uid, cache);

  m_FilterMutex.Lock();

  // reorder streams as preferred
  m_Demuxers.reorderStreams(m_LanguageIndex, m_LangStreamType);

  for (auto idx = m_Demuxers.begin(); idx != m_Demuxers.end(); idx++)
  {
    cTSDemuxer* stream = (*idx);

    if (stream == NULL)
      continue;

    int streamid = stream->GetPID();
    resp->put_U32(streamid);

    switch(stream->GetContent())
    {
      case cStreamInfo::scAUDIO:
        resp->put_String(stream->TypeName());
        resp->put_String(stream->GetLanguage());
        if(m_protocolVersion >= 5) {
          resp->put_U32(stream->GetChannels());
          resp->put_U32(stream->GetSampleRate());
          resp->put_U32(stream->GetBlockAlign());
          resp->put_U32(stream->GetBitRate());
          resp->put_U32(stream->GetBitsPerSample());
        }
        break;

      case cStreamInfo::scVIDEO:
        // H265 is supported on protocol version 6 or higher, ...
        resp->put_String(stream->TypeName());
        resp->put_U32(stream->GetFpsScale());
        resp->put_U32(stream->GetFpsRate());
        resp->put_U32(stream->GetHeight());
        resp->put_U32(stream->GetWidth());
        resp->put_S64(stream->GetAspect() * 10000.0);

        // send decoder specific data SPS / PPS / VPS ... (Protocol Version 6)
        if(m_protocolVersion >= 6) {
          int length = 0;

          // put SPS
          uint8_t* sps = stream->GetVideoDecoderSPS(length);
          resp->put_U8(length);
          if(sps != NULL) {
            resp->put_Blob(sps, length);
          }

          // put PPS
          uint8_t* pps = stream->GetVideoDecoderPPS(length);
          resp->put_U8(length);
          if(pps != NULL) {
            resp->put_Blob(pps, length);
          }

          // put VPS
          uint8_t* vps = stream->GetVideoDecoderVPS(length);
          resp->put_U8(length);
          if(pps != NULL) {
            resp->put_Blob(vps, length);
          }
        }
        break;

      case cStreamInfo::scSUBTITLE:
        resp->put_String(stream->TypeName());
        resp->put_String(stream->GetLanguage());
        resp->put_U32(stream->CompositionPageId());
        resp->put_U32(stream->AncillaryPageId());
        break;

      case cStreamInfo::scTELETEXT:
        resp->put_String(stream->TypeName());
        break;

      default:
        break;
    }
  }

  m_FilterMutex.Unlock();

  m_Queue->Add(resp, cStreamInfo::scSTREAMINFO);
  m_requestStreamChange = false;
}

void cLiveStreamer::sendStatus(int status)
{
  MsgPacket* packet = new MsgPacket(XVDR_STREAM_STATUS, XVDR_CHANNEL_STREAM);
  packet->put_U32(status);
  m_parent->QueueMessage(packet);
}

void cLiveStreamer::RequestSignalInfo()
{
  cMutexLock lock(&m_DeviceMutex);

  if(!Running() || m_Device == NULL) {
    return;
  }

  // do not send (and pollute the client with) signal information
  // if we are paused
  if(IsPaused())
    return;

  MsgPacket* resp = new MsgPacket(XVDR_STREAM_SIGNALINFO, XVDR_CHANNEL_STREAM);

  int DeviceNumber = m_Device->DeviceNumber() + 1;
  int Strength = 0;
  int Quality = 0;

  if(!TimeShiftMode()) {
    Strength = m_Device->SignalStrength();
    Quality = m_Device->SignalQuality();
  }

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

  if(TimeShiftMode())
  {
    resp->put_String("TIMESHIFT");
  }
  else if(Quality == -1)
  {
    resp->put_String("UNKNOWN (Incompatible device)");
    Quality = 0;
  }
  else
    resp->put_String(*cString::sprintf("%s:%s:%s:%s:%s", 
			(Quality > 4) ? "LOCKED" : "-",
			(Quality > 0) ? "SIGNAL" : "-",
			(Quality > 1) ? "CARRIER" : "-",
			(Quality > 2) ? "VITERBI" : "-",
			(Quality > 3) ? "SYNC" : "-"));

  resp->put_U32((Strength << 16 ) / 100);
  resp->put_U32((Quality << 16 ) / 100);
  resp->put_U32(0);
  resp->put_U32(0);

  // get provider & service information
  const cChannel* channel = FindChannelByUID(m_uid);
  if(channel != NULL) {
    // put in provider name
    resp->put_String(channel->Provider());

    // what the heck should be the service name ?
    // using PortalName for now
    resp->put_String(channel->PortalName());
  }
  else {
    resp->put_String("");
    resp->put_String("");
  }

  DEBUGLOG("RequestSignalInfo");
  m_Queue->Add(resp, cStreamInfo::scNONE);
}

void cLiveStreamer::SetLanguage(int lang, cStreamInfo::Type streamtype)
{
  if(lang == -1)
    return;

  m_LanguageIndex = lang;
  m_LangStreamType = streamtype;
}

bool cLiveStreamer::IsPaused()
{
  if(m_Queue == NULL)
    return false;

  return m_Queue->IsPaused();
}

bool cLiveStreamer::TimeShiftMode()
{
  if(m_Queue == NULL)
    return false;

  return m_Queue->TimeShiftMode();
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

#if VDRVERSNUM < 20300
void cLiveStreamer::Receive(uchar *Data, int Length)
#else
void cLiveStreamer::Receive(const uchar *Data, int Length)
#endif
{
  int p = Put(Data, Length);

  if (p != Length)
    ReportOverflow(Length - p);
}

void cLiveStreamer::ChannelChange(const cChannel* channel) {
  cMutexLock lock(&m_FilterMutex);

  if(CreateChannelUID(channel) != m_uid || !Running()) {
    return;
  }

  INFOLOG("ChannelChange()");
  SwitchChannel(channel);
}

void cLiveStreamer::CreateDemuxers(cStreamBundle* bundle) {
  // update demuxers
  m_Demuxers.updateFrom(bundle, this);

  // update pids
  SetPids(NULL);
  for (auto i = m_Demuxers.begin(); i != m_Demuxers.end(); i++) {
    cTSDemuxer* dmx = *i;
    dmx->info();
    AddPid(dmx->GetPID());
  }
}
