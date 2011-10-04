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

#include "config.h"
#include "livepatfilter.h"
#include "livereceiver.h"
#include "livestreamer.h"

static const char * const psStreamTypes[] = {
        "UNKNOWN",
        "ISO/IEC 11172 Video",
        "ISO/IEC 13818-2 Video",
        "ISO/IEC 11172 Audio",
        "ISO/IEC 13818-3 Audio",
        "ISO/IEC 13818-1 Privete sections",
        "ISO/IEC 13818-1 Private PES data",
        "ISO/IEC 13512 MHEG",
        "ISO/IEC 13818-1 Annex A DSM CC",
        "0x09",
        "ISO/IEC 13818-6 Multiprotocol encapsulation",
        "ISO/IEC 13818-6 DSM-CC U-N Messages",
        "ISO/IEC 13818-6 Stream Descriptors",
        "ISO/IEC 13818-6 Sections (any type, including private data)",
        "ISO/IEC 13818-1 auxiliary",
        "ISO/IEC 13818-7 Audio with ADTS transport sytax",
        "ISO/IEC 14496-2 Visual (MPEG-4)",
        "ISO/IEC 14496-3 Audio with LATM transport syntax",
        "0x12", "0x13", "0x14", "0x15", "0x16", "0x17", "0x18", "0x19", "0x1a",
        "ISO/IEC 14496-10 Video (MPEG-4 part 10/AVC, aka H.264)",
        "",
};

cLivePatFilter::cLivePatFilter(cLiveStreamer *Streamer, const cChannel *Channel)
{
  DEBUGLOG("cStreamdevPatFilter(\"%s\")", Channel->Name());
  m_Channel     = Channel;
  m_Streamer    = Streamer;
  m_pmtPid      = 0;
  m_pmtSid      = 0;
  m_pmtVersion  = -1;
  Set(0x00, 0x00);  // PAT

}

void cLivePatFilter::GetLanguage(SI::PMT::Stream& stream, char *langs)
{
  SI::Descriptor *d;
  for (SI::Loop::Iterator it; (d = stream.streamDescriptors.getNext(it)); )
  {
    switch (d->getDescriptorTag())
    {
      case SI::ISO639LanguageDescriptorTag:
      {
        SI::ISO639LanguageDescriptor *ld = (SI::ISO639LanguageDescriptor *)d;
        strn0cpy(langs, I18nNormalizeLanguageCode(ld->languageCode), MAXLANGCODE1);
        break;
      }
      default: ;
    }
    delete d;
  }
}

int cLivePatFilter::GetPid(SI::PMT::Stream& stream, eStreamType *type, char *langs, int *subtitlingType, int *compositionPageId, int *ancillaryPageId)
{
  SI::Descriptor *d;
  *langs = 0;

  if (!stream.getPid())
    return 0;

  if(m_Channel->Tpid() == stream.getPid())
  {
    DEBUGLOG("cStreamdevPatFilter PMT scanner: adding PID %d %s\n", stream.getPid(), "Teletext");
    *type = stTELETEXT;
    return stream.getPid();
  }

  switch (stream.getStreamType())
  {
    case 0x01: // ISO/IEC 11172 Video
    case 0x02: // ISO/IEC 13818-2 Video
    case 0x80: // ATSC Video MPEG2 (ATSC DigiCipher QAM)
      DEBUGLOG("cStreamdevPatFilter PMT scanner adding PID %d (%s)\n", stream.getPid(), psStreamTypes[stream.getStreamType()]);
      *type = stMPEG2VIDEO;
      return stream.getPid();
    case 0x03: // ISO/IEC 11172 Audio
    case 0x04: // ISO/IEC 13818-3 Audio
      *type   = stMPEG2AUDIO;
      GetLanguage(stream, langs);
      DEBUGLOG("cStreamdevPatFilter PMT scanner adding PID %d (%s) (%s)\n", stream.getPid(), psStreamTypes[stream.getStreamType()], langs);
      return stream.getPid();
    case 0x0f: // ISO/IEC 13818-7 Audio with ADTS transport syntax
    case 0x11: // ISO/IEC 14496-3 Audio with LATM transport syntax
       *type = stAAC;
       GetLanguage(stream, langs);
       DEBUGLOG("cStreamdevPatFilter PMT scanner: adding PID %d (%s) %s (%s)\n", stream.getPid(), psStreamTypes[stream.getStreamType()], "AAC", langs);
      return stream.getPid();
#if 1
    case 0x07: // ISO/IEC 13512 MHEG
    case 0x08: // ISO/IEC 13818-1 Annex A  DSM CC
    case 0x0a: // ISO/IEC 13818-6 Multiprotocol encapsulation
    case 0x0b: // ISO/IEC 13818-6 DSM-CC U-N Messages
    case 0x0c: // ISO/IEC 13818-6 Stream Descriptors
    case 0x0d: // ISO/IEC 13818-6 Sections (any type, including private data)
    case 0x0e: // ISO/IEC 13818-1 auxiliary
#endif
    case 0x10: // ISO/IEC 14496-2 Visual (MPEG-4)
      DEBUGLOG("cStreamdevPatFilter PMT scanner: Not adding PID %d (%s) (skipped)\n", stream.getPid(), psStreamTypes[stream.getStreamType()]);
      break;
    case 0x1b: // ISO/IEC 14496-10 Video (MPEG-4 part 10/AVC, aka H.264)
      DEBUGLOG("cStreamdevPatFilter PMT scanner adding PID %d (%s)\n", stream.getPid(), psStreamTypes[stream.getStreamType()]);
      *type = stH264;
      return stream.getPid();
    case 0x05: // ISO/IEC 13818-1 private sections
    case 0x06: // ISO/IEC 13818-1 PES packets containing private data
      for (SI::Loop::Iterator it; (d = stream.streamDescriptors.getNext(it)); )
      {
        switch (d->getDescriptorTag())
        {
          case SI::AC3DescriptorTag:
            *type = stAC3;
            GetLanguage(stream, langs);
            DEBUGLOG("cStreamdevPatFilter PMT scanner: adding PID %d (%s) %s (%s)\n", stream.getPid(), psStreamTypes[stream.getStreamType()], "AC3", langs);
            delete d;
            return stream.getPid();
          case SI::EnhancedAC3DescriptorTag:
            *type = stEAC3;
            GetLanguage(stream, langs);
            DEBUGLOG("cStreamdevPatFilter PMT scanner: adding PID %d (%s) %s (%s)\n", stream.getPid(), psStreamTypes[stream.getStreamType()], "EAC3", langs);
            delete d;
            return stream.getPid();
          case SI::DTSDescriptorTag:
            *type = stDTS;
            GetLanguage(stream, langs);
            DEBUGLOG("cStreamdevPatFilter PMT scanner: adding PID %d (%s) %s (%s)\n", stream.getPid(), psStreamTypes[stream.getStreamType()], "DTS", langs);
            delete d;
            return stream.getPid();
          case SI::AACDescriptorTag:
            *type = stAAC;
            GetLanguage(stream, langs);
            DEBUGLOG("cStreamdevPatFilter PMT scanner: adding PID %d (%s) %s (%s)\n", stream.getPid(), psStreamTypes[stream.getStreamType()], "AAC", langs);
            delete d;
            return stream.getPid();
          case SI::TeletextDescriptorTag:
            *type = stTELETEXT;
            DEBUGLOG("cStreamdevPatFilter PMT scanner: adding PID %d (%s) %s\n", stream.getPid(), psStreamTypes[stream.getStreamType()], "Teletext");
            delete d;
            return stream.getPid();
          case SI::SubtitlingDescriptorTag:
          {
            *type               = stDVBSUB;
            *langs              = 0;
            *subtitlingType     = 0;
            *compositionPageId  = 0;
            *ancillaryPageId    = 0;
            SI::SubtitlingDescriptor *sd = (SI::SubtitlingDescriptor *)d;
            SI::SubtitlingDescriptor::Subtitling sub;
            char *s = langs;
            int n = 0;
            for (SI::Loop::Iterator it; sd->subtitlingLoop.getNext(sub, it); )
            {
              if (sub.languageCode[0])
              {
                *subtitlingType     = sub.getSubtitlingType();
                *compositionPageId  = sub.getCompositionPageId();
                *ancillaryPageId    = sub.getAncillaryPageId();
                if (n > 0)
                  *s++ = '+';
                strn0cpy(s, I18nNormalizeLanguageCode(sub.languageCode), MAXLANGCODE1);
                s += strlen(s);
                if (n++ > 1)
                  break;
              }
            }
            delete d;
            DEBUGLOG("cStreamdevPatFilter PMT scanner: adding PID %d (%s) %s\n", stream.getPid(), psStreamTypes[stream.getStreamType()], "DVBSUB");
            return stream.getPid();
          }
          default:
            DEBUGLOG("cStreamdevPatFilter PMT scanner: NOT adding PID %d (%s) %s (%i)\n", stream.getPid(), psStreamTypes[stream.getStreamType()], "UNKNOWN", d->getDescriptorTag());
            break;
        }
        delete d;
      }
      break;
    default:
      /* This following section handles all the cases where the audio track
       * info is stored in PMT user info with stream id >= 0x81
       * we check the registration format identifier to see if it
       * holds "AC-3"
       */
      if (stream.getStreamType() >= 0x81)
      {
        bool found = false;
        for (SI::Loop::Iterator it; (d = stream.streamDescriptors.getNext(it)); )
        {
          switch (d->getDescriptorTag())
          {
            case SI::RegistrationDescriptorTag:
            /* unfortunately libsi does not implement RegistrationDescriptor */
            if (d->getLength() >= 4)
            {
              found = true;
              SI::CharArray rawdata = d->getData();
              if (/*rawdata[0] == 5 && rawdata[1] >= 4 && */
                  rawdata[2] == 'A' && rawdata[3] == 'C' &&
                  rawdata[4] == '-' && rawdata[5] == '3')
              {
                DEBUGLOG("cStreamdevPatFilter PMT scanner: Adding pid %d (type 0x%x) RegDesc len %d (%c%c%c%c)\n",
                            stream.getPid(), stream.getStreamType(), d->getLength(), rawdata[2], rawdata[3], rawdata[4], rawdata[5]);
                *type = stAC3;
                delete d;
                return stream.getPid();
              }
            }
            break;
            default:
            break;
          }
          delete d;
        }
        if (!found)
        {
          DEBUGLOG("NOT adding PID %d (type 0x%x) RegDesc not found -> UNKNOWN\n", stream.getPid(), stream.getStreamType());
        }
      }
      DEBUGLOG("cStreamdevPatFilter PMT scanner: NOT adding PID %d (%s) %s\n", stream.getPid(), psStreamTypes[stream.getStreamType()<0x1c?stream.getStreamType():0], "UNKNOWN");
      break;
  }
  *type = stNone;
  return 0;
}

void cLivePatFilter::Process(u_short Pid, u_char Tid, const u_char *Data, int Length)
{
  if (Pid == 0x00)
  {
    if (Tid == 0x00)
    {
      SI::PAT pat(Data, false);
      if (!pat.CheckCRCAndParse())
        return;
      SI::PAT::Association assoc;
      for (SI::Loop::Iterator it; pat.associationLoop.getNext(assoc, it); )
      {
        if (!assoc.isNITPid())
        {
          const cChannel *Channel =  Channels.GetByServiceID(Source(), Transponder(), assoc.getServiceId());
          if (Channel && (Channel == m_Channel))
          {
            int prevPmtPid = m_pmtPid;
            if (0 != (m_pmtPid = assoc.getPid()))
            {
              m_pmtSid = assoc.getServiceId();
              if (m_pmtPid != prevPmtPid)
              {
                Add(m_pmtPid, 0x02);
                m_pmtVersion = -1;
              }
              return;
            }
          }
        }
      }
    }
  }
  else if (Pid == m_pmtPid && Tid == SI::TableIdPMT && Source() && Transponder())
  {
    SI::PMT pmt(Data, false);
    if (!pmt.CheckCRCAndParse())
      return;
    if (pmt.getServiceId() != m_pmtSid)
      return; // skip broken PMT records
    if (m_pmtVersion != -1)
    {
      if (m_pmtVersion != pmt.getVersionNumber())
      {
//        printf("cStreamdevPatFilter: PMT version changed, detaching all pids\n");
        cFilter::Del(m_pmtPid, 0x02);
        m_pmtPid = 0; // this triggers PAT scan
      }
      return;
    }
    m_pmtVersion = pmt.getVersionNumber();

    SI::PMT::Stream stream;
    int         pids[MAXRECEIVEPIDS + 1];
    eStreamType types[MAXRECEIVEPIDS + 1];
    char        langs[MAXRECEIVEPIDS + 1][MAXLANGCODE2];
    int         subtitlingType[MAXRECEIVEPIDS + 1];
    int         compositionPageId[MAXRECEIVEPIDS + 1];
    int         ancillaryPageId[MAXRECEIVEPIDS + 1];
    int         streams = 0;
    for (SI::Loop::Iterator it; pmt.streamLoop.getNext(stream, it); )
    {
      eStreamType type;
      int pid = GetPid(stream, &type, langs[streams], &subtitlingType[streams], &compositionPageId[streams], &ancillaryPageId[streams]);
      if (0 != pid && streams < MAXRECEIVEPIDS)
      {
        pids[streams]   = pid;
        types[streams]  = type;
        streams++;
      }
    }
    pids[streams] = 0;

    int newstreams = 0;
    for (int i = 0; i < streams; i++)
    {
      if (m_Streamer->HaveStreamDemuxer(pids[i], types[i]) == -1)
        newstreams++;
    }

    m_Streamer->m_FilterMutex.Lock();
    if (newstreams > 0)
    {
      if (m_Streamer->m_Receiver)
      {
        DEBUGLOG("Detaching Live Receiver");
        m_Streamer->m_Device->Detach(m_Streamer->m_Receiver);
        DELETENULL(m_Streamer->m_Receiver);
      }

      for (int idx = 0; idx < MAXRECEIVEPIDS; ++idx)
      {
        if (m_Streamer->m_Streams[idx])
        {
          DELETENULL(m_Streamer->m_Streams[idx]);
          m_Streamer->m_Pids[idx] = 0;
        }
      }
      m_Streamer->m_NumStreams  = 0;
      m_Streamer->m_streamReady = m_Streamer->m_IsAudioOnly;
      m_Streamer->m_IFrameSeen  = false;

      for (int i = 0; i < streams; i++)
      {
        switch (types[i])
        {
          case stMPEG2AUDIO:
          {
            m_Streamer->m_Streams[m_Streamer->m_NumStreams] = new cTSDemuxer(m_Streamer, m_Streamer->m_NumStreams, stMPEG2AUDIO, pids[i]);
            m_Streamer->m_Streams[m_Streamer->m_NumStreams]->SetLanguage(langs[i]);
            m_Streamer->m_Pids[m_Streamer->m_NumStreams] = pids[i];
            m_Streamer->m_NumStreams++;
            break;
          }
          case stMPEG2VIDEO:
          {
            m_Streamer->m_Streams[m_Streamer->m_NumStreams] = new cTSDemuxer(m_Streamer, m_Streamer->m_NumStreams, stMPEG2VIDEO, pids[i]);
            m_Streamer->m_Pids[m_Streamer->m_NumStreams] = pids[i];
            m_Streamer->m_NumStreams++;
            break;
          }
          case stH264:
          {
            m_Streamer->m_Streams[m_Streamer->m_NumStreams] = new cTSDemuxer(m_Streamer, m_Streamer->m_NumStreams, stH264, pids[i]);
            m_Streamer->m_Pids[m_Streamer->m_NumStreams] = pids[i];
            m_Streamer->m_NumStreams++;
            break;
          }
          case stAC3:
          {
            m_Streamer->m_Streams[m_Streamer->m_NumStreams] = new cTSDemuxer(m_Streamer, m_Streamer->m_NumStreams, stAC3, pids[i]);
            m_Streamer->m_Streams[m_Streamer->m_NumStreams]->SetLanguage(langs[i]);
            m_Streamer->m_Pids[m_Streamer->m_NumStreams] = pids[i];
            m_Streamer->m_NumStreams++;
            break;
          }
          case stEAC3:
          {
            m_Streamer->m_Streams[m_Streamer->m_NumStreams] = new cTSDemuxer(m_Streamer, m_Streamer->m_NumStreams, stEAC3, pids[i]);
            m_Streamer->m_Streams[m_Streamer->m_NumStreams]->SetLanguage(langs[i]);
            m_Streamer->m_Pids[m_Streamer->m_NumStreams] = pids[i];
            m_Streamer->m_NumStreams++;
            break;
          }
          case stDTS:
          {
            m_Streamer->m_Streams[m_Streamer->m_NumStreams] = new cTSDemuxer(m_Streamer, m_Streamer->m_NumStreams, stDTS, pids[i]);
            m_Streamer->m_Streams[m_Streamer->m_NumStreams]->SetLanguage(langs[i]);
            m_Streamer->m_Pids[m_Streamer->m_NumStreams] = pids[i];
            m_Streamer->m_NumStreams++;
            break;
          }
          case stAAC:
          {
            m_Streamer->m_Streams[m_Streamer->m_NumStreams] = new cTSDemuxer(m_Streamer, m_Streamer->m_NumStreams, stAAC, pids[i]);
            m_Streamer->m_Streams[m_Streamer->m_NumStreams]->SetLanguage(langs[i]);
            m_Streamer->m_Pids[m_Streamer->m_NumStreams] = pids[i];
            m_Streamer->m_NumStreams++;
            break;
          }
          case stDVBSUB:
          {
            m_Streamer->m_Streams[m_Streamer->m_NumStreams] = new cTSDemuxer(m_Streamer, m_Streamer->m_NumStreams, stDVBSUB, pids[i]);
            m_Streamer->m_Streams[m_Streamer->m_NumStreams]->SetLanguage(langs[i]);
            m_Streamer->m_Streams[m_Streamer->m_NumStreams]->SetSubtitlingDescriptor(subtitlingType[i], compositionPageId[i], ancillaryPageId[i]);
            m_Streamer->m_Pids[m_Streamer->m_NumStreams] = pids[i];
            m_Streamer->m_NumStreams++;
            break;
          }
          case stTELETEXT:
          {
            m_Streamer->m_Streams[m_Streamer->m_NumStreams] = new cTSDemuxer(m_Streamer, m_Streamer->m_NumStreams, stTELETEXT, pids[i]);
            m_Streamer->m_Pids[m_Streamer->m_NumStreams] = pids[i];
            m_Streamer->m_NumStreams++;
            break;
          }
          default:
            break;
        }
      }

      m_Streamer->m_Receiver  = new cLiveReceiver(m_Streamer, m_Channel->GetChannelID(), m_Streamer->m_Priority, m_Streamer->m_Pids);
      m_Streamer->m_Device->AttachReceiver(m_Streamer->m_Receiver);
      INFOLOG("Currently unknown new streams found, receiver and demuxers reinited\n");
      m_Streamer->RequestStreamChange();
    }
    m_Streamer->m_FilterMutex.Unlock();
  }
}
