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

#ifndef XVDR_DEMUXER_MPEGVIDEO_H
#define XVDR_DEMUXER_MPEGVIDEO_H

#include "demuxer_PES.h"
#include <map>

class cParserMPEG2Video : public cParserPES
{
public:

  cParserMPEG2Video(cTSDemuxer *demuxer);

protected:

  void ParsePayload(unsigned char *data, int length);

  void SendPayload(unsigned char* payload, int length);

private:

  void ParseSequenceStart(unsigned char* data, int length);

  void ParsePicture(unsigned char* data, int length);

  int64_t m_pdiff;
};

#endif // XVDR_DEMUXER_MPEGVIDEO_H
