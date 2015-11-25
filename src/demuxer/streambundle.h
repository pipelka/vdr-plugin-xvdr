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

#ifndef XVDR_STREAMBUNDLE_H
#define XVDR_STREAMBUNDLE_H

#include "demuxer/streaminfo.h"
#include "vdr/channels.h"
#include <map>

class cStreamBundle : public std::map<int, cStreamInfo> {
public:

  cStreamBundle();

  void AddStream(const cStreamInfo& s);

  bool operator ==(const cStreamBundle& c) const;

  bool ismetaof(const cStreamBundle& c) const;

  bool contains(const cStreamInfo& s) const;

  bool changed() const { return m_bChanged; }

  bool IsParsed();

  static cStreamBundle FromChannel(const cChannel* channel);

private:

  bool m_bChanged;

};

MsgPacket& operator<< (MsgPacket& lhs, const cStreamBundle& rhs);
MsgPacket& operator>> (MsgPacket& lhs, cStreamBundle& rhs);

#endif // XVDR_STREAMBUNDLE_H
