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

#include <unistd.h>

#include "vdr/config.h"
#include "config/config.h"
#include "tools/hash.h"
#include "xvdr/xvdrchannels.h"

#include "livepatfilter.h"
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

void cLivePatFilter::GetLanguage(SI::PMT::Stream& stream, char *langs, uint8_t& type)
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
        SI::Loop::Iterator it;
        SI::ISO639LanguageDescriptor::Language first;
        type = 0;
        if (ld->languageLoop.getNext(first, it)) {
          type = first.getAudioType();
        }
        break;
      }
      default: ;
    }
    delete d;
  }
}

bool cLivePatFilter::GetStreamInfo(SI::PMT::Stream& stream, cStreamInfo& info)
{
  SI::Descriptor *d;

  info.m_pid = stream.getPid();

  if (!info.m_pid)
    return false;

  switch (stream.getStreamType())
  {
    case 0x01: // ISO/IEC 11172 Video
    case 0x02: // ISO/IEC 13818-2 Video
    case 0x80: // ATSC Video MPEG2 (ATSC DigiCipher QAM)
      DEBUGLOG("PMT scanner adding PID %d (%s)\n", stream.getPid(), psStreamTypes[stream.getStreamType()]);
      info.m_type = cStreamInfo::stMPEG2VIDEO;
      return true;

    case 0x03: // ISO/IEC 11172 Audio
    case 0x04: // ISO/IEC 13818-3 Audio
      info.m_type = cStreamInfo::stMPEG2AUDIO;
      GetLanguage(stream, info.m_language, info.m_audiotype);
      DEBUGLOG("PMT scanner adding PID %d (%s) (%s)\n", stream.getPid(), psStreamTypes[stream.getStreamType()], info.m_language);
      return true;

    case 0x0f: // ISO/IEC 13818-7 Audio with ADTS transport syntax
      info.m_type = cStreamInfo::stAAC;
      GetLanguage(stream, info.m_language, info.m_audiotype);
      DEBUGLOG("PMT scanner: adding PID %d (%s) %s (%s)\n", stream.getPid(), psStreamTypes[stream.getStreamType()], "AAC", info.m_language);
      return true;

    case 0x11: // ISO/IEC 14496-3 Audio with LATM transport syntax
      info.m_type = cStreamInfo::stLATM;
      GetLanguage(stream, info.m_language, info.m_audiotype);
      DEBUGLOG("PMT scanner: adding PID %d (%s) %s (%s)\n", stream.getPid(), psStreamTypes[stream.getStreamType()], "LATM", info.m_language);
      return true;

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
      DEBUGLOG("PMT scanner: Not adding PID %d (%s) (skipped)\n", stream.getPid(), psStreamTypes[stream.getStreamType()]);
      break;

    case 0x1b: // ISO/IEC 14496-10 Video (MPEG-4 part 10/AVC, aka H.264)
      DEBUGLOG("PMT scanner adding PID %d (%s)\n", stream.getPid(), psStreamTypes[stream.getStreamType()]);
      info.m_type = cStreamInfo::stH264;
      return true;

    case 0x05: // ISO/IEC 13818-1 private sections
    case 0x06: // ISO/IEC 13818-1 PES packets containing private data
      for (SI::Loop::Iterator it; (d = stream.streamDescriptors.getNext(it)); )
      {
        switch (d->getDescriptorTag())
        {
          case SI::AC3DescriptorTag:
            info.m_type = cStreamInfo::stAC3;
            GetLanguage(stream, info.m_language, info.m_audiotype);
            DEBUGLOG("PMT scanner: adding PID %d (%s) %s (%s)\n", stream.getPid(), psStreamTypes[stream.getStreamType()], "AC3", info.m_language);
            delete d;
            return true;

          case SI::EnhancedAC3DescriptorTag:
            info.m_type = cStreamInfo::stEAC3;
            GetLanguage(stream, info.m_language, info.m_audiotype);
            DEBUGLOG("PMT scanner: adding PID %d (%s) %s (%s)\n", stream.getPid(), psStreamTypes[stream.getStreamType()], "EAC3", info.m_language);
            delete d;
            return true;

          /*case SI::DTSDescriptorTag:
            info.m_type = cStreamInfo::stDTS;
            GetLanguage(stream, info.m_language, info.m_audiotype);
            DEBUGLOG("PMT scanner: adding PID %d (%s) %s (%s)\n", stream.getPid(), psStreamTypes[stream.getStreamType()], "DTS", info.m_language);
            delete d;
            return true;*/

          case SI::AACDescriptorTag:
            info.m_type = cStreamInfo::stAAC;
            GetLanguage(stream, info.m_language, info.m_audiotype);
            DEBUGLOG("PMT scanner: adding PID %d (%s) %s (%s)\n", stream.getPid(), psStreamTypes[stream.getStreamType()], "AAC", info.m_language);
            delete d;
            return true;

          case SI::TeletextDescriptorTag:
            info.m_type = cStreamInfo::stTELETEXT;
            DEBUGLOG("PMT scanner: adding PID %d (%s) %s\n", stream.getPid(), psStreamTypes[stream.getStreamType()], "Teletext");
            delete d;
            return true;

          case SI::SubtitlingDescriptorTag:
          {
            info.m_type = cStreamInfo::stDVBSUB;
            info.m_subtitlingtype = 0;
            info.m_compositionpageid = 0;
            info.m_ancillarypageid   = 0;
            SI::SubtitlingDescriptor *sd = (SI::SubtitlingDescriptor *)d;
            SI::SubtitlingDescriptor::Subtitling sub;
            char *s = info.m_language;
            int n = 0;
            for (SI::Loop::Iterator it; sd->subtitlingLoop.getNext(sub, it); )
            {
              if (sub.languageCode[0])
              {
                info.m_subtitlingtype = sub.getSubtitlingType();
                info.m_compositionpageid = sub.getCompositionPageId();
                info.m_ancillarypageid = sub.getAncillaryPageId();
                if (n > 0)
                  *s++ = '+';
                strn0cpy(s, I18nNormalizeLanguageCode(sub.languageCode), MAXLANGCODE1);
                s += strlen(s);
                if (n++ > 1)
                  break;
              }
              info.m_parsed = true;
            }
            delete d;
            DEBUGLOG("PMT scanner: adding PID %d (%s) %s\n", stream.getPid(), psStreamTypes[stream.getStreamType()], "DVBSUB");
            return true;

          }
          default:
            DEBUGLOG("PMT scanner: NOT adding PID %d (%s) %s (%i)\n", stream.getPid(), psStreamTypes[stream.getStreamType()], "UNKNOWN", d->getDescriptorTag());
            break;
        }
        delete d;
      }
      break;

    default:
#if VDRVERSNUM >= 10728
      // ATSC A/53 AUDIO
      if (stream.getStreamType() == 0x81 && Setup.StandardCompliance == STANDARD_ANSISCTE) {
        info.m_type = cStreamInfo::stAC3;
        GetLanguage(stream, info.m_language, info.m_audiotype);
        DEBUGLOG("PMT scanner: adding PID %d %s (%s)\n", stream.getPid(), "AC3", info.m_language);
        return true;
      }
      else
#endif
      /* This following section handles all the cases where the audio track
       * info is stored in PMT user info with stream id >= 0x81
       * we check the registration format identifier to see if it
       * holds "AC-3"
       */
      if (stream.getStreamType() >= 0x81) {
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
                DEBUGLOG("PMT scanner: Adding pid %d (type 0x%x) RegDesc len %d (%c%c%c%c)\n",
                            stream.getPid(), stream.getStreamType(), d->getLength(), rawdata[2], rawdata[3], rawdata[4], rawdata[5]);
                info.m_type = cStreamInfo::stAC3;
                delete d;
                return true;
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
      DEBUGLOG("PMT scanner: NOT adding PID %d (%s) %s\n", stream.getPid(), psStreamTypes[stream.getStreamType()<0x1c?stream.getStreamType():0], "UNKNOWN");
      break;
  }

  info.m_type = cStreamInfo::stNONE;
  return false;
}

void cLivePatFilter::Process(u_short Pid, u_char Tid, const u_char *Data, int Length)
{
  if (Pid == 0x00 && Tid == 0x00)
  {
    SI::PAT pat(Data, false);
    if (!pat.CheckCRCAndParse())
      return;

    SI::PAT::Association assoc;
    for (SI::Loop::Iterator it; pat.associationLoop.getNext(assoc, it); )
    {
      if (!assoc.isNITPid())
      {
    	XVDRChannels.Lock(false);
        const cChannel *Channel =  XVDRChannels.Get()->GetByServiceID(Source(), Transponder(), assoc.getServiceId());

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
            XVDRChannels.Unlock();
            return;
          }
        }

        XVDRChannels.Unlock();
      }
    }
  }
  else if (Pid == m_pmtPid && Tid == SI::TableIdPMT && Source() && Transponder())
  {
    SI::PMT pmt(Data, false);
    if (!pmt.CheckCRCAndParse() || pmt.getServiceId() != m_pmtSid)
      return;

    if (m_pmtVersion != -1)
    {
      if (m_pmtVersion != pmt.getVersionNumber())
      {
        cFilter::Del(m_pmtPid, 0x02);
        m_pmtPid = 0; // this triggers PAT scan
      }
      return;
    }
    m_pmtVersion = pmt.getVersionNumber();

    // get cached channel data
    if(m_ChannelCache.size() == 0)
      m_ChannelCache = cChannelCache::GetFromCache(CreateChannelUID(m_Channel));

    // get all streams and check if there are new (currently unknown) streams
    SI::PMT::Stream stream;
    cChannelCache cache;
    for (SI::Loop::Iterator it; pmt.streamLoop.getNext(stream, it); )
    {
      cStreamInfo info;
      if (GetStreamInfo(stream, info) && cache.size() < MAXRECEIVEPIDS) {
        info.SetContent();
        cache.AddStream(info);
      }
    }

    // no new streams found -> exit
    if (m_ChannelCache.ismetaof(cache))
      return;

    m_Streamer->m_FilterMutex.Lock();

    // create new stream demuxers
    m_Streamer->Detach();
    cache.CreateDemuxers(m_Streamer);

    m_Streamer->m_ready = false;
    INFOLOG("Currently unknown new streams found, requesting stream change");

    m_Streamer->RequestStreamChange();

    // write changed data back to the cache
    m_ChannelCache = cache;
    cChannelCache::AddToCache(CreateChannelUID(m_Channel), m_ChannelCache);

    // try to attach receiver
    int c = 0;
    for(; c < 3 && !m_Streamer->Attach(); c++) {
      INFOLOG("Unable to attach receiver, retrying ...");
      usleep(100 * 1000);
    }

    // unable to attach receiver
    if(c == 3) {
      ERRORLOG("failed to attach receiver, sending detach ...");
      m_Streamer->sendDetach();
    }

    m_Streamer->m_FilterMutex.Unlock();
  }
}
