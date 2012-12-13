/*
 *      vdr-plugin-xvdr - XBMC server plugin for VDR
 *
 *      Copyright (C) 2012 Alexander Pipelka
 *
 *      http://www.xbmc.org
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

#ifndef XVDR_PES_H
#define XVDR_PES_H

#include "vdr/remux.h"

// PES PIDs

#define PRIVATE_STREAM1   0xBD
#define PADDING_STREAM    0xBE
#define PRIVATE_STREAM2   0xBF
#define PRIVATE_STREAM3   0xFD
#define AUDIO_STREAM_S    0xC0  /* 1100 0000 */
#define AUDIO_STREAM_E    0xDF  /* 1101 1111 */
#define VIDEO_STREAM_S    0xE0  /* 1110 0000 */
#define VIDEO_STREAM_E    0xEF  /* 1110 1111 */

#define AUDIO_STREAM_MASK 0x1F  /* 0001 1111 */
#define VIDEO_STREAM_MASK 0x0F  /* 0000 1111 */
#define AUDIO_STREAM      0xC0  /* 1100 0000 */
#define VIDEO_STREAM      0xE0  /* 1110 0000 */

#define ECM_STREAM        0xF0
#define EMM_STREAM        0xF1
#define DSM_CC_STREAM     0xF2
#define ISO13522_STREAM   0xF3
#define PROG_STREAM_DIR   0xFF

// PES helper functions

inline bool PesIsHeader(const uchar *p)
{
  return !(p)[0] && !(p)[1] && (p)[2] == 1;
}

#if VDRVERSNUM < 10732 // VDR VERSION < 1.7.32

#define MAX33BIT  0x00000001FFFFFFFFLL // max. possible value with 33 bit

inline bool PesHasDts(const uchar *p)
{
  return (p[7] & 0x40) && p[8] >= 10;
}

inline int64_t PesGetDts(const uchar *p)
{
  return ((((int64_t)p[14]) & 0x0E) << 29) |
         (( (int64_t)p[15])         << 22) |
         ((((int64_t)p[16]) & 0xFE) << 14) |
         (( (int64_t)p[17])         <<  7) |
         ((((int64_t)p[18]) & 0xFE) >>  1);
}

inline int64_t PtsAdd(int64_t Pts1, int64_t Pts2) { return (Pts1 + Pts2) & MAX33BIT; }

#endif // VERSION 1.7.32

#endif // XVDR_PES_H

