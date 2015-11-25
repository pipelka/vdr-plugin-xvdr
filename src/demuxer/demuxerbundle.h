#ifndef XVDR_DEMUXERBUNDLE_H
#define XVDR_DEMUXERBUNDLE_H

#include "demuxer/demuxer.h"
#include <list>

class cDemuxerBundle : public std::list<cTSDemuxer*> {
};

#endif // XVDR_DEMUXERBUNDLE_H
