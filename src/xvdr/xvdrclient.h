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

#ifndef XVDR_CLIENT_H
#define XVDR_CLIENT_H

#include <map>
#include <list>
#include <string>

#include <vdr/thread.h>
#include <vdr/tools.h>
#include <vdr/receiver.h>
#include <vdr/status.h>

#include "demuxer/streaminfo.h"
#include "scanner/wirbelscan.h"

class cChannel;
class cDevice;
class cLiveStreamer;
class MsgPacket;
class cRecPlayer;
class cCmdControl;

class cXVDRClient : public cThread
                  , public cStatus
{
private:

  unsigned int      m_Id;
  int               m_socket;
  bool              m_loggedIn;
  bool              m_StatusInterfaceEnabled;
  cLiveStreamer    *m_Streamer;
  bool              m_isStreaming;
  cRecPlayer       *m_RecPlayer;
  MsgPacket        *m_req;
  MsgPacket        *m_resp;
  cCharSetConv      m_toUTF8;
  uint32_t          m_protocolVersion;
  cMutex            m_msgLock;
  static cMutex     m_timerLock;
  static cMutex     m_switchLock;
  int               m_compressionLevel;
  int               m_LanguageIndex;
  cStreamInfo::Type m_LangStreamType;
  std::list<int>    m_caids;
  bool              m_wantfta;
  bool              m_filterlanguage;
  int               m_channelCount;
  int               m_timeout;
  cWirbelScan       m_scanner;

protected:

  bool processRequest();

  virtual void Action(void);

  virtual void TimerChange(const cTimer *Timer, eTimerChange Change);
  virtual void Recording(const cDevice *Device, const char *Name, const char *FileName, bool On);
  virtual void OsdStatusMessage(const char *Message);

public:

  cXVDRClient(int fd, unsigned int id);
  virtual ~cXVDRClient();

  void ChannelChange();
  void RecordingsChange();
  void TimerChange();

  unsigned int GetID() { return m_Id; }

protected:

  void SetLoggedIn(bool yesNo) { m_loggedIn = yesNo; }
  void SetStatusInterface(bool yesNo) { m_StatusInterfaceEnabled = yesNo; }
  int StartChannelStreaming(const cChannel *channel, uint32_t timeout, int32_t priority);
  void StopChannelStreaming();

private:

  typedef struct {
    bool automatic;
    bool radio;
    std::string name;
  } ChannelGroup;

  std::map<std::string, ChannelGroup> m_channelgroups[2];

  void PutTimer(cTimer* timer, MsgPacket* p);
  bool IsChannelWanted(cChannel* channel, bool radio = false);
  int  ChannelsCount();
  cString CreateLogoURL(cChannel* channel);
  cString CreateServiceReference(cChannel* channel);

  bool process_Login();
  bool process_GetTime();
  bool process_EnableStatusInterface();
  bool process_UpdateChannels();
  bool process_ChannelFilter();

  bool processChannelStream_Open();
  bool processChannelStream_Close();
  bool processChannelStream_Pause();
  bool processChannelStream_Request();
  bool processChannelStream_Signal();

  bool processRecStream_Open();
  bool processRecStream_Close();
  bool processRecStream_GetBlock();
  bool processRecStream_Update();
  bool processRecStream_PositionFromFrameNumber();
  bool processRecStream_FrameNumberFromPosition();
  bool processRecStream_GetIFrame();

  bool processCHANNELS_GroupsCount();
  bool processCHANNELS_ChannelsCount();
  bool processCHANNELS_GroupList();
  bool processCHANNELS_GetChannels();
  bool processCHANNELS_GetGroupMembers();

  void CreateChannelGroups(bool automatic);

  bool processTIMER_GetCount();
  bool processTIMER_Get();
  bool processTIMER_GetList();
  bool processTIMER_Add();
  bool processTIMER_Delete();
  bool processTIMER_Update();

  bool processRECORDINGS_GetDiskSpace();
  bool processRECORDINGS_GetCount();
  bool processRECORDINGS_GetList();
  bool processRECORDINGS_GetInfo();
  bool processRECORDINGS_Rename();
  bool processRECORDINGS_Delete();
  bool processRECORDINGS_Move();
  bool processRECORDINGS_SetPlayCount();
  bool processRECORDINGS_SetPosition();
  bool processRECORDINGS_GetPosition();

  bool processEPG_GetForChannel();

  bool processSCAN_ScanSupported();
  bool processSCAN_GetSetup();
  bool processSCAN_SetSetup();
  bool processSCAN_Start();
  bool processSCAN_Stop();

  void SendScannerStatus();
};

#endif // XVDR_CLIENT_H
