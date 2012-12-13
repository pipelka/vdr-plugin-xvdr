/*
 *      vdr-plugin-xvdr - XVDR server plugin for VDR
 *
 *      Copyright (C) 20120 Alexander Pipelka
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

#include "config/config.h"
#include "bitstream.h"
#include "demuxer_H264.h"

// pixel aspect ratios
const struct cParserH264::pixel_aspect_t cParserH264::m_aspect_ratios[] = {
  {0, 1}, { 1,  1}, {12, 11}, {10, 11}, {16, 11}, { 40, 33}, {24, 11}, {20, 11}, {32, 11},
  {80, 33}, {18, 11}, {15, 11}, {64, 33}, {160, 99}, { 4,  3}, { 3,  2}, { 2,  1}
};

// H264 profiles
#define PROFILE_BASELINE  66
#define PROFILE_MAIN      77
#define PROFILE_EXTENDED  88
#define PROFILE_HP        100
#define PROFILE_HI10P     110
#define PROFILE_HI422     122
#define PROFILE_HI444     244
#define PROFILE_CAVLC444   44

// NAL SPS ID
#define NAL_SPS 0x07


cParserH264::cParserH264(cTSDemuxer *demuxer) : cParserPES(demuxer, 512 * 1024)
{
}

void cParserH264::ParsePayload(unsigned char* data, int length) {
  int o = 0;
  bool spsfound = false;

  // iterate through all NAL units (and look for SPS)
  while((o = FindStartCode(data, length, o, 0x00000001)) >= 0) {
    o += 4;
    if(o >= length)
      return;

    // NAL_SPS found ?
    if((data[o] & 0x1F) == NAL_SPS && length - o > 1) {
      o++;
      spsfound = true;
      break;
    }
  }

  if(!spsfound)
    return;

  int l = (length - o) > 512 ? 512 : (length - o);
  uint8_t nal_data[512];

  int nal_len = nalUnescape(nal_data, data + o, l);

  int width = 0;
  int height = 0;
  struct pixel_aspect_t pixelaspect = { 1, 1 };

  if (!Parse_SPS(nal_data, nal_len, pixelaspect, width, height))
    return;

  double PAR = (double)pixelaspect.num/(double)pixelaspect.den;
  double DAR = (PAR * width) / height;

  m_demuxer->SetVideoInformation(0,0, height, width, DAR, pixelaspect.num, pixelaspect.den);
}

int cParserH264::nalUnescape(uint8_t *dst, const uint8_t *src, int len)
{
  int s = 0, d = 0;

  while (s < len)
  {
    if (!src[s] && !src[s + 1])
    {
      // hit 00 00 xx
      dst[d] = dst[d + 1] = 0;
      s += 2;
      d += 2;
      if (src[s] == 3)
      {
        s++; // 00 00 03 xx --> 00 00 xx
        if (s >= len)
          return d;
      }
    }
    dst[d++] = src[s++];
  }

  return d;
}

bool cParserH264::Parse_SPS(uint8_t *buf, int len, struct pixel_aspect_t& pixelaspect, int& width, int& height)
{
  cBitstream bs(buf, len * 8);

  int profile_idc = bs.readBits(8); // profile idc

  // check for valid profile
  if( profile_idc != PROFILE_BASELINE &&
      profile_idc != PROFILE_MAIN &&
      profile_idc != PROFILE_EXTENDED &&
      profile_idc != PROFILE_HP &&
      profile_idc != PROFILE_HI10P &&
      profile_idc != PROFILE_HI422 &&
      profile_idc != PROFILE_HI444 &&
      profile_idc != PROFILE_CAVLC444)
  {
    ERRORLOG("H264: invalid profile idc: %i", profile_idc);
    return false;
  }

  bs.skipBits(8); // constraint set flag 0-4, 4 bits reserved

  bs.skipBits(8); // level idc
  bs.readGolombUE(); // sequence parameter set id

  // high profile ?
  if (profile_idc == PROFILE_HP ||
      profile_idc == PROFILE_HI10P ||
      profile_idc == PROFILE_HI422 ||
      profile_idc == PROFILE_HI444 ||
      profile_idc == PROFILE_CAVLC444)
  {
    if(bs.readGolombUE() == 3) // chroma_format_idc
      bs.skipBits(1); // residual_colour_transform_flag

    bs.readGolombUE(); // bit_depth_luma - 8
    bs.readGolombUE(); // bit_depth_chroma - 8
    bs.skipBits(1); // transform_bypass

    if (bs.readBits1()) // seq_scaling_matrix_present
    {
      for (int i = 0; i < 8; i++)
      {
        if (bs.readBits1()) // seq_scaling_list_present
        {
          int last = 8, next = 8, size = (i<6) ? 16 : 64;
          for (int j = 0; j < size; j++)
          {
            if (next)
              next = (last + bs.readGolombSE()) & 0xff;
            last = next ?: last;
          }
        }
      }
    }
  }

  bs.readGolombUE(); // log2_max_frame_num - 4
  int pic_order_cnt_type = bs.readGolombUE();

  if (pic_order_cnt_type == 0)
    bs.readGolombUE(); // log2_max_poc_lsb - 4
  else if (pic_order_cnt_type == 1)
  {
    bs.skipBits(1); // delta_pic_order_always_zero
    bs.readGolombSE(); // offset_for_non_ref_pic
    bs.readGolombSE(); // offset_for_top_to_bottom_field

    unsigned int tmp = bs.readGolombUE(); // num_ref_frames_in_pic_order_cnt_cycle
    for (unsigned int i = 0; i < tmp; i++)
      bs.readGolombSE(); // offset_for_ref_frame
  }
  else if(pic_order_cnt_type != 2)
  {
    ERRORLOG("pic_order_cnt_type = %i", pic_order_cnt_type);
    return false;
  }

  bs.readGolombUE(); // ref_frames
  bs.skipBits(1); // gaps_in_frame_num_allowed

  width = bs.readGolombUE() + 1;
  height = bs.readGolombUE() + 1;
  unsigned int frame_mbs_only = bs.readBits1();

  width  *= 16;
  height *= 16 * (2 - frame_mbs_only);

  if (!frame_mbs_only)
    bs.skipBits(1); // mb_adaptive_frame_field_flag

  bs.skipBits(1); // direct_8x8_inference_flag

  // frame_cropping_flag
  if (bs.readBits1())
  {
    uint32_t crop_left   = bs.readGolombUE();
    uint32_t crop_right  = bs.readGolombUE();
    uint32_t crop_top    = bs.readGolombUE();
    uint32_t crop_bottom = bs.readGolombUE();

    width -= 2*(crop_left + crop_right);

    if (frame_mbs_only)
      height -= 2*(crop_top + crop_bottom);
    else
      height -= 4*(crop_top + crop_bottom);
  }

  // VUI parameters
  pixelaspect.num = 0;
  if (bs.readBits1()) // vui_parameters_present flag
  {
    if (bs.readBits1()) // aspect_ratio_info_present
    {
      uint32_t aspect_ratio_idc = bs.readBits(8);

      // Extended_SAR
      if (aspect_ratio_idc == 255)
      {
        pixelaspect.num = bs.readBits(16); // sar width
        pixelaspect.den = bs.readBits(16); // sar height
      }
      else if (aspect_ratio_idc < sizeof(m_aspect_ratios)/sizeof(struct pixel_aspect_t))
          pixelaspect = m_aspect_ratios[aspect_ratio_idc];
    }
  }

  return true;
}
