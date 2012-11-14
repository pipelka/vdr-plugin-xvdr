/*
 *      vdr-plugin-xvdr - XVDR server plugin for VDR
 *
 *      Copyright (C) 2012 Alexander Pipelka
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
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#include "demuxer_PES.h"
#include "vdr/remux.h"

cParserPES::cParserPES(cTSDemuxer *demuxer, int buffersize) : cParser(demuxer, buffersize, 0), m_length(0) {
}

void cParserPES::Parse(unsigned char *data, int size, bool pusi) {

  // packet ready ?
  if(!m_startup) {
    int length = 0;
    uint8_t* buffer = Get(length);

    if(length >= m_length && buffer != NULL) {
      SendPayload(buffer, m_length);
      Clear();
    }
  }

  if(pusi) {
    int offset = ParsePESHeader(data, size);
    m_length = PesLength(data) - PesPayloadOffset(data);

    data += offset;
    size -= offset;
    m_startup = false;
  }

  // we start with the beginning of a packet
  if(!m_startup)
    Put(data, size);
}
