/*
 *      vdr-plugin-xvdr - XBMC server plugin for VDR
 *
 *      Copyright (C) 2010 Alwin Esch (Team XBMC)
 *      Copyright (C) 2010, 2011 Alexander Pipelka
 *
 *      http://www.xbmc.org
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
#include "livereceiver.h"
#include "livequeue.h"
#include "channelcache.h"

cLiveStreamer::cLiveStreamer(uint32_t timeout)
 : cThread("cLiveStreamer stream processor")
 , cRingBufferLinear(MEGABYTE(5), TS_SIZE*2, true)
 , m_scanTimeout(timeout)
{
  m_Channel         = NULL;
  m_Priority        = 0;
  m_socket          = -1;
  m_Device          = NULL;
  m_Receiver        = NULL;
  m_Queue           = NULL;
  m_PatFilter       = NULL;
  m_Frontend        = -1;
  m_startup         = true;
  m_SignalLost      = false;
  m_LangStreamType  = stMPEG2AUDIO;
  m_LanguageIndex   = -1;
  m_uid             = 0;

  m_requestStreamChange = false;


  memset(&m_FrontendInfo, 0, sizeof(m_FrontendInfo));

  if(m_scanTimeout == 0)
    m_scanTimeout = XVDRServerConfig.stream_timeout;

  SetTimeouts(0, 50);
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
    if (m_Receiver)
    {
      DEBUGLOG("Detaching Live Receiver");
      m_Device->Detach(m_Receiver);
    }
    else
    {
      DEBUGLOG("No live receiver present");
    }

    if (m_PatFilter)
    {
      DEBUGLOG("Detaching Live Filter");
      m_Device->Detach(m_PatFilter);
    }
    else
    {
      DEBUGLOG("No live filter present");
    }

    if (m_Receiver)
    {
      DEBUGLOG("Deleting Live Receiver");
      DELETENULL(m_Receiver);
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
  if (m_Frontend >= 0)
  {
    close(m_Frontend);
    m_Frontend = -1;
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

    if (!m_Receiver->IsAttached())
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

    // no data
    if (buf == NULL || size <= TS_SIZE)
      continue;

    /* Make sure we are looking at a TS packet */
    while (size > TS_SIZE)
    {
      if (buf[0] == TS_SYNC_BYTE && buf[TS_SIZE] == TS_SYNC_BYTE)
        break;
      used++;
      buf++;
      size--;
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

bool cLiveStreamer::StreamChannel(const cChannel *channel, int priority, int sock, MsgPacket *resp)
{
  if (channel == NULL)
  {
    ERRORLOG("Starting streaming of channel without valid channel");
    resp->put_U32(XVDR_RET_ERROR);
    return false;
  }

  m_Channel  = channel;
  m_Priority = priority;
  m_socket   = sock;
  m_uid      = CreateChannelUID(m_Channel);

  // check if any device is able to decrypt the channel - code taken from VDR
  int NumUsableSlots = 0;

  if (m_Channel->Ca() >= CA_ENCRYPTED_MIN) {
    for (cCamSlot *CamSlot = CamSlots.First(); CamSlot; CamSlot = CamSlots.Next(CamSlot)) {
      if (CamSlot->ModuleStatus() == msReady) {
        if (CamSlot->ProvidesCa(m_Channel->Caids())) {
          if (!ChannelCamRelations.CamChecked(m_Channel->GetChannelID(), CamSlot->SlotNumber())) {
            NumUsableSlots++;
          }
       }
      }
    }
    if (!NumUsableSlots) {
      ERRORLOG("Unable to decrypt channel %i - %s", m_Channel->Number(), m_Channel->Name());
      resp->put_U32(XVDR_RET_ENCRYPTED);
      return false;
    }
  }

  // get device for this channel
  m_Device = cDevice::GetDevice(channel, m_Priority, true);

  // try a bit harder if we can't find a device
  if(m_Device == NULL)
    m_Device = cDevice::GetDevice(channel, m_Priority, false);

  INFOLOG("--------------------------------------");
  INFOLOG("Channel streaming request: %i - %s", m_Channel->Number(), m_Channel->Name());

  if (m_Device == NULL)
  {
    ERRORLOG("Can't get device for channel %i - %s", m_Channel->Number(), m_Channel->Name());

    // return status "recording running" if there is an active timer
    time_t now = time(NULL);
    if(Timers.GetMatch(now) != NULL)
      resp->put_U32(XVDR_RET_RECRUNNING);
    else
      resp->put_U32(XVDR_RET_DATALOCKED);

    return false;
  }

  INFOLOG("Found available device %d", m_Device->CardIndex() + 1);

  if (!m_Device->SwitchChannel(m_Channel, false))
  {
    ERRORLOG("Can't switch to channel %i - %s", m_Channel->Number(), m_Channel->Name());
    resp->put_U32(XVDR_RET_ERROR);
    return false;
  }

  // Send the OK response here, that it is before the Stream end message
  resp->put_U32(XVDR_RET_OK);
  resp->write(sock, 3000);

  // create send queue
  if (m_Queue == NULL)
  {
    m_Queue = new cLiveQueue(m_socket);
    m_Queue->Start();
  }

  m_PatFilter = new cLivePatFilter(this, m_Channel);
  m_Receiver = new cLiveReceiver(this, m_Channel, m_Priority);

  // get cached demuxer data
  DEBUGLOG("Creating demuxers");
  cChannelCache cache = cChannelCache::GetFromCache(m_uid);
  if(cache.size() != 0) {
    cache.CreateDemuxers(this);
    RequestStreamChange();
  }

  DEBUGLOG("Starting PAT scanner");
  m_Device->AttachFilter(m_PatFilter);
  m_Device->AttachReceiver(m_Receiver);

  INFOLOG("Successfully switched to channel %i - %s", m_Channel->Number(), m_Channel->Name());
  return true;
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
    if (m_Receiver)
    {
      m_Device->Detach(m_Receiver);
      m_Device->AttachReceiver(m_Receiver);
    }
  }
}

void cLiveStreamer::Detach(void)
{
  DEBUGLOG("%s", __FUNCTION__);
  if (m_Device)
  {
    if (m_Receiver)
      m_Device->Detach(m_Receiver);
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
        resp->put_String("LATM");
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
  /* If no frontend is found m_Frontend is set to -2, in this case
     return a empty signalinfo package */
  if (m_Frontend == -2)
  {
    MsgPacket* resp = new MsgPacket(XVDR_STREAM_SIGNALINFO, XVDR_CHANNEL_STREAM);

    resp->put_String(*cString::sprintf("Unknown"));
    resp->put_String(*cString::sprintf("Unknown"));
    resp->put_U32(0);
    resp->put_U32(0);
    resp->put_U32(0);
    resp->put_U32(0);

    m_Queue->Add(resp);
    return;
  }

  if (m_Channel && ((m_Channel->Source() >> 24) == 'V'))
  {
    if (m_Frontend < 0)
    {
      for (int i = 0; i < 8; i++)
      {
        m_DeviceString = cString::sprintf("/dev/video%d", i);
        m_Frontend = open(m_DeviceString, O_RDONLY | O_NONBLOCK);
        if (m_Frontend >= 0)
        {
          if (ioctl(m_Frontend, VIDIOC_QUERYCAP, &m_vcap) < 0)
          {
            ERRORLOG("cannot read analog frontend info.");
            close(m_Frontend);
            m_Frontend = -1;
            memset(&m_vcap, 0, sizeof(m_vcap));
            continue;
          }
          break;
        }
      }
      if (m_Frontend < 0)
        m_Frontend = -2;
    }

    if (m_Frontend >= 0)
    {
      MsgPacket* resp = new MsgPacket(XVDR_STREAM_SIGNALINFO, XVDR_CHANNEL_STREAM);

      resp->put_String(*cString::sprintf("Analog #%s - %s (%s)", *m_DeviceString, (char *) m_vcap.card, m_vcap.driver));
      resp->put_String("");
      resp->put_U32(0);
      resp->put_U32(0);
      resp->put_U32(0);
      resp->put_U32(0);

      m_Queue->Add(resp);
    }
  }
  else
  {
    if (m_Frontend < 0)
    {
      m_DeviceString = cString::sprintf(FRONTEND_DEVICE, m_Device->CardIndex(), 0);
      m_Frontend = open(m_DeviceString, O_RDONLY | O_NONBLOCK);
      if (m_Frontend >= 0)
      {
        if (ioctl(m_Frontend, FE_GET_INFO, &m_FrontendInfo) < 0)
        {
          ERRORLOG("cannot read frontend info.");
          close(m_Frontend);
          m_Frontend = -2;
          memset(&m_FrontendInfo, 0, sizeof(m_FrontendInfo));
          return;
        }
      }
    }

    if (m_Frontend >= 0)
    {
      MsgPacket* resp = new MsgPacket(XVDR_STREAM_SIGNALINFO, XVDR_CHANNEL_STREAM);

      fe_status_t status;
      uint16_t fe_snr;
      uint16_t fe_signal;
      uint32_t fe_ber;
      uint32_t fe_unc;

      memset(&status, 0, sizeof(status));
      ioctl(m_Frontend, FE_READ_STATUS, &status);

      if (ioctl(m_Frontend, FE_READ_SIGNAL_STRENGTH, &fe_signal) == -1)
        fe_signal = -2;
      if (ioctl(m_Frontend, FE_READ_SNR, &fe_snr) == -1)
        fe_snr = -2;
      if (ioctl(m_Frontend, FE_READ_BER, &fe_ber) == -1)
        fe_ber = -2;
      if (ioctl(m_Frontend, FE_READ_UNCORRECTED_BLOCKS, &fe_unc) == -1)
        fe_unc = -2;

      switch (m_Channel->Source() & cSource::st_Mask)
      {
        case cSource::stSat:
          resp->put_String(*cString::sprintf("DVB-S%s #%d - %s", (m_FrontendInfo.caps & 0x10000000) ? "2" : "",  cDevice::ActualDevice()->CardIndex(), m_FrontendInfo.name));
          break;
        case cSource::stCable:
          resp->put_String(*cString::sprintf("DVB-C #%d - %s", cDevice::ActualDevice()->CardIndex(), m_FrontendInfo.name));
          break;
        case cSource::stTerr:
          resp->put_String(*cString::sprintf("DVB-T #%d - %s", cDevice::ActualDevice()->CardIndex(), m_FrontendInfo.name));
          break;
      }
      resp->put_String(*cString::sprintf("%s:%s:%s:%s:%s", (status & FE_HAS_LOCK) ? "LOCKED" : "-", (status & FE_HAS_SIGNAL) ? "SIGNAL" : "-", (status & FE_HAS_CARRIER) ? "CARRIER" : "-", (status & FE_HAS_VITERBI) ? "VITERBI" : "-", (status & FE_HAS_SYNC) ? "SYNC" : "-"));
      resp->put_U32(fe_snr);
      resp->put_U32(fe_signal);
      resp->put_U32(fe_ber);
      resp->put_U32(fe_unc);

      DEBUGLOG("sendSignalInfo");

      m_Queue->Add(resp);
    }
  }
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
        return true;
      }
    }
    else
      bAllParsed = false;
  }

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
