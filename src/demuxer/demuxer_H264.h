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
 *  along with this program; if not, write to the Free Software
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#ifndef XVDR_DEMUXER_H264_H
#define XVDR_DEMUXER_H264_H

#include "demuxer_PES.h"
#include "vdr/tools.h"

class cParserH264 : public cParserPES
{
public:

  cParserH264(cTSDemuxer *demuxer);

  int ParsePayload(unsigned char *data, int length);

protected:

  typedef struct {
    int num;
    int den;
  } pixel_aspect_t;

  // pixel aspect ratios
  static const pixel_aspect_t m_aspect_ratios[17];

  uint8_t* ExtractNAL(uint8_t* packet, int length, int nal_offset, int& nal_len);

  int nalUnescape(uint8_t *dst, const uint8_t *src, int len);

  uint32_t read_golomb_ue(cBitStream* bs);

  int32_t read_golomb_se(cBitStream* bs);

  int m_scale;

  int m_rate;

private:

  bool Parse_SPS(uint8_t *buf, int len, pixel_aspect_t& pixel_aspect, int& width, int& height);

  void Parse_SLH(uint8_t *buf, int len);

};


#endif // XVDR_DEMUXER_H264_H
