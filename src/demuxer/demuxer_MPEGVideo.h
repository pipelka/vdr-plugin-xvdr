/*
 *      vdr-plugin-xvdr - XVDR server plugin for VDR
 *
 *      Copyright (C) 2010 Alwin Esch (Team XBMC)
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

#ifndef XVDR_DEMUXER_MPEGVIDEO_H
#define XVDR_DEMUXER_MPEGVIDEO_H

#include "parser.h"

class cBitstream;

// --- cParserMPEG2Video -------------------------------------------------

class cParserMPEG2Video : public cParser
{
private:
  uint8_t        *m_pictureBuffer;
  int             m_pictureBufferSize;
  int             m_pictureBufferPtr;
  int             m_FrameDuration;
  uint32_t        m_StartCond;
  uint32_t        m_StartCode;
  int             m_StartCodeOffset;
  sStreamPacket  *m_StreamPacket;
  int             m_vbvDelay;       /* -1 if CBR */
  int             m_vbvSize;        /* Video buffer size (in bytes) */
  bool            m_FoundFrame;

  bool Parse_MPEG2Video(size_t len, uint32_t next_startcode, int sc_offset);
  bool Parse_MPEG2Video_SeqStart(cBitstream *bs);
  bool Parse_MPEG2Video_PicStart(int *frametype, cBitstream *bs);

public:
  cParserMPEG2Video(cTSDemuxer *demuxer);
  virtual ~cParserMPEG2Video();

  virtual void Parse(unsigned char *data, int size, bool pusi);
};

#endif // XVDR_DEMUXER_MPEGVIDEO_H
