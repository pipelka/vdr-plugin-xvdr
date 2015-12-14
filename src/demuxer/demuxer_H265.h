/*
 *      vdr-plugin-xvdr - XVDR server plugin for VDR
 *
 *      Copyright (C) 2015 Alexander Pipelka
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

#ifndef XVDR_DEMUXER_H265_H
#define XVDR_DEMUXER_H265_H

#include "demuxer_H264.h"

class cParserH265 : public cParserH264
{
public:

  cParserH265(cTSDemuxer *demuxer);

  int ParsePayload(unsigned char *data, int length);

private:

  void skipScalingList(cBitStream& bs);

  void skipShortTermRefPicSets(cBitStream& bs);

  bool Parse_SPS(uint8_t *buf, int len, pixel_aspect_t& pixel_aspect, int& width, int& height);

};


#endif // XVDR_DEMUXER_H265_H
