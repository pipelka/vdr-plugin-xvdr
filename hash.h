#include <stdint.h>
#include <vdr/channels.h>

class cChannel;

uint32_t CreateChannelUID(const cChannel* channel);
const cChannel* FindChannelByUID(uint32_t channelUID);

uint32_t CreateStringHash(const cString& string);
