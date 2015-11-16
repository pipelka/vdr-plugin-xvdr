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

#ifndef XVDR_RECEIVER_H
#define XVDR_RECEIVER_H

#include <vdr/channels.h>
#include <vdr/device.h>
#include <vdr/receiver.h>
#include <vdr/thread.h>
#include <vdr/ringbuffer.h>

#include "demuxer/demuxer.h"
#include "xvdr/xvdrcommand.h"

#include <list>

class cChannel;
class cTSDemuxer;
class MsgPacket;
class cLivePatFilter;
class cLiveQueue;
class cXVDRClient;

class cLiveStreamer : public cThread
                    , public cRingBufferLinear
                    , public cReceiver
{
private:
  friend class cTSDemuxer;
  friend class cLivePatFilter;
  friend class cChannelCache;

  void Detach(void);
  bool Attach(void);
  cTSDemuxer *FindStreamDemuxer(int Pid);

  void reorderStreams(int lang, cStreamInfo::Type type);

  void sendStreamPacket(sStreamPacket *pkt);
  void sendStreamChange();
  void sendStatus(int status);
  void sendDetach();

  cDevice          *m_Device;                       /*!> The receiving device the channel depents to */
  cLivePatFilter   *m_PatFilter;                    /*!> Filter processor to get changed pid's */
  std::list<cTSDemuxer*> m_Demuxers;
  bool              m_startup;
  bool              m_requestStreamChange;
  uint32_t          m_scanTimeout;                  /*!> Channel scanning timeout (in seconds) */
  cTimeMs           m_last_tick;
  bool              m_SignalLost;
  cMutex            m_FilterMutex;
  cMutex            m_DeviceMutex;
  int               m_LanguageIndex;
  cStreamInfo::Type m_LangStreamType;
  cLiveQueue*       m_Queue;
  uint32_t          m_uid;
  bool              m_ready;
  uint32_t          m_protocolVersion;
  bool              m_waitforiframe;
  cXVDRClient*      m_parent;
  bool              m_rawPTS;

protected:
  void Action(void);

#if VDRVERSNUM < 20300
  void Receive(uchar *Data, int Length);
#else
  void Receive(const uchar *Data, int Length);
#endif

  void RequestStreamChange();

  int SwitchChannel(const cChannel *channel);

private:

  void TryChannelSwitch();

public:
  cLiveStreamer(cXVDRClient* parent, const cChannel *channel, int priority, bool rawPTS = false);
  virtual ~cLiveStreamer();

  bool IsReady();
  bool IsStarting() { return m_startup; }
  bool IsPaused();
  bool TimeShiftMode();

  void SetLanguage(int lang, cStreamInfo::Type streamtype = cStreamInfo::stAC3);
  void SetTimeout(uint32_t timeout);
  void SetProtocolVersion(uint32_t protocolVersion);
  void SetWaitForIFrame(bool waitforiframe);

  void Pause(bool on);
  void RequestPacket();
  void RequestSignalInfo();

  void ChannelChange(const cChannel* Channel);
};

#endif  // XVDR_RECEIVER_H
