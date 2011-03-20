/*
 *      Copyright (C) 2010 Alwin Esch (Team XBMC)
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
#include <stdio.h>
#include <vdr/recording.h>
#include <vdr/channels.h>
#include <vdr/videodir.h>
#include <vdr/plugin.h>
#include <vdr/timers.h>
#include <vdr/menu.h>
#include <vdr/videodir.h>

#include "hash.h"
#include "config.h"
#include "cmdcontrol.h"
#include "connection.h"
#include "recplayer.h"
#include "vdrcommand.h"
#include "recordingscache.h"
#include "wirbelscanservice.h" /// copied from modified wirbelscan plugin
                               /// must be hold up to date with wirbelscan

cCmdControl::cCmdControl()
{
  m_req                   = NULL;
  m_resp                  = NULL;
  m_processSCAN_Response  = NULL;
  m_processSCAN_Socket    = NULL;

  Start();
}

cCmdControl::~cCmdControl()
{
  Cancel(10);
}

bool cCmdControl::recvRequest(cRequestPacket* newRequest)
{
  m_mutex.Lock();
  m_req_queue.push(newRequest);
  m_mutex.Unlock();
  m_Wait.Signal();
  return true;
}

void cCmdControl::Action(void)
{
  while (Running())
  {
    LOGCONSOLE("threadMethod waiting");
    m_Wait.Wait(500);  // unlocks, waits, relocks

    m_mutex.Lock();
    size_t s = m_req_queue.size();
    m_mutex.Unlock();

    while(s > 0)
    {
      m_mutex.Lock();
      m_req = m_req_queue.front();
      m_req_queue.pop();
      s = m_req_queue.size();
      m_mutex.Unlock();

      if (!processPacket())
      {
        esyslog("VNSI-Error: Response handler failed during processPacket, exiting thread");
        continue;
      }
    }
  }
}

bool cCmdControl::processPacket()
{
  m_resp = new cResponsePacket();
  if (!m_resp->init(m_req->getRequestID()))
  {
    esyslog("VNSI-Error: Response packet init fail");
    delete m_resp;
    delete m_req;
    m_resp = NULL;
    return false;
  }

  bool result = false;
  switch(m_req->getOpCode())
  {
    /** OPCODE 1 - 19: VNSI network functions for general purpose */
    case VDR_LOGIN:
      result = process_Login();
      break;

    case VDR_GETTIME:
      result = process_GetTime();
      break;

    case VDR_ENABLESTATUSINTERFACE:
      result = process_EnableStatusInterface();
      break;

    case VDR_ENABLEOSDINTERFACE:
      result = process_EnableOSDInterface();
      break;


    /** OPCODE 20 - 39: VNSI network functions for live streaming */
    /** NOTE: Live streaming opcodes are handled by cConnection::Action(void) */


    /** OPCODE 40 - 59: VNSI network functions for recording streaming */
    case VDR_RECSTREAM_OPEN:
      result = processRecStream_Open();
      break;

    case VDR_RECSTREAM_CLOSE:
      result = processRecStream_Close();
      break;

    case VDR_RECSTREAM_GETBLOCK:
      result = processRecStream_GetBlock();
      break;

    case VDR_RECSTREAM_POSTOFRAME:
      result = processRecStream_PositionFromFrameNumber();
      break;

    case VDR_RECSTREAM_FRAMETOPOS:
      result = processRecStream_FrameNumberFromPosition();
      break;

    case VDR_RECSTREAM_GETIFRAME:
      result = processRecStream_GetIFrame();
      break;


    /** OPCODE 60 - 79: VNSI network functions for channel access */
    case VDR_CHANNELS_GETCOUNT:
      result = processCHANNELS_ChannelsCount();
      break;

    case VDR_CHANNELS_GETCHANNELS:
      result = processCHANNELS_GetChannels();
      break;


    /** OPCODE 80 - 99: VNSI network functions for timer access */
    case VDR_TIMER_GETCOUNT:
      result = processTIMER_GetCount();
      break;

    case VDR_TIMER_GET:
      result = processTIMER_Get();
      break;

    case VDR_TIMER_GETLIST:
      result = processTIMER_GetList();
      break;

    case VDR_TIMER_ADD:
      result = processTIMER_Add();
      break;

    case VDR_TIMER_DELETE:
      result = processTIMER_Delete();
      break;

    case VDR_TIMER_UPDATE:
      result = processTIMER_Update();
      break;


    /** OPCODE 100 - 119: VNSI network functions for recording access */
    case VDR_RECORDINGS_DISKSIZE:
      result = processRECORDINGS_GetDiskSpace();
      break;

    case VDR_RECORDINGS_GETCOUNT:
      result = processRECORDINGS_GetCount();
      break;

    case VDR_RECORDINGS_GETLIST:
      result = processRECORDINGS_GetList();
      break;

    case VDR_RECORDINGS_RENAME:
      result = processRECORDINGS_Rename();
      break;

    case VDR_RECORDINGS_DELETE:
      result = processRECORDINGS_Delete();
      break;


    /** OPCODE 120 - 139: VNSI network functions for epg access and manipulating */
    case VDR_EPG_GETFORCHANNEL:
      result = processEPG_GetForChannel();
      break;


    /** OPCODE 140 - 159: VNSI network functions for channel scanning */
    case VDR_SCAN_SUPPORTED:
      result = processSCAN_ScanSupported();
      break;

    case VDR_SCAN_GETCOUNTRIES:
      result = processSCAN_GetCountries();
      break;

    case VDR_SCAN_GETSATELLITES:
      result = processSCAN_GetSatellites();
      break;

    case VDR_SCAN_START:
      result = processSCAN_Start();
      break;

    case VDR_SCAN_STOP:
      result = processSCAN_Stop();
      break;
  }

  delete m_resp;
  m_resp = NULL;

  delete m_req;
  m_req = NULL;

  return result;
}


/** OPCODE 1 - 19: VNSI network functions for general purpose */

bool cCmdControl::process_Login() /* OPCODE 1 */
{
  if (m_req->getDataLength() <= 4) return false;

  m_protocolVersion      = m_req->extract_U32();
                           m_req->extract_U8();
  const char *clientName = m_req->extract_String();

  if (m_protocolVersion > VNSIProtocolVersion)
  {
    esyslog("VNSI-Error: Client '%s' have a not allowed protocol version '%u', terminating client", clientName, m_protocolVersion);
    delete[] clientName;
    return false;
  }

  isyslog("VNSI: Welcome client '%s' with protocol version '%u'", clientName, m_protocolVersion);

  // Send the login reply
  time_t timeNow        = time(NULL);
  struct tm* timeStruct = localtime(&timeNow);
  int timeOffset        = timeStruct->tm_gmtoff;

  m_resp->add_U32(VNSIProtocolVersion);
  m_resp->add_U32(timeNow);
  m_resp->add_S32(timeOffset);
  m_resp->add_String("VDR-Network-Streaming-Interface (VNSI) Server");
  m_resp->add_String(VNSI_SERVER_VERSION);
  m_resp->finalise();
  m_req->getClient()->GetSocket()->write(m_resp->getPtr(), m_resp->getLen());

  m_req->getClient()->SetLoggedIn(true);
  delete[] clientName;

  return true;
}

bool cCmdControl::process_GetTime() /* OPCODE 2 */
{
  time_t timeNow        = time(NULL);
  struct tm* timeStruct = localtime(&timeNow);
  int timeOffset        = timeStruct->tm_gmtoff;

  m_resp->add_U32(timeNow);
  m_resp->add_S32(timeOffset);
  m_resp->finalise();
  m_req->getClient()->GetSocket()->write(m_resp->getPtr(), m_resp->getLen());
  return true;
}

bool cCmdControl::process_EnableStatusInterface()
{
  bool enabled = m_req->extract_U8();

  m_req->getClient()->SetStatusInterface(enabled);

  m_resp->add_U32(VDR_RET_OK);
  m_resp->finalise();
  m_req->getClient()->GetSocket()->write(m_resp->getPtr(), m_resp->getLen());
  return true;
}

bool cCmdControl::process_EnableOSDInterface()
{
  bool enabled = m_req->extract_U8();

  m_req->getClient()->SetOSDInterface(enabled);

  m_resp->add_U32(VDR_RET_OK);
  m_resp->finalise();
  m_req->getClient()->GetSocket()->write(m_resp->getPtr(), m_resp->getLen());
  return true;
}


/** OPCODE 20 - 39: VNSI network functions for live streaming */
/** NOTE: Live streaming opcodes are handled by cConnection::Action(void) */


/** OPCODE 40 - 59: VNSI network functions for recording streaming */

bool cCmdControl::processRecStream_Open() /* OPCODE 40 */
{
  cRecording *recording = NULL;

  if(m_protocolVersion >= 2) {
    uint32_t uid = m_req->extract_U32();
    recording = cRecordingsCache::GetInstance().Lookup(uid);
  }
  else {
    const char *fileName = m_req->extract_String();
    recording = Recordings.GetByName(fileName);
    delete[] fileName;
  }

  if (recording && m_req->getClient()->m_RecPlayer == NULL)
  {
    m_req->getClient()->m_RecPlayer = new cRecPlayer(recording);

    m_resp->add_U32(VDR_RET_OK);
    m_resp->add_U32(m_req->getClient()->m_RecPlayer->getLengthFrames());
    m_resp->add_U64(m_req->getClient()->m_RecPlayer->getLengthBytes());

#if VDRVERSNUM < 10703
    m_resp->add_U8(true);//added for TS
#else
    m_resp->add_U8(recording->IsPesRecording());//added for TS
#endif
  }
  else
  {
    m_resp->add_U32(VDR_RET_DATAUNKNOWN);
    esyslog("%s - unable to start recording !", __FUNCTION__);
  }

  m_resp->finalise();
  m_req->getClient()->GetSocket()->write(m_resp->getPtr(), m_resp->getLen());

  return true;
}

bool cCmdControl::processRecStream_Close() /* OPCODE 41 */
{
  if (m_req->getClient()->m_RecPlayer)
  {
    delete m_req->getClient()->m_RecPlayer;
    m_req->getClient()->m_RecPlayer = NULL;
  }

  m_resp->add_U32(VDR_RET_OK);
  m_resp->finalise();
  m_req->getClient()->GetSocket()->write(m_resp->getPtr(), m_resp->getLen());
  return true;
}

bool cCmdControl::processRecStream_GetBlock() /* OPCODE 42 */
{
  if (m_req->getClient()->IsStreaming())
  {
    esyslog("VNSI-Error: Get block called during live streaming");
    return false;
  }

  if (!m_req->getClient()->m_RecPlayer)
  {
    esyslog("VNSI-Error: Get block called when no recording open");
    return false;
  }

  uint64_t position  = m_req->extract_U64();
  uint32_t amount    = m_req->extract_U32();

  uint8_t* p = m_resp->reserve(amount);
  uint32_t amountReceived = m_req->getClient()->m_RecPlayer->getBlock(p, position, amount);

  if(amount > amountReceived) m_resp->unreserve(amount - amountReceived);

  if (!amountReceived)
  {
    m_resp->add_U32(0);
    LOGCONSOLE("written 4(0) as getblock got 0");
  }

  m_resp->finalise();
  m_req->getClient()->GetSocket()->write(m_resp->getPtr(), m_resp->getLen());
  return true;
}

bool cCmdControl::processRecStream_PositionFromFrameNumber() /* OPCODE 43 */
{
  uint64_t retval       = 0;
  uint32_t frameNumber  = m_req->extract_U32();

  if (m_req->getClient()->m_RecPlayer)
    retval = m_req->getClient()->m_RecPlayer->positionFromFrameNumber(frameNumber);

  m_resp->add_U64(retval);
  m_resp->finalise();
  m_req->getClient()->GetSocket()->write(m_resp->getPtr(), m_resp->getLen());

  LOGCONSOLE("Wrote posFromFrameNum reply to client");
  return true;
}

bool cCmdControl::processRecStream_FrameNumberFromPosition() /* OPCODE 44 */
{
  uint32_t retval   = 0;
  uint64_t position = m_req->extract_U64();

  if (m_req->getClient()->m_RecPlayer)
    retval = m_req->getClient()->m_RecPlayer->frameNumberFromPosition(position);

  m_resp->add_U32(retval);
  m_resp->finalise();
  m_req->getClient()->GetSocket()->write(m_resp->getPtr(), m_resp->getLen());

  LOGCONSOLE("Wrote frameNumFromPos reply to client");
  return true;
}

bool cCmdControl::processRecStream_GetIFrame() /* OPCODE 45 */
{
  bool success            = false;
  uint32_t frameNumber    = m_req->extract_U32();
  uint32_t direction      = m_req->extract_U32();
  uint64_t rfilePosition  = 0;
  uint32_t rframeNumber   = 0;
  uint32_t rframeLength   = 0;

  if (m_req->getClient()->m_RecPlayer)
    success = m_req->getClient()->m_RecPlayer->getNextIFrame(frameNumber, direction, &rfilePosition, &rframeNumber, &rframeLength);

  // returns file position, frame number, length
  if (success)
  {
    m_resp->add_U64(rfilePosition);
    m_resp->add_U32(rframeNumber);
    m_resp->add_U32(rframeLength);
  }
  else
  {
    m_resp->add_U32(0);
  }

  m_resp->finalise();
  m_req->getClient()->GetSocket()->write(m_resp->getPtr(), m_resp->getLen());

  LOGCONSOLE("Wrote GNIF reply to client %llu %lu %lu", rfilePosition, rframeNumber, rframeLength);
  return true;
}


/** OPCODE 60 - 79: VNSI network functions for channel access */

bool cCmdControl::processCHANNELS_ChannelsCount() /* OPCODE 61 */
{
  int count = Channels.MaxNumber();

  m_resp->add_U32(count);

  m_resp->finalise();
  m_req->getClient()->GetSocket()->write(m_resp->getPtr(), m_resp->getLen());
  return true;
}

bool cCmdControl::processCHANNELS_GetChannels() /* OPCODE 63 */
{
  if (m_req->getDataLength() != 4) return false;

  bool radio = m_req->extract_U32();

  int groupIndex = 0;
  const cChannel* group = NULL;
  for (cChannel *channel = Channels.First(); channel; channel = Channels.Next(channel))
  {
    if (channel->GroupSep())
    {
      group = channel;
      groupIndex++;
    }
    else
    {
      bool isRadio = false;

      if (channel->Vpid())
        isRadio = false;
      else if (channel->Apid(0))
        isRadio = true;
      else
        continue;

      if (radio != isRadio)
        continue;

      // skip invalid channels
      if (channel->Sid() == 0)
        continue;

      m_resp->add_U32(channel->Number());
      m_resp->add_String(m_toUTF8.Convert(channel->Name()));
      if(m_protocolVersion >= 2) {
        m_resp->add_U32(CreateChannelUID(channel));
      }
      else {
        m_resp->add_U32(channel->Sid());
      }
      m_resp->add_U32(groupIndex);
      m_resp->add_U32(channel->Ca());
#if APIVERSNUM >= 10701
      m_resp->add_U32(channel->Vtype());
#else
      m_resp->add_U32(2);
#endif
    }
  }

  m_resp->finalise();
  m_req->getClient()->GetSocket()->write(m_resp->getPtr(), m_resp->getLen());
  return true;
}


/** OPCODE 80 - 99: VNSI network functions for timer access */

bool cCmdControl::processTIMER_GetCount() /* OPCODE 80 */
{
  int count = Timers.Count();

  m_resp->add_U32(count);

  m_resp->finalise();
  m_req->getClient()->GetSocket()->write(m_resp->getPtr(), m_resp->getLen());
  return true;
}

bool cCmdControl::processTIMER_Get() /* OPCODE 81 */
{
  uint32_t number = m_req->extract_U32();

  int numTimers = Timers.Count();
  if (numTimers > 0)
  {
    cTimer *timer = Timers.Get(number-1);
    if (timer)
    {
      m_resp->add_U32(VDR_RET_OK);

      m_resp->add_U32(timer->Index()+1);
      m_resp->add_U32(timer->HasFlags(tfActive));
      m_resp->add_U32(timer->Recording());
      m_resp->add_U32(timer->Pending());
      m_resp->add_U32(timer->Priority());
      m_resp->add_U32(timer->Lifetime());
      m_resp->add_U32(timer->Channel()->Number());
      if(m_protocolVersion >= 2) {
        m_resp->add_U32(CreateChannelUID(timer->Channel()));
      }
      m_resp->add_U32(timer->StartTime());
      m_resp->add_U32(timer->StopTime());
      m_resp->add_U32(timer->Day());
      m_resp->add_U32(timer->WeekDays());
      m_resp->add_String(m_toUTF8.Convert(timer->File()));
    }
    else
      m_resp->add_U32(VDR_RET_DATAUNKNOWN);
  }
  else
    m_resp->add_U32(VDR_RET_DATAUNKNOWN);

  m_resp->finalise();
  m_req->getClient()->GetSocket()->write(m_resp->getPtr(), m_resp->getLen());
  return true;
}

bool cCmdControl::processTIMER_GetList() /* OPCODE 82 */
{
  cTimer *timer;
  int numTimers = Timers.Count();

  m_resp->add_U32(numTimers);

  for (int i = 0; i < numTimers; i++)
  {
    timer = Timers.Get(i);
    if (!timer)
      continue;

    m_resp->add_U32(timer->Index()+1);
    m_resp->add_U32(timer->HasFlags(tfActive));
    m_resp->add_U32(timer->Recording());
    m_resp->add_U32(timer->Pending());
    m_resp->add_U32(timer->Priority());
    m_resp->add_U32(timer->Lifetime());
    m_resp->add_U32(timer->Channel()->Number());
    if(m_protocolVersion >= 2) {
      m_resp->add_U32(CreateChannelUID(timer->Channel()));
    }
    m_resp->add_U32(timer->StartTime());
    m_resp->add_U32(timer->StopTime());
    m_resp->add_U32(timer->Day());
    m_resp->add_U32(timer->WeekDays());
    m_resp->add_String(m_toUTF8.Convert(timer->File()));
  }

  m_resp->finalise();
  m_req->getClient()->GetSocket()->write(m_resp->getPtr(), m_resp->getLen());
  return true;
}

bool cCmdControl::processTIMER_Add() /* OPCODE 83 */
{
  uint32_t flags      = m_req->extract_U32() > 0 ? tfActive : tfNone;
  uint32_t priority   = m_req->extract_U32();
  uint32_t lifetime   = m_req->extract_U32();
  uint32_t number     = m_req->extract_U32();
  time_t startTime    = m_req->extract_U32();
  time_t stopTime     = m_req->extract_U32();
  time_t day          = m_req->extract_U32();
  uint32_t weekdays   = m_req->extract_U32();
  const char *file    = m_req->extract_String();
  const char *aux     = m_req->extract_String();

  struct tm tm_r;
  struct tm *time = localtime_r(&startTime, &tm_r);
  if (day <= 0)
    day = cTimer::SetTime(startTime, 0);
  int start = time->tm_hour * 100 + time->tm_min;
  time = localtime_r(&stopTime, &tm_r);
  int stop = time->tm_hour * 100 + time->tm_min;

  cString buffer = cString::sprintf("%u:%i:%s:%04d:%04d:%d:%d:%s:%s\n", flags, number, *cTimer::PrintDay(day, weekdays, true), start, stop, priority, lifetime, file, aux);

  delete[] file;
  delete[] aux;

  cTimer *timer = new cTimer;
  if (timer->Parse(buffer))
  {
    cTimer *t = Timers.GetTimer(timer);
    if (!t)
    {
      Timers.Add(timer);
      Timers.SetModified();
      isyslog("VNSI: Timer %s added", *timer->ToDescr());
      m_resp->add_U32(VDR_RET_OK);
      m_resp->finalise();
      m_req->getClient()->GetSocket()->write(m_resp->getPtr(), m_resp->getLen());
      return true;
    }
    else
    {
      esyslog("VNSI-Error: Timer already defined: %d %s", t->Index() + 1, *t->ToText());
      m_resp->add_U32(VDR_RET_DATALOCKED);
    }
  }
  else
  {
    esyslog("VNSI-Error: Error in timer settings");
    m_resp->add_U32(VDR_RET_DATAINVALID);
  }

  delete timer;

  m_resp->finalise();
  m_req->getClient()->GetSocket()->write(m_resp->getPtr(), m_resp->getLen());
  return true;
}

bool cCmdControl::processTIMER_Delete() /* OPCODE 84 */
{
  uint32_t number = m_req->extract_U32();
  bool     force  = m_req->extract_U32();

  if (number <= 0 || number > (uint32_t)Timers.Count())
  {
    esyslog("VNSI-Error: Unable to delete timer - invalid timer identifier");
    m_resp->add_U32(VDR_RET_DATAINVALID);
  }
  else
  {
    cTimer *timer = Timers.Get(number-1);
    if (timer)
    {
      if (!Timers.BeingEdited())
      {
        if (timer->Recording())
        {
          if (force)
          {
            timer->Skip();
            cRecordControls::Process(time(NULL));
          }
          else
          {
            esyslog("VNSI-Error: Timer \"%i\" is recording and can be deleted (use force=1 to stop it)", number);
            m_resp->add_U32(VDR_RET_RECRUNNING);
            m_resp->finalise();
            m_req->getClient()->GetSocket()->write(m_resp->getPtr(), m_resp->getLen());
            return true;
          }
        }
        isyslog("VNSI: Deleting timer %s", *timer->ToDescr());
        Timers.Del(timer);
        Timers.SetModified();
        m_resp->add_U32(VDR_RET_OK);
      }
      else
      {
        esyslog("VNSI-Error: Unable to delete timer - timers being edited at VDR");
        m_resp->add_U32(VDR_RET_DATALOCKED);
      }
    }
    else
    {
      esyslog("VNSI-Error: Unable to delete timer - invalid timer identifier");
      m_resp->add_U32(VDR_RET_DATAINVALID);
    }
  }
  m_resp->finalise();
  m_req->getClient()->GetSocket()->write(m_resp->getPtr(), m_resp->getLen());
  return true;
}

bool cCmdControl::processTIMER_Update() /* OPCODE 85 */
{
  int length      = m_req->getDataLength();
  uint32_t index  = m_req->extract_U32();
  bool active     = m_req->extract_U32();

  cTimer *timer = Timers.Get(index - 1);
  if (!timer)
  {
    esyslog("VNSI-Error: Timer \"%u\" not defined", index);
    m_resp->add_U32(VDR_RET_DATAUNKNOWN);
    m_resp->finalise();
    m_req->getClient()->GetSocket()->write(m_resp->getPtr(), m_resp->getLen());
    return true;
  }

  cTimer t = *timer;

  if (length == 8)
  {
    if (active)
      t.SetFlags(tfActive);
    else
      t.ClrFlags(tfActive);
  }
  else
  {
    uint32_t flags      = active ? tfActive : tfNone;
    uint32_t priority   = m_req->extract_U32();
    uint32_t lifetime   = m_req->extract_U32();
    uint32_t number     = m_req->extract_U32();
    time_t startTime    = m_req->extract_U32();
    time_t stopTime     = m_req->extract_U32();
    time_t day          = m_req->extract_U32();
    uint32_t weekdays   = m_req->extract_U32();
    const char *file    = m_req->extract_String();
    const char *aux     = m_req->extract_String();

    struct tm tm_r;
    struct tm *time = localtime_r(&startTime, &tm_r);
    if (day <= 0)
      day = cTimer::SetTime(startTime, 0);
    int start = time->tm_hour * 100 + time->tm_min;
    time = localtime_r(&stopTime, &tm_r);
    int stop = time->tm_hour * 100 + time->tm_min;

    cString buffer = cString::sprintf("%u:%i:%s:%04d:%04d:%d:%d:%s:%s\n", flags, number, *cTimer::PrintDay(day, weekdays, true), start, stop, priority, lifetime, file, aux);
    
    delete[] file;
    delete[] aux;

    if (!t.Parse(buffer))
    {
      esyslog("VNSI-Error: Error in timer settings");
      m_resp->add_U32(VDR_RET_DATAINVALID);
      m_resp->finalise();
      m_req->getClient()->GetSocket()->write(m_resp->getPtr(), m_resp->getLen());
      return true;
    }
  }

  *timer = t;
  Timers.SetModified();

  m_resp->add_U32(VDR_RET_OK);
  m_resp->finalise();
  m_req->getClient()->GetSocket()->write(m_resp->getPtr(), m_resp->getLen());
  return true;
}


/** OPCODE 100 - 119: VNSI network functions for recording access */

bool cCmdControl::processRECORDINGS_GetDiskSpace() /* OPCODE 100 */
{
  int FreeMB;
  int Percent = VideoDiskSpace(&FreeMB);
  int Total   = (FreeMB / (100 - Percent)) * 100;

  m_resp->add_U32(Total);
  m_resp->add_U32(FreeMB);
  m_resp->add_U32(Percent);

  m_resp->finalise();
  m_req->getClient()->GetSocket()->write(m_resp->getPtr(), m_resp->getLen());
  return true;
}

bool cCmdControl::processRECORDINGS_GetCount() /* OPCODE 101 */
{
  Recordings.Load();
  m_resp->add_U32(Recordings.Count());

  m_resp->finalise();
  m_req->getClient()->GetSocket()->write(m_resp->getPtr(), m_resp->getLen());
  return true;
}

bool cCmdControl::processRECORDINGS_GetList() /* OPCODE 102 */
{
  if(m_protocolVersion == 1) {
    m_resp->add_String(VideoDirectory);
  }

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
        recordingStart = recording->start;
      }
    }
    LOGCONSOLE("GRI: RC: recordingStart=%lu recordingDuration=%lu", recordingStart, recordingDuration);

    // recording_time
    m_resp->add_U32(recordingStart);

    // duration
    m_resp->add_U32(recordingDuration);

    // priority
    m_resp->add_U32(recording->priority);

    // lifetime
    m_resp->add_U32(recording->lifetime);

    // channel_name
    m_resp->add_String(recording->Info()->ChannelName() ? m_toUTF8.Convert(recording->Info()->ChannelName()) : "");

    char* fullname = strdup(recording->Name());
    char* recname = strrchr(fullname, '~');
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
    m_resp->add_String(m_toUTF8.Convert(recname));

    // subtitle
    if (!isempty(recording->Info()->ShortText()))
      m_resp->add_String(m_toUTF8.Convert(recording->Info()->ShortText()));
    else
      m_resp->add_String("");

    // description
    if (!isempty(recording->Info()->Description()))
      m_resp->add_String(m_toUTF8.Convert(recording->Info()->Description()));
    else
      m_resp->add_String("");

    // directory
    if(m_protocolVersion >= 2) {
      if(directory != NULL) {
        char* p = directory;
        while(*p != 0) {
          if(*p == '~') *p = '/';
          p++;
        }
        while(*directory == '/') directory++;
      }

      m_resp->add_String((isempty(directory)) ? "" : m_toUTF8.Convert(directory));
    }

    // filename / uid of recording
    if(m_protocolVersion >= 2) {
      uint32_t uid = cRecordingsCache::GetInstance().Register(recording);
      m_resp->add_U32(uid);
    }
    else {
      cString filename = recording->FileName();
      m_resp->add_String(filename);
    }

    free(fullname);
  }

  m_resp->finalise();
  m_req->getClient()->GetSocket()->write(m_resp->getPtr(), m_resp->getLen());
  return true;
}

bool cCmdControl::processRECORDINGS_Rename() /* OPCODE 103 */
{
  uint32_t    uid          = m_req->extract_U32();
  char*       newtitle     = m_req->extract_String();
  cRecording* recording    = cRecordingsCache::GetInstance().Lookup(uid);
  int         r            = VDR_RET_DATAINVALID;

  if(recording != NULL) {
    // get filename and remove last part (recording time)
    const char* filename_old = recording->FileName();
    char* sep = strrchr(filename_old, '/');
    if(sep != NULL) {
      *sep = 0;
    }

    // replace spaces in newtitle
    strreplace(newtitle, ' ', '_');
    char* filename_new = new char[512];
    strncpy(filename_new, filename_old, 512);
    sep = strrchr(filename_new, '/');
    if(sep != NULL) {
      sep++;
      *sep = 0;
    }
    strncat(filename_new, newtitle, 512);

    isyslog("renaming recording '%s' to '%s'", filename_old, filename_new);
    r = rename(filename_old, filename_new);
    Recordings.Update();
  }

  m_resp->add_U32(r);
  m_resp->finalise();
  m_req->getClient()->GetSocket()->write(m_resp->getPtr(), m_resp->getLen());

  return true;
}

bool cCmdControl::processRECORDINGS_Delete() /* OPCODE 104 */
{
  cString recName;
  cRecording* recording = NULL;

  if(m_protocolVersion >= 2) {
    uint32_t uid = m_req->extract_U32();
    recording = cRecordingsCache::GetInstance().Lookup(uid);
  }
  else {
    const char* temp = m_req->extract_String();
    recName = temp;
    recording = Recordings.GetByName(recName);
    delete[] temp;
  }


  if (recording)
  {
    LOGCONSOLE("deleting recording: %s", recording->Name());

    cRecordControl *rc = cRecordControls::GetRecordControl(recording->FileName());
    if (!rc)
    {
      if (recording->Delete())
      {
        // Copy svdrdeveldevelp's way of doing this, see if it works
        Recordings.DelByName(recording->FileName());
        isyslog("VNSI: Recording \"%s\" deleted", recording->FileName());
        m_resp->add_U32(VDR_RET_OK);
      }
      else
      {
        esyslog("VNSI-Error: Error while deleting recording!");
        m_resp->add_U32(VDR_RET_ERROR);
      }
    }
    else
    {
      esyslog("VNSI-Error: Recording \"%s\" is in use by timer %d", recording->Name(), rc->Timer()->Index() + 1);
      m_resp->add_U32(VDR_RET_DATALOCKED);
    }
  }
  else
  {
    esyslog("VNSI-Error: Error in recording name \"%s\"", (const char*)recName);
    m_resp->add_U32(VDR_RET_DATAUNKNOWN);
  }

  m_resp->finalise();
  m_req->getClient()->GetSocket()->write(m_resp->getPtr(), m_resp->getLen());

  return true;
}


/** OPCODE 120 - 139: VNSI network functions for epg access and manipulating */

bool cCmdControl::processEPG_GetForChannel() /* OPCODE 120 */
{
  uint32_t channelNumber  = m_req->extract_U32();
  uint32_t startTime      = m_req->extract_U32();
  uint32_t duration       = m_req->extract_U32();

  isyslog("get schedule called for channel %u", channelNumber);

  cChannel* channel = Channels.GetByNumber(channelNumber);
  if (!channel)
  {
    m_resp->add_U32(0);
    m_resp->finalise();
    m_req->getClient()->GetSocket()->write(m_resp->getPtr(), m_resp->getLen());

    LOGCONSOLE("written 0 because channel = NULL");
    return true;
  }

  cSchedulesLock MutexLock;
  const cSchedules *Schedules = cSchedules::Schedules(MutexLock);
  if (!Schedules)
  {
    m_resp->add_U32(0);
    m_resp->finalise();
    m_req->getClient()->GetSocket()->write(m_resp->getPtr(), m_resp->getLen());

    LOGCONSOLE("written 0 because Schedule!s! = NULL");
    return true;
  }

  const cSchedule *Schedule = Schedules->GetSchedule(channel->GetChannelID());
  if (!Schedule)
  {
    m_resp->add_U32(0);
    m_resp->finalise();
    m_req->getClient()->GetSocket()->write(m_resp->getPtr(), m_resp->getLen());

    LOGCONSOLE("written 0 because Schedule = NULL");
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

    m_resp->add_U32(thisEventID);
    m_resp->add_U32(thisEventTime);
    m_resp->add_U32(thisEventDuration);
    m_resp->add_U32(thisEventContent);
    m_resp->add_U32(thisEventRating);

    m_resp->add_String(m_toUTF8.Convert(thisEventTitle));
    m_resp->add_String(m_toUTF8.Convert(thisEventSubTitle));
    m_resp->add_String(m_toUTF8.Convert(thisEventDescription));

    atLeastOneEvent = true;
  }

  LOGCONSOLE("Got all event data");

  if (!atLeastOneEvent)
  {
    m_resp->add_U32(0);
    LOGCONSOLE("Written 0 because no data");
  }

  m_resp->finalise();
  m_req->getClient()->GetSocket()->write(m_resp->getPtr(), m_resp->getLen());

  LOGCONSOLE("written schedules packet");

  return true;
}


/** OPCODE 140 - 169: VNSI network functions for channel scanning */

bool cCmdControl::processSCAN_ScanSupported() /* OPCODE 140 */
{
  /** Note: Using "WirbelScanService-StopScan-v1.0" to detect
            a present service interface in wirbelscan plugin,
            it returns true if supported */
  cPlugin *p = cPluginManager::GetPlugin("wirbelscan");
  if (p && p->Service("WirbelScanService-StopScan-v1.0", NULL))
    m_resp->add_U32(VDR_RET_OK);
  else
    m_resp->add_U32(VDR_RET_NOTSUPPORTED);

  m_resp->finalise();
  m_req->getClient()->GetSocket()->write(m_resp->getPtr(), m_resp->getLen());
  return true;
}

bool cCmdControl::processSCAN_GetCountries() /* OPCODE 141 */
{
  if (!m_processSCAN_Response)
  {
    m_processSCAN_Response = m_resp;
    cPlugin *p = cPluginManager::GetPlugin("wirbelscan");
    if (p)
    {
      m_resp->add_U32(VDR_RET_OK);
      p->Service("WirbelScanService-GetCountries-v1.0", (void*) processSCAN_AddCountry);
    }
    else
    {
      m_resp->add_U32(VDR_RET_NOTSUPPORTED);
    }
    m_processSCAN_Response = NULL;
  }
  else
  {
    m_resp->add_U32(VDR_RET_DATALOCKED);
  }

  m_resp->finalise();
  m_req->getClient()->GetSocket()->write(m_resp->getPtr(), m_resp->getLen());
  return true;
}

bool cCmdControl::processSCAN_GetSatellites() /* OPCODE 142 */
{
  if (!m_processSCAN_Response)
  {
    m_processSCAN_Response = m_resp;
    cPlugin *p = cPluginManager::GetPlugin("wirbelscan");
    if (p)
    {
      m_resp->add_U32(VDR_RET_OK);
      p->Service("WirbelScanService-GetSatellites-v1.0", (void*) processSCAN_AddSatellite);
    }
    else
    {
      m_resp->add_U32(VDR_RET_NOTSUPPORTED);
    }
    m_processSCAN_Response = NULL;
  }
  else
  {
    m_resp->add_U32(VDR_RET_DATALOCKED);
  }

  m_resp->finalise();
  m_req->getClient()->GetSocket()->write(m_resp->getPtr(), m_resp->getLen());
  return true;
}

bool cCmdControl::processSCAN_Start() /* OPCODE 143 */
{
  WirbelScanService_DoScan_v1_0 svc;
  svc.type              = (scantype_t)m_req->extract_U32();
  svc.scan_tv           = (bool)m_req->extract_U8();
  svc.scan_radio        = (bool)m_req->extract_U8();
  svc.scan_fta          = (bool)m_req->extract_U8();
  svc.scan_scrambled    = (bool)m_req->extract_U8();
  svc.scan_hd           = (bool)m_req->extract_U8();
  svc.CountryIndex      = (int)m_req->extract_U32();
  svc.DVBC_Inversion    = (int)m_req->extract_U32();
  svc.DVBC_Symbolrate   = (int)m_req->extract_U32();
  svc.DVBC_QAM          = (int)m_req->extract_U32();
  svc.DVBT_Inversion    = (int)m_req->extract_U32();
  svc.SatIndex          = (int)m_req->extract_U32();
  svc.ATSC_Type         = (int)m_req->extract_U32();
  svc.SetPercentage     = processSCAN_SetPercentage;
  svc.SetSignalStrength = processSCAN_SetSignalStrength;
  svc.SetDeviceInfo     = processSCAN_SetDeviceInfo;
  svc.SetTransponder    = processSCAN_SetTransponder;
  svc.NewChannel        = processSCAN_NewChannel;
  svc.IsFinished        = processSCAN_IsFinished;
  svc.SetStatus         = processSCAN_SetStatus;
  m_processSCAN_Socket  = m_req->getClient()->GetSocket();

  cPlugin *p = cPluginManager::GetPlugin("wirbelscan");
  if (p)
  {
    if (p->Service("WirbelScanService-DoScan-v1.0", (void*) &svc))
      m_resp->add_U32(VDR_RET_OK);
    else
      m_resp->add_U32(VDR_RET_ERROR);
  }
  else
  {
    m_resp->add_U32(VDR_RET_NOTSUPPORTED);
  }

  m_resp->finalise();
  m_req->getClient()->GetSocket()->write(m_resp->getPtr(), m_resp->getLen());
  return true;
}

bool cCmdControl::processSCAN_Stop() /* OPCODE 144 */
{
  cPlugin *p = cPluginManager::GetPlugin("wirbelscan");
  if (p)
  {
    p->Service("WirbelScanService-StopScan-v1.0", NULL);
    m_resp->add_U32(VDR_RET_OK);
  }
  else
  {
    m_resp->add_U32(VDR_RET_NOTSUPPORTED);
  }
  m_resp->finalise();
  m_req->getClient()->GetSocket()->write(m_resp->getPtr(), m_resp->getLen());
  return true;
}

cResponsePacket *cCmdControl::m_processSCAN_Response = NULL;
cxSocket *cCmdControl::m_processSCAN_Socket = NULL;

void cCmdControl::processSCAN_AddCountry(int index, const char *isoName, const char *longName)
{
  m_processSCAN_Response->add_U32(index);
  m_processSCAN_Response->add_String(isoName);
  m_processSCAN_Response->add_String(longName);
}

void cCmdControl::processSCAN_AddSatellite(int index, const char *shortName, const char *longName)
{
  m_processSCAN_Response->add_U32(index);
  m_processSCAN_Response->add_String(shortName);
  m_processSCAN_Response->add_String(longName);
}

void cCmdControl::processSCAN_SetPercentage(int percent)
{
  cResponsePacket *resp = new cResponsePacket();
  if (!resp->initScan(VDR_SCANNER_PERCENTAGE))
  {
    delete resp;
    return;
  }
  resp->add_U32(percent);
  resp->finalise();
  m_processSCAN_Socket->write(resp->getPtr(), resp->getLen());
  delete resp;
}

void cCmdControl::processSCAN_SetSignalStrength(int strength, bool locked)
{
  cResponsePacket *resp = new cResponsePacket();
  if (!resp->initScan(VDR_SCANNER_SIGNAL))
  {
    delete resp;
    return;
  }
  strength *= 100;
  strength /= 0xFFFF;
  resp->add_U32(strength);
  resp->add_U32(locked);
  resp->finalise();
  m_processSCAN_Socket->write(resp->getPtr(), resp->getLen());
  delete resp;
}

void cCmdControl::processSCAN_SetDeviceInfo(const char *Info)
{
  cResponsePacket *resp = new cResponsePacket();
  if (!resp->initScan(VDR_SCANNER_DEVICE))
  {
    delete resp;
    return;
  }
  resp->add_String(Info);
  resp->finalise();
  m_processSCAN_Socket->write(resp->getPtr(), resp->getLen());
  delete resp;
}

void cCmdControl::processSCAN_SetTransponder(const char *Info)
{
  cResponsePacket *resp = new cResponsePacket();
  if (!resp->initScan(VDR_SCANNER_TRANSPONDER))
  {
    delete resp;
    return;
  }
  resp->add_String(Info);
  resp->finalise();
  m_processSCAN_Socket->write(resp->getPtr(), resp->getLen());
  delete resp;
}

void cCmdControl::processSCAN_NewChannel(const char *Name, bool isRadio, bool isEncrypted, bool isHD)
{
  cResponsePacket *resp = new cResponsePacket();
  if (!resp->initScan(VDR_SCANNER_NEWCHANNEL))
  {
    delete resp;
    return;
  }
  resp->add_U32(isRadio);
  resp->add_U32(isEncrypted);
  resp->add_U32(isHD);
  resp->add_String(Name);
  resp->finalise();
  m_processSCAN_Socket->write(resp->getPtr(), resp->getLen());
  delete resp;
}

void cCmdControl::processSCAN_IsFinished()
{
  cResponsePacket *resp = new cResponsePacket();
  if (!resp->initScan(VDR_SCANNER_FINISHED))
  {
    delete resp;
    return;
  }
  resp->finalise();
  m_processSCAN_Socket->write(resp->getPtr(), resp->getLen());
  m_processSCAN_Socket = NULL;
  delete resp;
}

void cCmdControl::processSCAN_SetStatus(int status)
{
  cResponsePacket *resp = new cResponsePacket();
  if (!resp->initScan(VDR_SCANNER_STATUS))
  {
    delete resp;
    return;
  }
  resp->add_U32(status);
  resp->finalise();
  m_processSCAN_Socket->write(resp->getPtr(), resp->getLen());
  delete resp;
}
