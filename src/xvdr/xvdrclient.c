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
#include <stdio.h>
#include <sys/socket.h>
#include <set>
#include <map>
#include <string>

#include <vdr/recording.h>
#include <vdr/channels.h>
#include <vdr/i18n.h>
#include <vdr/videodir.h>
#include <vdr/plugin.h>
#include <vdr/timers.h>
#include <vdr/menu.h>
#include <vdr/device.h>
#include <vdr/sources.h>

#include "config/config.h"
#include "live/livestreamer.h"
#include "net/msgpacket.h"
#include "net/socketlock.h"
#include "recordings/recordingscache.h"
#include "recordings/recplayer.h"
#include "tools/hash.h"
#include "xvdr/xvdrchannels.h"

#include "xvdrcommand.h"
#include "xvdrclient.h"
#include "xvdrserver.h"


static bool IsRadio(const cChannel* channel)
{
  bool isRadio = false;

  // assume channels without VPID & APID are video channels
  if (channel->Vpid() == 0 && channel->Apid(0) == 0)
    isRadio = false;
  // channels without VPID are radio channels (channels with VPID 1 are encrypted radio channels)
  else if (channel->Vpid() == 0 || channel->Vpid() == 1)
    isRadio = true;

  return isRadio;
}

static uint32_t recid2uid(const char* recid)
{
  uint32_t uid = 0;
  sscanf(recid, "%8x", &uid);
  DEBUGLOG("lookup recid: %s (uid: %u)", recid, uid);
  return uid;
}

cString cXVDRClient::CreateServiceReference(cChannel* channel)
{
  int hash = 0;

  if(cSource::IsSat(channel->Source()))
  {
    hash = channel->Source() & cSource::st_Pos;
    if(hash > 0x00007FFF)
      hash |= 0xFFFF0000;

    if(hash < 0)
      hash = -hash;
    else
      hash = 1800 + hash;

    hash = hash << 16;
  }
  else if(cSource::IsCable(channel->Source()))
    hash = 0xFFFF0000;
  else if(cSource::IsTerr(channel->Source()))
    hash = 0xEEEE0000;
  else if(cSource::IsAtsc(channel->Source()))
    ; // how should we handle ATSC ?

  cString serviceref = cString::sprintf("1_0_%i_%X_%X_%X_%X_0_0_0",
                                  (channel->Vpid() == 0) ? 2 : (channel->Vtype() == 27) ? 19 : 1,
                                  channel->Sid(),
                                  channel->Tid(),
                                  channel->Nid(),
                                  hash);

  return serviceref;
}

cString cXVDRClient::CreateLogoURL(cChannel* channel)
{
  if((const char*)XVDRServerConfig.PiconsURL == NULL || strlen((const char*)XVDRServerConfig.PiconsURL) == 0)
    return "";

  cString url = AddDirectory(XVDRServerConfig.PiconsURL, (const char*)CreateServiceReference(channel));
  return cString::sprintf("%s.png", (const char*)url);
}

void cXVDRClient::PutTimer(cTimer* timer, MsgPacket* p)
{
  XVDRChannels.Lock(false);

  // check for conflicts
  DEBUGLOG("Checking conflicts for: %s", (const char*)timer->ToText(true));

  // order active timers by starttime
  std::map<time_t, cTimer*> timeline;
  int numTimers = Timers.Count();
  for (int i = 0; i < numTimers; i++)
  {
    cTimer* t = Timers.Get(i);

    // same timer -> skip
    if (!t || timer->Index() == i)
      continue;

    // timer not active -> skip
    if(!(t->Flags() & tfActive))
      continue;

    // this one is earlier -> no match
    if(t->StopTime() <= timer->StartTime())
      continue;

    // this one is later -> no match
    if(t->StartTime() >= timer->StopTime())
      continue;

    timeline[t->StartTime()] = t;
  }

  std::set<int> transponders;
  transponders.insert(timer->Channel()->Transponder()); // we also count ourself
  cTimer* to_check = timer;

  std::map<time_t, cTimer*>::iterator i;
  for (i = timeline.begin(); i != timeline.end(); i++)
  {
    cTimer* t = i->second;

    // this one is earlier -> no match
    if(t->StopTime() <= to_check->StartTime())
      continue;

    // this one is later -> no match
    if(t->StartTime() >= to_check->StopTime())
      continue;

    // same transponder -> no conflict
    if(t->Channel()->Transponder() == to_check->Channel()->Transponder())
      continue;

    // different source -> no conflict
    if(t->Channel()->Source() != to_check->Channel()->Source())
      continue;

    DEBUGLOG("Possible conflict: %s", (const char*)t->ToText(true));
    transponders.insert(t->Channel()->Transponder());

    // now check conflicting timer
    to_check = t;
  }

  uint32_t number_of_devices_for_this_channel = 0;
  for(int i = 0; i < cDevice::NumDevices(); i++)
  {
    cDevice* device = cDevice::GetDevice(i);
    if(device != NULL && device->ProvidesTransponder(timer->Channel()))
      number_of_devices_for_this_channel++;
  }

  int cflags = 0;
  if(transponders.size() > number_of_devices_for_this_channel)
  {
    DEBUGLOG("ERROR - Not enough devices");
    cflags += 2048;
  }
  else if(transponders.size() > 1)
  {
    DEBUGLOG("Overlapping timers - Will record");
    cflags += 1024;
  }
  else
    DEBUGLOG("No conflicts");

  XVDRChannels.Unlock();

  p->put_U32(timer->Index()+1);
  p->put_U32(timer->Flags() | cflags);
  p->put_U32(timer->Priority());
  p->put_U32(timer->Lifetime());
  p->put_U32(CreateChannelUID(timer->Channel()));
  p->put_U32(timer->StartTime());
  p->put_U32(timer->StopTime());
  p->put_U32(timer->Day());
  p->put_U32(timer->WeekDays());
  p->put_String(m_toUTF8.Convert(timer->File()));
}

cMutex cXVDRClient::m_timerLock;
cMutex cXVDRClient::m_switchLock;

cXVDRClient::cXVDRClient(int fd, unsigned int id)
{
  m_Id                      = id;
  m_loggedIn                = false;
  m_Streamer                = NULL;
  m_isStreaming             = false;
  m_StatusInterfaceEnabled  = false;
  m_RecPlayer               = NULL;
  m_req                     = NULL;
  m_resp                    = NULL;
  m_compressionLevel        = 0;
  m_LanguageIndex           = -1;
  m_LangStreamType          = cStreamInfo::stMPEG2AUDIO;
  m_channelCount            = 0;
  m_timeout                 = 3000;

  m_socket = fd;
  m_wantfta = true;
  m_filterlanguage = false;

  Start();
}

cXVDRClient::~cXVDRClient()
{
  DEBUGLOG("%s", __FUNCTION__);
  StopChannelStreaming();

  // shutdown connection
  shutdown(m_socket, SHUT_RDWR); 
  Cancel(10);

  // remove socket lock
  cSocketLock::erase(m_socket);

  // close connection
  close(m_socket);
  DEBUGLOG("done");
}

void cXVDRClient::Action(void)
{
  bool bClosed(false);

  SetPriority(10);

  while (Running()) {
    m_req = MsgPacket::read(m_socket, bClosed, 1000);

    if(bClosed) {
      delete m_req;
      m_req = NULL;
      break;
    }

    if(m_req != NULL) {
      processRequest();
      delete m_req;
    }
    else if(m_scanner.IsScanning()) {
      SendScannerStatus();
    }
  }

  /* If thread is ended due to closed connection delete a
     possible running stream here */
  StopChannelStreaming();
}

int cXVDRClient::StartChannelStreaming(const cChannel *channel, uint32_t timeout, int32_t priority)
{
  cMutexLock lock(&m_switchLock);
  m_Streamer = new cLiveStreamer(priority, timeout, m_protocolVersion);
  m_Streamer->SetLanguage(m_LanguageIndex, m_LangStreamType);

  return m_Streamer->StreamChannel(channel, m_socket);
}

void cXVDRClient::StopChannelStreaming()
{
  cMutexLock lock(&m_switchLock);
  delete m_Streamer;
  m_Streamer = NULL;
  m_isStreaming = false;
}

void cXVDRClient::TimerChange(const cTimer *Timer, eTimerChange Change)
{
  if(Change != tcAdd && Change != tcDel)
    return;

  TimerChange();
}

void cXVDRClient::TimerChange()
{
  cMutexLock lock(&m_msgLock);

  if (m_StatusInterfaceEnabled)
  {
    INFOLOG("Sending timer change request to client #%i ...", m_Id);
    cSocketLock locks(m_socket);
    MsgPacket* resp = new MsgPacket(XVDR_STATUS_TIMERCHANGE, XVDR_CHANNEL_STATUS);
    resp->write(m_socket, m_timeout);
    delete resp;
  }
}

void cXVDRClient::ChannelChange()
{
  cMutexLock lock(&m_msgLock);

  if(!m_StatusInterfaceEnabled)
    return;

  int count = ChannelsCount();
  if (m_channelCount == count)
  {
    INFOLOG("Client %i: %i channels, no change", m_Id, count);
    return;
  }

  if (m_channelCount == 0)
    INFOLOG("Client %i: no channels - sending request", m_Id);
  else
    INFOLOG("Client %i : %i channels, %i available - sending request", m_Id, m_channelCount, count);

  cSocketLock locks(m_socket);
  MsgPacket* resp = new MsgPacket(XVDR_STATUS_CHANNELCHANGE, XVDR_CHANNEL_STATUS);
  resp->write(m_socket, m_timeout);
  delete resp;
}

void cXVDRClient::RecordingsChange()
{
  cMutexLock lock(&m_msgLock);

  if (!m_StatusInterfaceEnabled)
    return;

  cSocketLock locks(m_socket);
  MsgPacket* resp = new MsgPacket(XVDR_STATUS_RECORDINGSCHANGE, XVDR_CHANNEL_STATUS);
  resp->write(m_socket, m_timeout);
  delete resp;
}

void cXVDRClient::Recording(const cDevice *Device, const char *Name, const char *FileName, bool On)
{
  cMutexLock lock(&m_msgLock);

  if (m_StatusInterfaceEnabled)
  {
    cSocketLock locks(m_socket);
    MsgPacket* resp = new MsgPacket(XVDR_STATUS_RECORDING, XVDR_CHANNEL_STATUS);

    resp->put_U32(Device->CardIndex());
    resp->put_U32(On);
    if (Name)
      resp->put_String(Name);
    else
      resp->put_String("");

    if (FileName)
      resp->put_String(FileName);
    else
      resp->put_String("");

    resp->write(m_socket, m_timeout);
    delete resp;
  }
}

void cXVDRClient::OsdStatusMessage(const char *Message)
{
  cMutexLock lock(&m_msgLock);

  if (m_StatusInterfaceEnabled && Message)
  {
    /* Ignore this messages */
    if (strcasecmp(Message, trVDR("Channel not available!")) == 0) return;
    else if (strcasecmp(Message, trVDR("Delete timer?")) == 0) return;
    else if (strcasecmp(Message, trVDR("Delete recording?")) == 0) return;
    else if (strcasecmp(Message, trVDR("Press any key to cancel shutdown")) == 0) return;
    else if (strcasecmp(Message, trVDR("Press any key to cancel restart")) == 0) return;
    else if (strcasecmp(Message, trVDR("Editing - shut down anyway?")) == 0) return;
    else if (strcasecmp(Message, trVDR("Recording - shut down anyway?")) == 0) return;
    else if (strcasecmp(Message, trVDR("shut down anyway?")) == 0) return;
    else if (strcasecmp(Message, trVDR("Recording - restart anyway?")) == 0) return;
    else if (strcasecmp(Message, trVDR("Editing - restart anyway?")) == 0) return;
    else if (strcasecmp(Message, trVDR("Delete channel?")) == 0) return;
    else if (strcasecmp(Message, trVDR("Timer still recording - really delete?")) == 0) return;
    else if (strcasecmp(Message, trVDR("Delete marks information?")) == 0) return;
    else if (strcasecmp(Message, trVDR("Delete resume information?")) == 0) return;
    else if (strcasecmp(Message, trVDR("CAM is in use - really reset?")) == 0) return;
    else if (strcasecmp(Message, trVDR("Really restart?")) == 0) return;
    else if (strcasecmp(Message, trVDR("Stop recording?")) == 0) return;
    else if (strcasecmp(Message, trVDR("Cancel editing?")) == 0) return;
    else if (strcasecmp(Message, trVDR("Cutter already running - Add to cutting queue?")) == 0) return;
    else if (strcasecmp(Message, trVDR("No index-file found. Creating may take minutes. Create one?")) == 0) return;

    cSocketLock locks(m_socket);
    MsgPacket* resp = new MsgPacket(XVDR_STATUS_MESSAGE, XVDR_CHANNEL_STATUS);

    resp->put_U32(0);
    resp->put_String(Message);

    resp->write(m_socket, m_timeout);
    delete resp;
  }
}

bool cXVDRClient::IsChannelWanted(cChannel* channel, bool radio)
{
  // dismiss invalid channels
  if(channel == NULL)
    return false;

  // right type ?
  if (radio != IsRadio(channel))
    return false;

  // skip channels witout SID
  if (channel->Sid() == 0)
    return false;

  if (strcmp(channel->Name(), ".") == 0)
    return false;

  // check language
  if(m_filterlanguage && m_LanguageIndex != -1)
  {
    bool bLanguageFound = false;
    const char* lang = NULL;

    // check MP2 languages
    for(int i = 0; i < MAXAPIDS; i++) {
      lang = channel->Alang(i);

      if(lang == NULL)
        break;

      if(m_LanguageIndex == I18nLanguageIndex(lang))
      {
        bLanguageFound = true;
        break;
      }
    }

    // check other digital languages
    for(int i = 0; i < MAXDPIDS; i++) {
      lang = channel->Dlang(i);

      if(lang == NULL)
        break;

      if(m_LanguageIndex == I18nLanguageIndex(lang))
      {
        bLanguageFound = true;
        break;
      }
    }

    if(!bLanguageFound)
      return false;
  }

  // user selection for FTA channels
  if(channel->Ca(0) == 0)
    return m_wantfta;

  // we want all encrypted channels if there isn't any CaID filter
  if(m_caids.size() == 0)
    return true;

  // check if we have a matching CaID
  for(std::list<int>::iterator i = m_caids.begin(); i != m_caids.end(); i++)
  {
    for(int j = 0; j < MAXCAIDS; j++) {

      if(channel->Ca(j) == 0)
        break;

      if(channel->Ca(j) == *i)
        return true;
    }
  }

  return false;
}

bool cXVDRClient::processRequest()
{
  cMutexLock lock(&m_msgLock);

  m_resp = new MsgPacket(m_req->getMsgID(), XVDR_CHANNEL_REQUEST_RESPONSE, m_req->getUID());
  m_resp->setProtocolVersion(XVDR_PROTOCOLVERSION);

  bool result = false;
  switch(m_req->getMsgID())
  {
    /** OPCODE 1 - 19: XVDR network functions for general purpose */
    case XVDR_LOGIN:
      result = process_Login();
      break;

    case XVDR_GETTIME:
      result = process_GetTime();
      break;

    case XVDR_ENABLESTATUSINTERFACE:
      result = process_EnableStatusInterface();
      break;

    case XVDR_UPDATECHANNELS:
      result = process_UpdateChannels();
      break;

   case XVDR_CHANNELFILTER:
      result = process_ChannelFilter();
      break;

    /** OPCODE 20 - 39: XVDR network functions for live streaming */
    case XVDR_CHANNELSTREAM_OPEN:
      result = processChannelStream_Open();
      break;

    case XVDR_CHANNELSTREAM_CLOSE:
      result = processChannelStream_Close();
      break;

    case XVDR_CHANNELSTREAM_REQUEST:
      result = processChannelStream_Request();
      break;

    case XVDR_CHANNELSTREAM_PAUSE:
      result = processChannelStream_Pause();
      break;

    case XVDR_CHANNELSTREAM_SIGNAL:
      result = processChannelStream_Signal();
      break;

    /** OPCODE 40 - 59: XVDR network functions for recording streaming */
    case XVDR_RECSTREAM_OPEN:
      result = processRecStream_Open();
      break;

    case XVDR_RECSTREAM_CLOSE:
      result = processRecStream_Close();
      break;

    case XVDR_RECSTREAM_GETBLOCK:
      result = processRecStream_GetBlock();
      break;

    case XVDR_RECSTREAM_UPDATE:
      result = processRecStream_Update();
      break;

    case XVDR_RECSTREAM_POSTOFRAME:
      result = processRecStream_PositionFromFrameNumber();
      break;

    case XVDR_RECSTREAM_FRAMETOPOS:
      result = processRecStream_FrameNumberFromPosition();
      break;

    case XVDR_RECSTREAM_GETIFRAME:
      result = processRecStream_GetIFrame();
      break;


    /** OPCODE 60 - 79: XVDR network functions for channel access */
    case XVDR_CHANNELS_GETCOUNT:
      result = processCHANNELS_ChannelsCount();
      break;

    case XVDR_CHANNELS_GETCHANNELS:
      result = processCHANNELS_GetChannels();
      break;

    case XVDR_CHANNELGROUP_GETCOUNT:
      result = processCHANNELS_GroupsCount();
      break;

    case XVDR_CHANNELGROUP_LIST:
      result = processCHANNELS_GroupList();
      break;

    case XVDR_CHANNELGROUP_MEMBERS:
      result = processCHANNELS_GetGroupMembers();
      break;

    /** OPCODE 80 - 99: XVDR network functions for timer access */
    case XVDR_TIMER_GETCOUNT:
      result = processTIMER_GetCount();
      break;

    case XVDR_TIMER_GET:
      result = processTIMER_Get();
      break;

    case XVDR_TIMER_GETLIST:
      result = processTIMER_GetList();
      break;

    case XVDR_TIMER_ADD:
      result = processTIMER_Add();
      break;

    case XVDR_TIMER_DELETE:
      result = processTIMER_Delete();
      break;

    case XVDR_TIMER_UPDATE:
      result = processTIMER_Update();
      break;


    /** OPCODE 100 - 119: XVDR network functions for recording access */
    case XVDR_RECORDINGS_DISKSIZE:
      result = processRECORDINGS_GetDiskSpace();
      break;

    case XVDR_RECORDINGS_GETCOUNT:
      result = processRECORDINGS_GetCount();
      break;

    case XVDR_RECORDINGS_GETLIST:
      result = processRECORDINGS_GetList();
      break;

    case XVDR_RECORDINGS_RENAME:
      result = processRECORDINGS_Rename();
      break;

    case XVDR_RECORDINGS_DELETE:
      result = processRECORDINGS_Delete();
      break;

    case XVDR_RECORDINGS_SETPLAYCOUNT:
      result = processRECORDINGS_SetPlayCount();
      break;

    case XVDR_RECORDINGS_SETPOSITION:
      result = processRECORDINGS_SetPosition();
      break;

    case XVDR_RECORDINGS_GETPOSITION:
      result = processRECORDINGS_GetPosition();
      break;


    /** OPCODE 120 - 139: XVDR network functions for epg access and manipulating */
    case XVDR_EPG_GETFORCHANNEL:
      result = processEPG_GetForChannel();
      break;


    /** OPCODE 140 - 159: XVDR network functions for channel scanning */
    case XVDR_SCAN_SUPPORTED:
      result = processSCAN_ScanSupported();
      break;

    case XVDR_SCAN_GETSETUP:
      result = processSCAN_GetSetup();
      break;

    case XVDR_SCAN_SETSETUP:
      result = processSCAN_SetSetup();
      break;

    case XVDR_SCAN_START:
      result = processSCAN_Start();
      break;

    case XVDR_SCAN_STOP:
      result = processSCAN_Stop();
      break;

    default:
      break;
  }

  if(result)
  {
    cSocketLock locks(m_socket);
    m_resp->write(m_socket, m_timeout);
  }

  delete m_resp;
  m_resp = NULL;

  return result;
}


/** OPCODE 1 - 19: XVDR network functions for general purpose */

bool cXVDRClient::process_Login() /* OPCODE 1 */
{
  m_protocolVersion      = m_req->getProtocolVersion();
  m_compressionLevel     = m_req->get_U8();
  const char *clientName = m_req->get_String();
  const char *language   = NULL;

  // get preferred language
  if(!m_req->eop())
  {
    language = m_req->get_String();
    m_LanguageIndex = I18nLanguageIndex(language);
    m_LangStreamType = (cStreamInfo::Type)m_req->get_U8();
  }

  if (m_protocolVersion > XVDR_PROTOCOLVERSION || m_protocolVersion < 4)
  {
    ERRORLOG("Client '%s' has unsupported protocol version '%u', terminating client", clientName, m_protocolVersion);
    return false;
  }

  INFOLOG("Welcome client '%s' with protocol version '%u'", clientName, m_protocolVersion);

  if(!m_LanguageIndex != -1) {
    INFOLOG("Preferred language: %s / type: %i", I18nLanguageCode(m_LanguageIndex), (int)m_LangStreamType);
  }

  // Send the login reply
  time_t timeNow        = time(NULL);
  struct tm* timeStruct = localtime(&timeNow);
  int timeOffset        = timeStruct->tm_gmtoff;

  m_resp->setProtocolVersion(m_protocolVersion);
  m_resp->put_U32(timeNow);
  m_resp->put_S32(timeOffset);
  m_resp->put_String("VDR-XVDR Server");
  m_resp->put_String(XVDR_VERSION);

  SetLoggedIn(true);
  return true;
}

bool cXVDRClient::process_GetTime() /* OPCODE 2 */
{
  time_t timeNow        = time(NULL);
  struct tm* timeStruct = localtime(&timeNow);
  int timeOffset        = timeStruct->tm_gmtoff;

  m_resp->put_U32(timeNow);
  m_resp->put_S32(timeOffset);

  return true;
}

bool cXVDRClient::process_EnableStatusInterface()
{
  bool enabled = m_req->get_U8();

  SetStatusInterface(enabled);

  m_resp->put_U32(XVDR_RET_OK);

  return true;
}

bool cXVDRClient::process_UpdateChannels()
{
  uint8_t updatechannels = m_req->get_U8();

  if(updatechannels <= 5)
  {
    Setup.UpdateChannels = updatechannels;
    INFOLOG("Setting channel update method: %i", updatechannels);
    m_resp->put_U32(XVDR_RET_OK);
  }
  else
    m_resp->put_U32(XVDR_RET_DATAINVALID);

  return true;
}

bool cXVDRClient::process_ChannelFilter()
{
  INFOLOG("Channellist filter:");

  // do we want fta channels ?
  m_wantfta = m_req->get_U32();
  INFOLOG("Free To Air channels: %s", m_wantfta ? "Yes" : "No");

  // display only channels with native language audio ?
  m_filterlanguage = m_req->get_U32();
  INFOLOG("Only native language: %s", m_filterlanguage ? "Yes" : "No");

  // read caids
  m_caids.clear();
  uint32_t count = m_req->get_U32();

  INFOLOG("Enabled CaIDs: ");

  // sanity check (maximum of 20 caids)
  if(count < 20) {
    for(uint32_t i = 0; i < count; i++) {
      int caid = m_req->get_U32();
      m_caids.push_back(caid);
      INFOLOG("%04X", caid);
    }
  }


  m_resp->put_U32(XVDR_RET_OK);

  return true;
}



/** OPCODE 20 - 39: XVDR network functions for live streaming */

bool cXVDRClient::processChannelStream_Open() /* OPCODE 20 */
{
  cMutexLock lock(&m_timerLock);
  SetPriority(-15);

  uint32_t uid = m_req->get_U32();
  int32_t priority = 50;

  if(!m_req->eop()) {
    priority = m_req->get_S32();
  }

  uint32_t timeout = XVDRServerConfig.stream_timeout;

  StopChannelStreaming();

  XVDRChannels.Lock(false);
  const cChannel *channel = NULL;

  // try to find channel by uid first
  channel = FindChannelByUID(uid);

  // try channelnumber
  if (channel == NULL)
    channel = XVDRChannels.Get()->GetByNumber(uid);

  XVDRChannels.Unlock();

  if (channel == NULL) {
    ERRORLOG("Can't find channel %08x", uid);
    m_resp->put_U32(XVDR_RET_DATAINVALID);
  }
  else
  {
    int status = StartChannelStreaming(channel, timeout, priority);

    if (status == XVDR_RET_OK)
      INFOLOG("Started streaming of channel %s (timeout %i seconds, priority %i)", channel->Name(), timeout, priority);
    else
      DEBUGLOG("Can't stream channel %s", channel->Name());

    m_resp->put_U32(status);
  }

  return true;
}

bool cXVDRClient::processChannelStream_Close() /* OPCODE 21 */
{
  StopChannelStreaming();
  return true;
}

bool cXVDRClient::processChannelStream_Request() /* OPCODE 22 */
{
  if(m_Streamer != NULL)
    m_Streamer->RequestPacket();

  // no response needed for the request
  return false;
}

bool cXVDRClient::processChannelStream_Pause() /* OPCODE 23 */
{
  bool on = m_req->get_U32();
  INFOLOG("LIVESTREAM: %s", on ? "PAUSED" : "TIMESHIFT");

  m_Streamer->Pause(on);

  return true;
}

bool cXVDRClient::processChannelStream_Signal() /* OPCODE 24 */
{
  if(m_Streamer == NULL)
    return false;

  m_Streamer->RequestSignalInfo();
  return false;
}

/** OPCODE 40 - 59: XVDR network functions for recording streaming */

bool cXVDRClient::processRecStream_Open() /* OPCODE 40 */
{
  cRecording *recording = NULL;
  SetPriority(-15);

  const char* recid = m_req->get_String();
  unsigned int uid = recid2uid(recid);
  DEBUGLOG("lookup recid: %s (uid: %u)", recid, uid);
  recording = cRecordingsCache::GetInstance().Lookup(uid);

  if (recording && m_RecPlayer == NULL)
  {
    m_RecPlayer = new cRecPlayer(recording);

    m_resp->put_U32(XVDR_RET_OK);
    m_resp->put_U32(m_RecPlayer->getLengthFrames());
    m_resp->put_U64(m_RecPlayer->getLengthBytes());

#if VDRVERSNUM < 10703
    m_resp->put_U8(true);//added for TS
#else
    m_resp->put_U8(recording->IsPesRecording());//added for TS
#endif
  }
  else
  {
    m_resp->put_U32(XVDR_RET_DATAUNKNOWN);
    ERRORLOG("%s - unable to start recording !", __FUNCTION__);
  }

  return true;
}

bool cXVDRClient::processRecStream_Close() /* OPCODE 41 */
{
  if (m_RecPlayer)
  {
    delete m_RecPlayer;
    m_RecPlayer = NULL;
  }

  m_resp->put_U32(XVDR_RET_OK);

  return true;
}

bool cXVDRClient::processRecStream_Update() /* OPCODE 46 */
{
  if(m_RecPlayer == NULL)
    return false;

  m_RecPlayer->update();
  m_resp->put_U32(m_RecPlayer->getLengthFrames());
  m_resp->put_U64(m_RecPlayer->getLengthBytes());

  return true;
}

bool cXVDRClient::processRecStream_GetBlock() /* OPCODE 42 */
{
  if (m_isStreaming)
  {
    ERRORLOG("Get block called during live streaming");
    return false;
  }

  if (!m_RecPlayer)
  {
    ERRORLOG("Get block called when no recording open");
    return false;
  }

  uint64_t position  = m_req->get_U64();
  uint32_t amount    = m_req->get_U32();

  uint8_t* p = m_resp->reserve(amount);
  uint32_t amountReceived = m_RecPlayer->getBlock(p, position, amount);

  // smaller chunk ?
  if(amountReceived < amount)
    m_resp->unreserve(amount - amountReceived);

  return true;
}

bool cXVDRClient::processRecStream_PositionFromFrameNumber() /* OPCODE 43 */
{
  uint64_t retval       = 0;
  uint32_t frameNumber  = m_req->get_U32();

  if (m_RecPlayer)
    retval = m_RecPlayer->positionFromFrameNumber(frameNumber);

  m_resp->put_U64(retval);

  return true;
}

bool cXVDRClient::processRecStream_FrameNumberFromPosition() /* OPCODE 44 */
{
  uint32_t retval   = 0;
  uint64_t position = m_req->get_U64();

  if (m_RecPlayer)
    retval = m_RecPlayer->frameNumberFromPosition(position);

  m_resp->put_U32(retval);

  return true;
}

bool cXVDRClient::processRecStream_GetIFrame() /* OPCODE 45 */
{
  bool success            = false;
  uint32_t frameNumber    = m_req->get_U32();
  uint32_t direction      = m_req->get_U32();
  uint64_t rfilePosition  = 0;
  uint32_t rframeNumber   = 0;
  uint32_t rframeLength   = 0;

  if (m_RecPlayer)
    success = m_RecPlayer->getNextIFrame(frameNumber, direction, &rfilePosition, &rframeNumber, &rframeLength);

  // returns file position, frame number, length
  if (success)
  {
    m_resp->put_U64(rfilePosition);
    m_resp->put_U32(rframeNumber);
    m_resp->put_U32(rframeLength);
  }
  else
  {
    m_resp->put_U32(0);
  }

  return true;
}

int cXVDRClient::ChannelsCount()
{
  XVDRChannels.Lock(false);
  cChannels *channels = XVDRChannels.Get();
  int count = 0;

  for (cChannel *channel = channels->First(); channel; channel = channels->Next(channel))
  {
    if(IsChannelWanted(channel, false)) count++;
    if(IsChannelWanted(channel, true)) count++;
  }

  XVDRChannels.Unlock();
  return count;
}

/** OPCODE 60 - 79: XVDR network functions for channel access */

bool cXVDRClient::processCHANNELS_ChannelsCount() /* OPCODE 61 */
{
  m_channelCount = ChannelsCount();
  m_resp->put_U32(m_channelCount);

  return true;
}

bool cXVDRClient::processCHANNELS_GetChannels() /* OPCODE 63 */
{
  bool radio = m_req->get_U32();

  m_channelCount = ChannelsCount();

  if(!XVDRChannels.Lock(false)) {
    return true;
  }

  cChannels *channels = XVDRChannels.Get();

  for (cChannel *channel = channels->First(); channel; channel = channels->Next(channel))
  {
    if(!IsChannelWanted(channel, radio))
      continue;

    m_resp->put_U32(channel->Number());
    m_resp->put_String(m_toUTF8.Convert(channel->Name()));
    m_resp->put_U32(CreateChannelUID(channel));
    m_resp->put_U32(channel->Ca());

    // logo url
    m_resp->put_String((const char*)CreateLogoURL(channel));

    // service reference
    if(m_protocolVersion > 4)
      m_resp->put_String((const char*)CreateServiceReference(channel));
  }

  XVDRChannels.Unlock();

  m_resp->compress(m_compressionLevel);

  return true;
}

bool cXVDRClient::processCHANNELS_GroupsCount()
{
  uint32_t type = m_req->get_U32();

  XVDRChannels.Lock(false);

  m_channelgroups[0].clear();
  m_channelgroups[1].clear();

  switch(type)
  {
    // get groups defined in channels.conf
    default:
    case 0:
      CreateChannelGroups(false);
      break;
    // automatically create groups
    case 1:
      CreateChannelGroups(true);
      break;
  }

  XVDRChannels.Unlock();

  uint32_t count = m_channelgroups[0].size() + m_channelgroups[1].size();

  m_resp->put_U32(count);

  return true;
}

bool cXVDRClient::processCHANNELS_GroupList()
{
  uint32_t radio = m_req->get_U8();
  std::map<std::string, ChannelGroup>::iterator i;

  for(i = m_channelgroups[radio].begin(); i != m_channelgroups[radio].end(); i++)
  {
    m_resp->put_String(i->second.name.c_str());
    m_resp->put_U8(i->second.radio);
  }

  return true;
}

bool cXVDRClient::processCHANNELS_GetGroupMembers()
{
  const char* groupname = m_req->get_String();
  uint32_t radio = m_req->get_U8();
  int index = 0;

  // unknown group
  if(m_channelgroups[radio].find(groupname) == m_channelgroups[radio].end())
  {
    return true;
  }

  bool automatic = m_channelgroups[radio][groupname].automatic;
  std::string name;

  m_channelCount = ChannelsCount();

  XVDRChannels.Lock(false);
  cChannels *channels = XVDRChannels.Get();

  for (cChannel *channel = channels->First(); channel; channel = channels->Next(channel))
  {

    if(automatic && !channel->GroupSep())
      name = channel->Provider();
    else
    {
      if(channel->GroupSep())
      {
        name = channel->Name();
        continue;
      }
    }

    if(name.empty())
      continue;

    if(!IsChannelWanted(channel, radio))
      continue;

    if(name == groupname)
    {
      m_resp->put_U32(CreateChannelUID(channel));
      m_resp->put_U32(++index);
    }
  }

  XVDRChannels.Unlock();
  return true;
}

void cXVDRClient::CreateChannelGroups(bool automatic)
{
  std::string groupname;
  cChannels *channels = XVDRChannels.Get();

  for (cChannel *channel = channels->First(); channel; channel = channels->Next(channel))
  {
    bool isRadio = IsRadio(channel);

    if(automatic && !channel->GroupSep())
      groupname = channel->Provider();
    else if(!automatic && channel->GroupSep())
      groupname = channel->Name();

    if(groupname.empty())
      continue;

    if(!IsChannelWanted(channel, isRadio))
      continue;

    if(m_channelgroups[isRadio].find(groupname) == m_channelgroups[isRadio].end())
    {
      ChannelGroup group;
      group.name = groupname;
      group.radio = isRadio;
      group.automatic = automatic;
      m_channelgroups[isRadio][groupname] = group;
    }
  }
}

/** OPCODE 80 - 99: XVDR network functions for timer access */

bool cXVDRClient::processTIMER_GetCount() /* OPCODE 80 */
{
  cMutexLock lock(&m_timerLock);

  int count = Timers.Count();

  m_resp->put_U32(count);

  return true;
}

bool cXVDRClient::processTIMER_Get() /* OPCODE 81 */
{
  cMutexLock lock(&m_timerLock);

  uint32_t number = m_req->get_U32();

  if (Timers.Count() == 0)
  {
    m_resp->put_U32(XVDR_RET_DATAUNKNOWN);
    return true;
  }

  cTimer *timer = Timers.Get(number-1);
  if (timer == NULL)
  {
    m_resp->put_U32(XVDR_RET_DATAUNKNOWN);
    return true;
  }

  m_resp->put_U32(XVDR_RET_OK);
  PutTimer(timer, m_resp);

  return true;
}

bool cXVDRClient::processTIMER_GetList() /* OPCODE 82 */
{
  cMutexLock lock(&m_timerLock);

  cTimer *timer;
  int numTimers = Timers.Count();

  m_resp->put_U32(numTimers);

  for (int i = 0; i < numTimers; i++)
  {
    timer = Timers.Get(i);
    if (!timer)
      continue;

    PutTimer(timer, m_resp);
  }

  return true;
}

bool cXVDRClient::processTIMER_Add() /* OPCODE 83 */
{
  cMutexLock lock(&m_timerLock);

  m_req->get_U32(); // index unused
  uint32_t flags      = m_req->get_U32() > 0 ? tfActive : tfNone;
  uint32_t priority   = m_req->get_U32();
  uint32_t lifetime   = m_req->get_U32();
  uint32_t channelid  = m_req->get_U32();
  time_t startTime    = m_req->get_U32();
  time_t stopTime     = m_req->get_U32();
  time_t day          = m_req->get_U32();
  uint32_t weekdays   = m_req->get_U32();
  const char *file    = m_req->get_String();
  const char *aux     = m_req->get_String();

  // handle instant timers
  if(startTime == -1 || startTime == 0)
  {
    startTime = time(NULL);
  }

  struct tm tm_r;
  struct tm *time = localtime_r(&startTime, &tm_r);
  if (day <= 0)
    day = cTimer::SetTime(startTime, 0);
  int start = time->tm_hour * 100 + time->tm_min;
  time = localtime_r(&stopTime, &tm_r);
  int stop = time->tm_hour * 100 + time->tm_min;

  cString buffer;
  XVDRChannels.Lock(false);
  const cChannel* channel = FindChannelByUID(channelid);
  if(channel != NULL) {
    buffer = cString::sprintf("%u:%s:%s:%04d:%04d:%d:%d:%s:%s\n", flags, (const char*)channel->GetChannelID().ToString(), *cTimer::PrintDay(day, weekdays, true), start, stop, priority, lifetime, file, aux);
  }
  XVDRChannels.Unlock();

  cTimer *timer = new cTimer;
  if (timer->Parse(buffer))
  {
    cTimer *t = Timers.GetTimer(timer);
    if (!t)
    {
      Timers.Add(timer);
      Timers.SetModified();
      INFOLOG("Timer %s added", *timer->ToDescr());
      m_resp->put_U32(XVDR_RET_OK);
      return true;
    }
    else
    {
      ERRORLOG("Timer already defined: %d %s", t->Index() + 1, *t->ToText());
      m_resp->put_U32(XVDR_RET_DATALOCKED);
    }
  }
  else
  {
    ERRORLOG("Error in timer settings");
    m_resp->put_U32(XVDR_RET_DATAINVALID);
  }

  delete timer;

  return true;
}

bool cXVDRClient::processTIMER_Delete() /* OPCODE 84 */
{
  cMutexLock lock(&m_timerLock);

  uint32_t number = m_req->get_U32();
  bool     force  = m_req->get_U32();

  if (number <= 0 || number > (uint32_t)Timers.Count())
  {
    ERRORLOG("Unable to delete timer - invalid timer identifier");
    m_resp->put_U32(XVDR_RET_DATAINVALID);
    return true;
  }

  cTimer *timer = Timers.Get(number-1);
  if (timer == NULL)
  {
    ERRORLOG("Unable to delete timer - invalid timer identifier");
    m_resp->put_U32(XVDR_RET_DATAINVALID);
    return true;
  }

  if (Timers.BeingEdited())
  {
    ERRORLOG("Unable to delete timer - timers being edited at VDR");
    m_resp->put_U32(XVDR_RET_DATALOCKED);
    return true;
  }

  if (timer->Recording() && !force)
  {
    ERRORLOG("Timer \"%i\" is recording and can be deleted (use force=1 to stop it)", number);
    m_resp->put_U32(XVDR_RET_RECRUNNING);
    return true;
  }

  timer->Skip();
  cRecordControls::Process(time(NULL));

  INFOLOG("Deleting timer %s", *timer->ToDescr());
  Timers.Del(timer);
  Timers.SetModified();
  m_resp->put_U32(XVDR_RET_OK);

  return true;
}

bool cXVDRClient::processTIMER_Update() /* OPCODE 85 */
{
  cMutexLock lock(&m_timerLock);

  uint32_t index  = m_req->get_U32();
  bool active     = m_req->get_U32();

  cTimer *timer = Timers.Get(index - 1);
  if (!timer)
  {
    ERRORLOG("Timer \"%u\" not defined", index);
    m_resp->put_U32(XVDR_RET_DATAUNKNOWN);
    return true;
  }

  if(timer->Recording())
  {
    INFOLOG("Will not update timer #%i - currently recording", index);
    m_resp->put_U32(XVDR_RET_OK);
    return true;
  }

  cTimer t = *timer;

  uint32_t flags      = active ? tfActive : tfNone;
  uint32_t priority   = m_req->get_U32();
  uint32_t lifetime   = m_req->get_U32();
  uint32_t channelid  = m_req->get_U32();
  time_t startTime    = m_req->get_U32();
  time_t stopTime     = m_req->get_U32();
  time_t day          = m_req->get_U32();
  uint32_t weekdays   = m_req->get_U32();
  const char *file    = m_req->get_String();
  const char *aux     = m_req->get_String();

  struct tm tm_r;
  struct tm *time = localtime_r(&startTime, &tm_r);

  if (day <= 0)
    day = cTimer::SetTime(startTime, 0);

  int start = time->tm_hour * 100 + time->tm_min;
  time = localtime_r(&stopTime, &tm_r);
  int stop = time->tm_hour * 100 + time->tm_min;

  cString buffer;
  XVDRChannels.Lock(false);
  const cChannel* channel = FindChannelByUID(channelid);

  if(channel != NULL)
    buffer = cString::sprintf("%u:%s:%s:%04d:%04d:%d:%d:%s:%s\n", flags, (const char*)channel->GetChannelID().ToString(), *cTimer::PrintDay(day, weekdays, true), start, stop, priority, lifetime, file, aux);

  XVDRChannels.Unlock();

  if (!t.Parse(buffer))
  {
    ERRORLOG("Error in timer settings");
    m_resp->put_U32(XVDR_RET_DATAINVALID);
    return true;
  }

  *timer = t;
  Timers.SetModified();

  m_resp->put_U32(XVDR_RET_OK);

  return true;
}


/** OPCODE 100 - 119: XVDR network functions for recording access */

bool cXVDRClient::processRECORDINGS_GetDiskSpace() /* OPCODE 100 */
{
  int FreeMB;
  int Percent = VideoDiskSpace(&FreeMB);
  int Total   = (FreeMB / (100 - Percent)) * 100;

  m_resp->put_U32(Total);
  m_resp->put_U32(FreeMB);
  m_resp->put_U32(Percent);

  return true;
}

bool cXVDRClient::processRECORDINGS_GetCount() /* OPCODE 101 */
{
  Recordings.Load();
  m_resp->put_U32(Recordings.Count());

  return true;
}

bool cXVDRClient::processRECORDINGS_GetList() /* OPCODE 102 */
{
  cMutexLock lock(&m_timerLock);
  cRecordingsCache& reccache = cRecordingsCache::GetInstance();

  for (cRecording *recording = Recordings.First(); recording; recording = Recordings.Next(recording))
  {
#if APIVERSNUM >= 10705
    const cEvent *event = recording->Info()->GetEvent();
#else
    const cEvent *event = NULL;
#endif

    time_t recordingStart    = 0;
    int    recordingDuration = 0;
    if (event)
    {
      recordingStart    = event->StartTime();
      recordingDuration = event->Duration();
    }
    else
    {
      cRecordControl *rc = cRecordControls::GetRecordControl(recording->FileName());
      if (rc)
      {
        recordingStart    = rc->Timer()->StartTime();
        recordingDuration = rc->Timer()->StopTime() - recordingStart;
      }
      else
      {
#if APIVERSNUM >= 10727
        recordingStart = recording->Start();
#else
        recordingStart = recording->start;
#endif
      }
    }
    DEBUGLOG("GRI: RC: recordingStart=%lu recordingDuration=%i", recordingStart, recordingDuration);

    // recording_time
    m_resp->put_U32(recordingStart);

    // duration
    m_resp->put_U32(recordingDuration);

    // priority
    m_resp->put_U32(
#if APIVERSNUM >= 10727
    recording->Priority()
#else
    recording->priority
#endif
    );

    // lifetime
    m_resp->put_U32(
#if APIVERSNUM >= 10727
    recording->Lifetime()
#else
    recording->lifetime
#endif
    );

    // channel_name
    m_resp->put_String(recording->Info()->ChannelName() ? m_toUTF8.Convert(recording->Info()->ChannelName()) : "");

    char* fullname = strdup(recording->Name());
    char* recname = strrchr(fullname, FOLDERDELIMCHAR);
    char* directory = NULL;

    if(recname == NULL) {
      recname = fullname;
    }
    else {
      *recname = 0;
      recname++;
      directory = fullname;
    }

    // title
    m_resp->put_String(m_toUTF8.Convert(recname));

    // subtitle
    if (!isempty(recording->Info()->ShortText()))
      m_resp->put_String(m_toUTF8.Convert(recording->Info()->ShortText()));
    else
      m_resp->put_String("");

    // description
    if (!isempty(recording->Info()->Description()))
      m_resp->put_String(m_toUTF8.Convert(recording->Info()->Description()));
    else
      m_resp->put_String("");

    // directory
    if(directory != NULL) {
      char* p = directory;
      while(*p != 0) {
        if(*p == FOLDERDELIMCHAR) *p = '/';
        if(*p == '_') *p = ' ';
        p++;
      }
      while(*directory == '/') directory++;
    }

    m_resp->put_String((isempty(directory)) ? "" : m_toUTF8.Convert(directory));

    // filename / uid of recording
    uint32_t uid = cRecordingsCache::GetInstance().Register(recording);
    char recid[9];
    snprintf(recid, sizeof(recid), "%08x", uid);
    m_resp->put_String(recid);

    // playcount
    m_resp->put_U32(reccache.GetPlayCount(uid));

    // content
    if(event != NULL)
      m_resp->put_U32(event->Contents());
    else
      m_resp->put_U32(0);

    // thumbnail url - for future use
    m_resp->put_String("");

    // icon url - for future use
    m_resp->put_String("");

    free(fullname);
  }

  m_resp->compress(m_compressionLevel);

  return true;
}

bool cXVDRClient::processRECORDINGS_Rename() /* OPCODE 103 */
{
  uint32_t uid = 0;
  const char* recid = m_req->get_String();
  uid = recid2uid(recid);

  const char* newtitle     = m_req->get_String();
  cRecording* recording    = cRecordingsCache::GetInstance().Lookup(uid);
  int         r            = XVDR_RET_DATAINVALID;

  if(recording != NULL) {
    // get filename and remove last part (recording time)
    char* filename_old = strdup((const char*)recording->FileName());
    char* sep = strrchr(filename_old, '/');
    if(sep != NULL) {
      *sep = 0;
    }

    // replace spaces in newtitle
    strreplace((char*)newtitle, ' ', '_');
    char* filename_new = new char[512];
    strncpy(filename_new, filename_old, 512);
    sep = strrchr(filename_new, '/');
    if(sep != NULL) {
      sep++;
      *sep = 0;
    }
    strncat(filename_new, newtitle, 512);

    INFOLOG("renaming recording '%s' to '%s'", filename_old, filename_new);
    r = rename(filename_old, filename_new);
    Recordings.Update();

    free(filename_old);
    delete[] filename_new;
  }

  m_resp->put_U32(r);

  return true;
}

bool cXVDRClient::processRECORDINGS_Delete() /* OPCODE 104 */
{
  const char* recid = m_req->get_String();
  uint32_t uid = recid2uid(recid);
  cRecording* recording = cRecordingsCache::GetInstance().Lookup(uid);

  if (recording == NULL)
  {
    ERRORLOG("Recording not found !");
    m_resp->put_U32(XVDR_RET_DATAUNKNOWN);
    return true;
  }

  DEBUGLOG("deleting recording: %s", recording->Name());

  cRecordControl *rc = cRecordControls::GetRecordControl(recording->FileName());
  if (rc != NULL)
  {
    ERRORLOG("Recording \"%s\" is in use by timer %d", recording->Name(), rc->Timer()->Index() + 1);
    m_resp->put_U32(XVDR_RET_DATALOCKED);
    return true;
  }

  if (!recording->Delete())
  {
    ERRORLOG("Error while deleting recording!");
    m_resp->put_U32(XVDR_RET_ERROR);
    return true;
  }

  Recordings.DelByName(recording->FileName());
  INFOLOG("Recording \"%s\" deleted", recording->FileName());
  m_resp->put_U32(XVDR_RET_OK);

  return true;
}

bool cXVDRClient::processRECORDINGS_SetPlayCount()
{
  const char* recid = m_req->get_String();
  uint32_t count = m_req->get_U32();

  uint32_t uid = recid2uid(recid);
  cRecordingsCache::GetInstance().SetPlayCount(uid, count);
  cRecordingsCache::GetInstance().SaveResumeData();

  return true;
}

bool cXVDRClient::processRECORDINGS_SetPosition()
{
  const char* recid = m_req->get_String();
  uint64_t position = m_req->get_U64();

  uint32_t uid = recid2uid(recid);
  cRecordingsCache::GetInstance().SetLastPlayedPosition(uid, position);
  cRecordingsCache::GetInstance().SaveResumeData();

  return true;
}

bool cXVDRClient::processRECORDINGS_GetPosition()
{
  const char* recid = m_req->get_String();

  uint32_t uid = recid2uid(recid);
  uint64_t position = cRecordingsCache::GetInstance().GetLastPlayedPosition(uid);

  m_resp->put_U64(position);
  return true;
}


/** OPCODE 120 - 139: XVDR network functions for epg access and manipulating */

bool cXVDRClient::processEPG_GetForChannel() /* OPCODE 120 */
{
  uint32_t channelUID = m_req->get_U32();
  uint32_t startTime  = m_req->get_U32();
  uint32_t duration   = m_req->get_U32();

  XVDRChannels.Lock(false);

  const cChannel* channel = NULL;

  channel = FindChannelByUID(channelUID);
  if(channel != NULL) {
    DEBUGLOG("get schedule called for channel '%s'", (const char*)channel->GetChannelID().ToString());
  }

  if (!channel)
  {
    m_resp->put_U32(0);
    XVDRChannels.Unlock();

    ERRORLOG("written 0 because channel = NULL");
    return true;
  }

  cSchedulesLock MutexLock;
  const cSchedules *Schedules = cSchedules::Schedules(MutexLock);
  if (!Schedules)
  {
    m_resp->put_U32(0);
    XVDRChannels.Unlock();

    DEBUGLOG("written 0 because Schedule!s! = NULL");
    return true;
  }

  const cSchedule *Schedule = Schedules->GetSchedule(channel->GetChannelID());
  if (!Schedule)
  {
    m_resp->put_U32(0);
    XVDRChannels.Unlock();

    DEBUGLOG("written 0 because Schedule = NULL");
    return true;
  }

  bool atLeastOneEvent = false;

  uint32_t thisEventID;
  uint32_t thisEventTime;
  uint32_t thisEventDuration;
  uint32_t thisEventContent;
  uint32_t thisEventRating;
  const char* thisEventTitle;
  const char* thisEventSubTitle;
  const char* thisEventDescription;

  for (const cEvent* event = Schedule->Events()->First(); event; event = Schedule->Events()->Next(event))
  {
    thisEventID           = event->EventID();
    thisEventTitle        = event->Title();
    thisEventSubTitle     = event->ShortText();
    thisEventDescription  = event->Description();
    thisEventTime         = event->StartTime();
    thisEventDuration     = event->Duration();
#if defined(USE_PARENTALRATING) || defined(PARENTALRATINGCONTENTVERSNUM)
    thisEventContent      = event->Contents();
    thisEventRating       = 0;
#elif APIVERSNUM >= 10711
    thisEventContent      = event->Contents();
    thisEventRating       = event->ParentalRating();
#else
    thisEventContent      = 0;
    thisEventRating       = 0;
#endif

    //in the past filter
    if ((thisEventTime + thisEventDuration) < (uint32_t)time(NULL)) continue;

    //start time filter
    if ((thisEventTime + thisEventDuration) <= startTime) continue;

    //duration filter
    if (duration != 0 && thisEventTime >= (startTime + duration)) continue;

    if (!thisEventTitle)        thisEventTitle        = "";
    if (!thisEventSubTitle)     thisEventSubTitle     = "";
    if (!thisEventDescription)  thisEventDescription  = "";

    m_resp->put_U32(thisEventID);
    m_resp->put_U32(thisEventTime);
    m_resp->put_U32(thisEventDuration);
    m_resp->put_U32(thisEventContent);
    m_resp->put_U32(thisEventRating);

    m_resp->put_String(m_toUTF8.Convert(thisEventTitle));
    m_resp->put_String(m_toUTF8.Convert(thisEventSubTitle));
    m_resp->put_String(m_toUTF8.Convert(thisEventDescription));

    atLeastOneEvent = true;
  }

  XVDRChannels.Unlock();
  DEBUGLOG("Got all event data");

  if (!atLeastOneEvent)
  {
    m_resp->put_U32(0);
    DEBUGLOG("Written 0 because no data");
  }

  m_resp->compress(m_compressionLevel);

  return true;
}


/** OPCODE 140 - 169: XVDR network functions for channel scanning */

bool cXVDRClient::processSCAN_ScanSupported() /* OPCODE 140 */
{
  if(m_scanner.Connect()) {
    m_resp->put_U32(XVDR_RET_OK);
  }
  else {
    m_resp->put_U32(XVDR_RET_NOTSUPPORTED);
  }

  return true;
}

bool cXVDRClient::processSCAN_GetSetup() /* OPCODE 141 */
{
  // get setup
  WIRBELSCAN_SERVICE::cWirbelscanScanSetup setup;
  if(!m_scanner.GetSetup(setup)) {
    INFOLOG("Unable to get wirbelscan setup !");
    m_resp->put_U32(XVDR_RET_NOTSUPPORTED);
    return true;
  }

  // get satellites
  cWirbelScan::List satellites;
  if(!m_scanner.GetSat(satellites)) {
    INFOLOG("Unable to get wirbelscan satellite list !");
    m_resp->put_U32(XVDR_RET_NOTSUPPORTED);
    return true;
  }

  // get coutries
  cWirbelScan::List countries;
  if(!m_scanner.GetCountry(countries)) {
    INFOLOG("Unable to get wirbelscan country list !");
    m_resp->put_U32(XVDR_RET_NOTSUPPORTED);
    return true;
  }

  // assemble response packet
  m_resp->put_U32(XVDR_RET_OK);

  // add setup
  m_resp->put_U16(setup.verbosity);
  m_resp->put_U16(setup.logFile);
  m_resp->put_U16(setup.DVB_Type);
  m_resp->put_U16(setup.DVBT_Inversion);
  m_resp->put_U16(setup.DVBC_Inversion);
  m_resp->put_U16(setup.DVBC_Symbolrate);
  m_resp->put_U16(setup.DVBC_QAM);
  m_resp->put_U16(setup.CountryId);
  m_resp->put_U16(setup.SatId);
  m_resp->put_U32(setup.scanflags);
  m_resp->put_U16(setup.ATSC_type);

  cCharSetConv toUTF8("ISO-8859-1", "UTF-8");

  // add satellites
  m_resp->put_U16(satellites.size());
  for(cWirbelScan::List::iterator i = satellites.begin(); i != satellites.end(); i++) {
    m_resp->put_S32(i->id);
    m_resp->put_String(toUTF8.Convert(i->short_name));
    m_resp->put_String(toUTF8.Convert(i->full_name));
  }

  // add countries
  m_resp->put_U16(countries.size());
  for(cWirbelScan::List::iterator i = countries.begin(); i != countries.end(); i++) {
    m_resp->put_S32(i->id);
    m_resp->put_String(toUTF8.Convert(i->short_name));
    m_resp->put_String(toUTF8.Convert(i->full_name));
  }

  m_resp->compress(m_compressionLevel);
  return true;
}

bool cXVDRClient::processSCAN_SetSetup() /* OPCODE 141 */
{
  WIRBELSCAN_SERVICE::cWirbelscanScanSetup setup;

  // read setup
  setup.verbosity = m_req->get_U16();
  setup.logFile = m_req->get_U16();
  setup.DVB_Type = m_req->get_U16();
  setup.DVBT_Inversion = m_req->get_U16();
  setup.DVBC_Inversion = m_req->get_U16();
  setup.DVBC_Symbolrate = m_req->get_U16();
  setup.DVBC_QAM = m_req->get_U16();
  setup.CountryId = m_req->get_U16();
  setup.SatId = m_req->get_U16();
  setup.scanflags = m_req->get_U32();
  setup.ATSC_type = m_req->get_U16();

  INFOLOG("Logfile: %i", setup.logFile);

  // set setup
  if(!m_scanner.SetSetup(setup)) {
    INFOLOG("Unable to set wirbelscan setup !");
    m_resp->put_U32(XVDR_RET_NOTSUPPORTED);
    return true;
  }

  // store setup
  WIRBELSCAN_SERVICE::cWirbelscanCmd cmd;
  cmd.cmd = WIRBELSCAN_SERVICE::CmdStore;

  if(!m_scanner.DoCmd(cmd)) {
    INFOLOG("Unable to store wirbelscan setup !");
    m_resp->put_U32(XVDR_RET_NOTSUPPORTED);
    return true;
  }

  INFOLOG("new wirbelscan setup stored.");

  m_resp->put_U32(XVDR_RET_OK);
  return true;
}

bool cXVDRClient::processSCAN_Start() {
  WIRBELSCAN_SERVICE::cWirbelscanCmd cmd;
  cmd.cmd = WIRBELSCAN_SERVICE::CmdStartScan;

  if(!m_scanner.DoCmd(cmd)) {
    INFOLOG("Unable to start channel scanner !");
    m_resp->put_U32(XVDR_RET_NOTSUPPORTED);
    return true;
  }

  INFOLOG("channel scanner started ...");

  m_resp->put_U32(XVDR_RET_OK);
  return true;
}

bool cXVDRClient::processSCAN_Stop() {
  WIRBELSCAN_SERVICE::cWirbelscanCmd cmd;
  cmd.cmd = WIRBELSCAN_SERVICE::CmdStopScan;

  if(!m_scanner.DoCmd(cmd)) {
    INFOLOG("Unable to stop channel scanner !");
    m_resp->put_U32(XVDR_RET_NOTSUPPORTED);
    return true;
  }

  INFOLOG("channel scanner stopped.");

  m_resp->put_U32(XVDR_RET_OK);
  return true;
}

void cXVDRClient::SendScannerStatus() {
  WIRBELSCAN_SERVICE::cWirbelscanStatus status;
  if(!m_scanner.GetStatus(status)) {
    return;
  }

  MsgPacket* resp = new MsgPacket(XVDR_STATUS_CHANNELSCAN, XVDR_CHANNEL_STATUS);
  resp->setProtocolVersion(XVDR_PROTOCOLVERSION);

  resp->put_U8((uint8_t)status.status);
  resp->put_U16(status.progress);
  resp->put_U16(status.strength);
  resp->put_U16(status.numChannels);
  resp->put_U16(status.newChannels);
  resp->put_String(status.curr_device);
  resp->put_String(status.transponder);

  m_resp->compress(m_compressionLevel);
  resp->write(m_socket, m_timeout);
  delete resp;
}
