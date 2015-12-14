#ifndef XVDR_DEMUXERBUNDLE_H
#define XVDR_DEMUXERBUNDLE_H

#include "demuxer/demuxer.h"
#include "demuxer/streambundle.h"

#include <list>

class cDemuxerBundle : public std::list<cTSDemuxer*> {
public:

  void clear();

  cTSDemuxer* findDemuxer(int pid);

  void reorderStreams(int lang, cStreamInfo::Type type);

  bool isReady();

  void updateFrom(cStreamBundle* bundle, cTSDemuxer::Listener* listener);

};

#endif // XVDR_DEMUXERBUNDLE_H
