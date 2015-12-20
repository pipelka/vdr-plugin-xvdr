#include "config/config.h"
#include "demuxerbundle.h"

#include <map>
#include <vdr/remux.h>

cDemuxerBundle::cDemuxerBundle(cTSDemuxer::Listener* listener) : m_listener(listener) {
}

cDemuxerBundle::~cDemuxerBundle() {
    clear();
}


void cDemuxerBundle::clear() {
  for (auto i = begin(); i != end(); i++) {
    if ((*i) != NULL) {
      DEBUGLOG("Deleting stream demuxer for pid=%i and type=%i", (*i)->GetPID(), (*i)->GetType());
      delete (*i);
    }
  }

  std::list<cTSDemuxer*>::clear();
}

cTSDemuxer* cDemuxerBundle::findDemuxer(int Pid) {
  for (auto i = begin(); i != end(); i++) {
    if ((*i) != NULL && (*i)->GetPID() == Pid) {
      return (*i);
    }
  }

  return NULL;
}

void cDemuxerBundle::reorderStreams(int lang, cStreamInfo::Type type) {
  std::map<uint32_t, cTSDemuxer*> weight;

  // compute weights
  int i = 0;
  for (auto idx = begin(); idx != end(); idx++, i++) {
    cTSDemuxer* stream = (*idx);
    if (stream == NULL)
      continue;

    // 32bit weight:
    // V0000000ASLTXXXXPPPPPPPPPPPPPPPP
    //
    // VIDEO (V):      0x80000000
    // AUDIO (A):      0x00800000
    // SUBTITLE (S):   0x00400000
    // LANGUAGE (L):   0x00200000
    // STREAMTYPE (T): 0x00100000 (only audio)
    // AUDIOTYPE (X):  0x000F0000 (only audio)
    // PID (P):        0x0000FFFF

#define VIDEO_MASK      0x80000000
#define AUDIO_MASK      0x00800000
#define SUBTITLE_MASK   0x00400000
#define LANGUAGE_MASK   0x00200000
#define STREAMTYPE_MASK 0x00100000
#define AUDIOTYPE_MASK  0x000F0000
#define PID_MASK        0x0000FFFF

    // last resort ordering, the PID
    uint32_t w = 0xFFFF - (stream->GetPID() & PID_MASK);

    // stream type weights
    switch(stream->GetContent()) {
      case cStreamInfo::scVIDEO:
        w |= VIDEO_MASK;
        break;

      case cStreamInfo::scAUDIO:
        w |= AUDIO_MASK;

        // weight of audio stream type
        w |= (stream->GetType() == type) ? STREAMTYPE_MASK : 0;

        // weight of audio type
        w |= ((4 - stream->GetAudioType()) << 16) & AUDIOTYPE_MASK;
        break;

      case cStreamInfo::scSUBTITLE:
        w |= SUBTITLE_MASK;
        break;

      default:
        break;
    }

    // weight of language
    int streamLangIndex = I18nLanguageIndex(stream->GetLanguage());
    w |= (streamLangIndex == lang) ? LANGUAGE_MASK : 0;

    // summed weight
    weight[w] = stream;
  }

  // reorder streams on weight
  int idx = 0;
  std::list<cTSDemuxer*>::clear();

  for(std::map<uint32_t, cTSDemuxer*>::reverse_iterator i = weight.rbegin(); i != weight.rend(); i++, idx++) {
    cTSDemuxer* stream = i->second;
    DEBUGLOG("Stream : Type %s / %s Weight: %08X", stream->TypeName(), stream->GetLanguage(), i->first);
    push_back(stream);
  }
}

bool cDemuxerBundle::isReady() {
  for(auto i = begin(); i != end(); i++) {
    if (!(*i)->IsParsed()) {
      DEBUGLOG("Stream with PID %i not parsed", (*i)->GetPID());
      return false;
    }
  }

  return true;
}

void cDemuxerBundle::updateFrom(cStreamBundle* bundle) {
  cStreamBundle old;

  // remove old demuxers
  for(auto i = begin(); i != end(); i++) {
    old.AddStream(*(*i));
    delete *i;
  }

  std::list<cTSDemuxer*>::clear();

  // create new stream demuxers
  for(auto i = bundle->begin(); i != bundle->end(); i++) {
    cStreamInfo& infonew = i->second;
    cStreamInfo& infoold = old[i->first];

    // reuse previous stream information
    if(infonew.GetPID() == infoold.GetPID() && infonew.GetType() == infoold.GetType()) {
      infonew = infoold;
    }

    cTSDemuxer* dmx = new cTSDemuxer(m_listener, infonew);
    if (dmx != NULL) {
      push_back(dmx);
      dmx->info();
    }
  }
}

bool cDemuxerBundle::processTsPacket(uint8_t* packet) {
  unsigned int ts_pid = TsPid(packet);
  cTSDemuxer* demuxer = findDemuxer(ts_pid);

  if(demuxer == NULL) {
    return false;
  }

  return demuxer->ProcessTSPacket(packet);
}

MsgPacket* cDemuxerBundle::createStreamChangePacket(int protocolVersion) {
  MsgPacket* resp = new MsgPacket(XVDR_STREAM_CHANGE, XVDR_CHANNEL_STREAM);

  for (auto idx = begin(); idx != end(); idx++) {
    cTSDemuxer* stream = (*idx);

    if (stream == NULL) {
      continue;
    }

    int streamid = stream->GetPID();
    resp->put_U32(streamid);

    switch(stream->GetContent())
    {
      case cStreamInfo::scAUDIO:
        resp->put_String(stream->TypeName());
        resp->put_String(stream->GetLanguage());
        if(protocolVersion >= 5) {
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
        if(protocolVersion >= 6) {
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

  return resp;
}
  
