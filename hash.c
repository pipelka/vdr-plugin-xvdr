#include <vdr/tools.h>
#include "hash.h"

static uint8_t EncodeCharacter(char c) {
  static const char* alphabet = ".0123456789SCTEW";
  for(int i=0; i<16; i++) {
    if(alphabet[i] == c) return i;
  }
  return 0;
}

uint32_t CreateChannelUID(const cChannel* channel) {
  static uint32_t m = 246944297;
  uint32_t h = 0;

  cString channelid = channel->GetChannelID().ToString();
  const char* p = channelid;

  while(*p != 0) {
    uint8_t c = EncodeCharacter(*p++);
    h = ((h << 4) + c) % m;
  }

  return h;
}
