/*
 *      vdr-plugin-xvdr - XVDR server plugin for VDR
 *
 *      Copyright (C) 2010 Alwin Esch (Team XBMC)
 *      Copyright (C) 2010-2013 Alexander Pipelka
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

/*
 * This code is taken from VOMP for VDR plugin.
 */

#include "recplayer.h"
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#define __STDC_FORMAT_MACROS // Required for format specifiers
#include <inttypes.h>

#include "config/config.h"

#ifndef O_NOATIME
#define O_NOATIME 0
#endif

cRecPlayer::cRecPlayer(cRecording* rec)
{
  m_file = -1;
  m_fileOpen = -1;
  m_rescanInterval = 2000; // 2000 ms rescan interval
  m_recordingFilename = strdup(rec->FileName());
  m_totalLength = 0;

  // FIXME find out max file path / name lengths
#if VDRVERSNUM < 10703
  m_pesrecording = true;
#else
  m_pesrecording = rec->IsPesRecording();
#endif

  scan();
  m_rescanTime.Set(0);
}

cRecPlayer::~cRecPlayer()
{
  cleanup();
  closeFile();
  free(m_recordingFilename);
}

void cRecPlayer::cleanup() {
  for(int i = 0; i != m_segments.Size(); i++) {
    delete m_segments[i];
  }
  m_segments.Clear();
}

void cRecPlayer::scan()
{
  struct stat s;
  uint64_t len = m_totalLength;
  m_totalLength = 0;

  cleanup();

  for(int i = 0; ; i++) {
    fileNameFromIndex(i);

    if(stat(m_fileName, &s) == -1) {
      break;
    }

    cSegment* segment = new cSegment();
    segment->start = m_totalLength;
    segment->end = segment->start + s.st_size;

    m_segments.Append(segment);

    m_totalLength += s.st_size;
  }

  if(len != m_totalLength) {
    INFOLOG("recording scan: %llu bytes", m_totalLength);
  }
}

void cRecPlayer::update()
{
  // do not rescan too often
  if(m_rescanTime.Elapsed() < m_rescanInterval)
    return;

  DEBUGLOG("%s", __FUNCTION__);
  m_rescanTime.Set(0);

  scan();
}

char* cRecPlayer::fileNameFromIndex(int index) {
  if (m_pesrecording)
    snprintf(m_fileName, sizeof(m_fileName), "%s/%03i.vdr", m_recordingFilename, index+1);
  else
    snprintf(m_fileName, sizeof(m_fileName), "%s/%05i.ts", m_recordingFilename, index+1);

  return m_fileName;
}

bool cRecPlayer::openFile(int index)
{
  if (index == m_fileOpen) return true;
  closeFile();

  fileNameFromIndex(index);
  INFOLOG("openFile called for index %i (%s)", index, m_fileName);

  // first try to open with NOATIME flag
  m_file = open(m_fileName, O_RDONLY | O_NOATIME);

  // fallback if FS doesn't support NOATIME
  if (m_file == -1) {
    m_file = open(m_fileName, O_RDONLY);
  }

  // failed to open file
  if (m_file == -1) {
    INFOLOG("file failed to open");
    m_fileOpen = -1;
    return false;
  }

  m_fileOpen = index;
  return true;
}

void cRecPlayer::closeFile()
{
  if(m_file == -1) {
    return;
  }

  INFOLOG("file closed");
  close(m_file);

  m_file = -1;
  m_fileOpen = -1;
}

uint64_t cRecPlayer::getLengthBytes()
{
  return m_totalLength;
}

int cRecPlayer::getBlock(unsigned char* buffer, uint64_t position, int amount)
{
  // dont let the block be larger than 256 kb
  if (amount > 256*1024)
    amount = 256*1024;

  if ((uint64_t)amount > m_totalLength)
    amount = m_totalLength;

  if (position >= m_totalLength)
    return 0;

  if ((position + amount) > m_totalLength)
    amount = m_totalLength - position;

  // work out what block "position" is in
  int segmentNumber = -1;
  for(int i = 0; i < m_segments.Size(); i++)
  {
    if ((position >= m_segments[i]->start) && (position < m_segments[i]->end)) {
      segmentNumber = i;
      break;
    }
  }

  // segment not found / invalid position
  if (segmentNumber == -1) return 0;

  // open file (if not already open)
  if (!openFile(segmentNumber)) return 0;

  // work out position in current file
  uint64_t filePosition = position - m_segments[segmentNumber]->start;

  // seek to position
  if(lseek(m_file, filePosition, SEEK_SET) == -1) {
    ERRORLOG("unable to seek to position: %llu", filePosition);
    return 0;
  }

  // try to read the block
  int bytes_read = read(m_file, buffer, amount);
  DEBUGLOG("read %i bytes from file %i at position %llu", bytes_read, segmentNumber, filePosition);

  if(bytes_read <= 0) {
    return 0;
  }

#ifndef __FreeBSD__
  // Tell linux not to bother keeping the data in the FS cache
  posix_fadvise(m_file, filePosition, bytes_read, POSIX_FADV_DONTNEED);
#endif

  // divide and conquer
  if(bytes_read < amount) {
    bytes_read += getBlock(&buffer[bytes_read], position + bytes_read, amount - bytes_read);
  }

  return bytes_read;
}
