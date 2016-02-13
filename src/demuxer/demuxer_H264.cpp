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

#include "config/config.h"
#include "demuxer_H264.h"

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
#define NAL_SLH 0x01
#define NAL_SEI 0x06
#define NAL_SPS 0x07
#define NAL_PPS 0x08

const cParserH264::pixel_aspect_t cParserH264::m_aspect_ratios[17] = {
  {0, 1}, { 1,  1}, {12, 11}, {10, 11}, {16, 11}, { 40, 33}, {24, 11}, {20, 11}, {32, 11},
  {80, 33}, {18, 11}, {15, 11}, {64, 33}, {160, 99}, { 4,  3}, { 3,  2}, { 2,  1}
};

// golomb decoding
uint32_t cParserH264::read_golomb_ue(cBitStream* bs)
{
  int leadingZeroBits = -1;

  for (uint32_t b = 0; !b; ++leadingZeroBits)
    b = bs->GetBits(1);

  return ((1 << leadingZeroBits) - 1) + bs->GetBits(leadingZeroBits);
}

int32_t cParserH264::read_golomb_se(cBitStream* bs)
{
  int32_t v = read_golomb_ue(bs);
  if(v == 0)
    return 0;

  int32_t neg = !(v & 1);
  v = (v + 1) >> 1;

  return neg ? -v : v;
}


cParserH264::cParserH264(cTSDemuxer *demuxer) : cParserPES(demuxer, 512 * 1024)
{
  m_scale = 0;
  m_rate = 0;
}

uint8_t* cParserH264::ExtractNAL(uint8_t* packet, int length, int nal_offset, int& nal_len) {
  int e = FindStartCode(packet, length, nal_offset, 0x00000001);
  if (e == -1) {
    e = length;
  }

  int l = e - nal_offset;

  if(l <= 0) {
    return NULL;
  }

  uint8_t* nal_data = new uint8_t[l];
  nal_len = nalUnescape(nal_data, packet + nal_offset, l);

  if(nal_len + nal_offset > length) {
    ERRORLOG("nal overrun: nal len: %i, offset: %i, packet length: %i", nal_len, nal_offset, length);
  }

  return nal_data;
}

int cParserH264::ParsePayload(unsigned char* data, int length) {
  int o = 0;
  int sps_start = -1;
  int pps_start = -1;
  int nal_len = 0;

  if(length < 4) {
    return length;
  }

  // iterate through all NAL units
  while((o = FindStartCode(data, length, o, 0x00000001)) >= 0) {
    o += 4;
    if(o >= length)
      return length;

    uint8_t nal_type = data[o] & 0x1F;

    // NAL_SLH
    if(nal_type == NAL_SLH && length - o > 1) {
      o++;
      uint8_t* nal_data = ExtractNAL(data, length, o, nal_len);

      if(nal_data != NULL) {
        Parse_SLH(nal_data, nal_len);
        delete[] nal_data;
      }
    }

    // NAL_PPS
    else if(nal_type == NAL_PPS && length - o > 1) {
      o++;
      pps_start = o;
    }

    // NAL_SPS
    else if(nal_type == NAL_SPS && length - o > 1) {
      o++;
      sps_start = o;
    }

    // remove filler data
    else if(nal_type == 0x0C) {
      length = o - 4;
    }
  }

  // extract and register PPS data (decoder specific data)
  if(pps_start != -1) {
    uint8_t* pps_data = ExtractNAL(data, length, pps_start, nal_len);
    if(pps_data != NULL) {
      m_demuxer->SetVideoDecoderData(NULL, 0, pps_data, nal_len);
      delete[] pps_data;
    }
  }

  // exit if we do not have SPS data
  if(sps_start == -1)
    return length;

  // extract SPS
  uint8_t* nal_data = ExtractNAL(data, length, sps_start, nal_len);

  if(nal_data == NULL) {
    return length;
  }

  // register SPS data (decoder specific data)
  m_demuxer->SetVideoDecoderData(nal_data, nal_len, NULL, 0);

  int width = 0;
  int height = 0;
  pixel_aspect_t pixelaspect = { 1, 1 };

  bool rc = Parse_SPS(nal_data, nal_len, pixelaspect, width, height);
  delete[] nal_data;

  if(!rc)
    return length;

  double PAR = (double)pixelaspect.num/(double)pixelaspect.den;
  double DAR = (PAR * width) / height;

  m_demuxer->SetVideoInformation(m_scale, m_rate, height, width, DAR, pixelaspect.num, pixelaspect.den);
  return length;
}

int cParserH264::nalUnescape(uint8_t *dst, const uint8_t *src, int len)
{
  int s = 0, d = 0;

  while (s < len) {
    if(s >= 2 && s < len - 1) {
      // hit 00 00 03 ?
      if(src[s - 2] == 0 && src[s - 1] == 0 && src[s] == 3) {
        s++; // skip 03
      }
    }

    dst[d++] = src[s++];
  }

  return d;
}

void cParserH264::Parse_SLH(uint8_t *buf, int len) {
  cBitStream bs(buf, len*8);

  read_golomb_ue(&bs); // first_mb_in_slice
  int type = read_golomb_ue(&bs);;

  if(type > 4) {
    type -= 5;
  }

  switch(type) {
    case 0:
      m_frametype = cStreamInfo::ftPFRAME;
      break;
    case 1:
      m_frametype = cStreamInfo::ftBFRAME;
      break;
    case 2:
      m_frametype = cStreamInfo::ftIFRAME;
      break;
    default:
      m_frametype = cStreamInfo::ftUNKNOWN;
      break;
  }

  return;
}

bool cParserH264::Parse_SPS(uint8_t *buf, int len, pixel_aspect_t& pixelaspect, int& width, int& height)
{
  bool seq_scaling_matrix_present = false;
  cBitStream bs(buf, len * 8);

  int profile_idc = bs.GetBits(8); // profile idc

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

  bs.SkipBits(8); // constraint set flag 0-4, 4 bits reserved

  bs.SkipBits(8); // level idc
  read_golomb_ue(&bs); // sequence parameter set id

  // high profile ?
  if (profile_idc == PROFILE_HP ||
      profile_idc == PROFILE_HI10P ||
      profile_idc == PROFILE_HI422 ||
      profile_idc == PROFILE_HI444 ||
      profile_idc == PROFILE_CAVLC444)
  {
    int chroma_format_idc = read_golomb_ue(&bs);
    if(chroma_format_idc == 3) // chroma_format_idc
      bs.SkipBits(1); // residual_colour_transform_flag

    read_golomb_ue(&bs); // bit_depth_luma - 8
    read_golomb_ue(&bs); // bit_depth_chroma - 8
    bs.SkipBits(1); // transform_bypass

    seq_scaling_matrix_present = bs.GetBit();

    if (seq_scaling_matrix_present) // seq_scaling_matrix_present
    {
      for (int i = 0; i < 8; i++)
      {
        if (bs.GetBit()) // seq_scaling_list_present
        {
          int last = 8, next = 8, size = (i<6) ? 16 : 64;
          for (int j = 0; j < size; j++) {
            if (next) {
              next = (last + read_golomb_se(&bs)) & 0xff;
            }
            last = next ?: last;
          }
        }
      }
    }
  }

  read_golomb_ue(&bs); // log2_max_frame_num - 4
  int pic_order_cnt_type = read_golomb_ue(&bs);

  if (pic_order_cnt_type == 0)
    read_golomb_ue(&bs); // log2_max_poc_lsb - 4
  else if (pic_order_cnt_type == 1)
  {
    bs.SkipBits(1); // delta_pic_order_always_zero
    read_golomb_se(&bs); // offset_for_non_ref_pic
    read_golomb_se(&bs); // offset_for_top_to_bottom_field

    unsigned int tmp = read_golomb_ue(&bs); // num_ref_frames_in_pic_order_cnt_cycle
    for (unsigned int i = 0; i < tmp; i++)
      read_golomb_se(&bs); // offset_for_ref_frame
  }
  else if(pic_order_cnt_type != 2)
  {
    ERRORLOG("pic_order_cnt_type = %i", pic_order_cnt_type);
    return false;
  }

  read_golomb_ue(&bs); // ref_frames
  bs.SkipBits(1); // gaps_in_frame_num_allowed

  width = read_golomb_ue(&bs) + 1;
  height = read_golomb_ue(&bs) + 1;
  unsigned int frame_mbs_only = bs.GetBit();

  width  *= 16;
  height *= 16 * (2 - frame_mbs_only);

  if (!frame_mbs_only)
    bs.SkipBits(1); // mb_adaptive_frame_field_flag

  bs.SkipBits(1); // direct_8x8_inference_flag

  // frame_cropping_flag
  if (bs.GetBit())
  {
    uint32_t crop_left = read_golomb_ue(&bs);
    uint32_t crop_right = read_golomb_ue(&bs);
    uint32_t crop_top = read_golomb_ue(&bs);
    uint32_t crop_bottom = read_golomb_ue(&bs);

    width -= 2*(crop_left + crop_right);

    if (frame_mbs_only)
      height -= 2*(crop_top + crop_bottom);
    else
      height -= 4*(crop_top + crop_bottom);
  }

  // VUI parameters
  pixelaspect.num = 0;
  if (bs.GetBit()) // vui_parameters_present flag
  {
    if (bs.GetBit()) // aspect_ratio_info_present
    {
      uint32_t aspect_ratio_idc = bs.GetBits(8);

      // Extended_SAR
      if (aspect_ratio_idc == 255) {
        pixelaspect.num = bs.GetBits(16); // sar width
        pixelaspect.den = bs.GetBits(16); // sar height
      }
      else if (aspect_ratio_idc < sizeof(m_aspect_ratios)/sizeof(pixel_aspect_t))
          pixelaspect = m_aspect_ratios[aspect_ratio_idc];
    }
    // overscan info
    if(bs.GetBit()) {
      bs.SkipBits(1); // overscan appropriate flag
    }
    // video signal type present
    if(bs.GetBit()) {
      bs.SkipBits(3); // video format
      bs.SkipBits(1); // video full range flag
      // color description present
      if(bs.GetBit()) {
        bs.SkipBits(8); // color primaries
        bs.SkipBits(8); // transfer characteristics
        bs.SkipBits(8); // matrix coefficients
      }
    }
    // chroma loc info present
    if(bs.GetBit()) {
      read_golomb_ue(&bs); // type top field
      read_golomb_ue(&bs); // type bottom field
    }
    // timing info present
    if(bs.GetBit()) {
      // get timing

      uint32_t num_units_in_tick = bs.GetBits(32);
      uint32_t time_scale = bs.GetBits(32);

      // fixed frame rate flag
      if(bs.GetBit()) {
        num_units_in_tick *= 2;
        m_duration = (90000 * num_units_in_tick) / time_scale;
        m_rate = time_scale;
        m_scale = num_units_in_tick;
      }
    }
  }

  return true;
}
