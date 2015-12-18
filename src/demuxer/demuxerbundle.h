#ifndef XVDR_DEMUXERBUNDLE_H
#define XVDR_DEMUXERBUNDLE_H

#include "demuxer/demuxer.h"
#include "demuxer/streambundle.h"

#include <list>

class cDemuxerBundle : public std::list<cTSDemuxer*> {
public:

  cDemuxerBundle(cTSDemuxer::Listener* listener);
  
  virtual ~cDemuxerBundle();
  
  void clear();

  cTSDemuxer* findDemuxer(int pid);

  void reorderStreams(int lang, cStreamInfo::Type type);

  bool isReady();

  void updateFrom(cStreamBundle* bundle);
  
  bool processTsPacket(uint8_t* packet);

protected:
  
  cTSDemuxer::Listener* m_listener = NULL;

};

#endif // XVDR_DEMUXERBUNDLE_H
