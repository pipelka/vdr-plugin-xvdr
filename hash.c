#include <vdr/tools.h>
#include <vdr/channels.h>

#include "hash.h"

#define USE_CRC32
#define POLYNOMIAL 0x04c11db7

static uint32_t crctab[256];

static void crc32_init() {
  int i,j;
  uint32_t crc;

  for (i = 0; i < 256; i++) {
    crc = i << 24;
    for (j = 0; j < 8; j++) {
      if (crc & 0x80000000)
        crc = (crc << 1) ^ POLYNOMIAL;
      else
        crc = crc << 1;
    }
    crctab[i] = htonl(crc);
  }
}

static uint32_t crc32(const unsigned char *data, int len) {
  static bool need_init = true;
  uint32_t r;
  uint32_t* p = (uint32_t*)data;
  uint32_t* e = (unsigned int *)(data + len);

  if (len < 4) return 0;

  if(need_init) {
    crc32_init();
    need_init = false;
  }

  r = ~*p++;
  while(p < e) {
#if defined(LITTLE_ENDIAN)
    r = crctab[r & 0xff] ^ r >> 8;
    r = crctab[r & 0xff] ^ r >> 8;
    r = crctab[r & 0xff] ^ r >> 8;
    r = crctab[r & 0xff] ^ r >> 8;
    r ^= *p++;
#else
    r = crctab[r >> 24] ^ r << 8;
    r = crctab[r >> 24] ^ r << 8;
    r = crctab[r >> 24] ^ r << 8;
    r = crctab[r >> 24] ^ r << 8;
    r ^= *p++;
#endif
  }

  return (~r) & 0x7FFFFFFF; // channeluid is signed
}

#ifndef USE_CRC32
static uint8_t EncodeCharacter(char c) {
  static const char* alphabet = ".0123456789SCTEW";
  for(int i=0; i<16; i++) {
    if(alphabet[i] == c) return i;
  }
  return 0;
}

uint32_t CreateStringHash(const cString& string) {
  static uint32_t m = 246944297;
  uint32_t h = 0;

  const char* p = string;

  while(*p != 0) {
    uint8_t c = EncodeCharacter(*p++);
    h = ((h << 4) + c) % m;
  }

  return h;
}

#else
uint32_t CreateStringHash(const cString& string) {
  const char* p = string;
  int len = strlen(p);

  return crc32((const unsigned char*)p, len);
}
#endif

uint32_t CreateChannelUID(const cChannel* channel) {
  cString channelid = channel->GetChannelID().ToString();
  return CreateStringHash(channelid);
}

const cChannel* FindChannelByUID(uint32_t channelUID) {
  cChannel* result = NULL;

  Channels.Lock(false);

  // maybe we need to use a lookup table
  for (cChannel *channel = Channels.First(); channel; channel = Channels.Next(channel)) {
    cString channelid = channel->GetChannelID().ToString();
    if(channelUID == CreateStringHash(channelid)) {
      result = channel;
      break;
    }
  }

  Channels.Unlock();

  return result;
}
