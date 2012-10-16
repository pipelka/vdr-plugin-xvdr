/*
 *      vdr-plugin-xvdr - XBMC server plugin for VDR
 *
 *      Copyright (C) 2011 Alexander Pipelka
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

#include <stdio.h>

#include "config/config.h"
#include "recordingscache.h"
#include "tools/hash.h"

cRecordingsCache::cRecordingsCache() : m_changed(false)
{
  // initialize cache
  Recordings.Load();
  for (cRecording *recording = Recordings.First(); recording; recording = Recordings.Next(recording))
    Register(recording);
}

cRecordingsCache::~cRecordingsCache() {
}

cRecordingsCache& cRecordingsCache::GetInstance() {
  static cRecordingsCache singleton;
  return singleton;
}

uint32_t cRecordingsCache::Register(cRecording* recording) {
  cMutexLock lock(&m_mutex);

  cString filename = recording->FileName();
  uint32_t uid = CreateStringHash(filename);

  if(m_recordings.find(uid) == m_recordings.end())
  {
    DEBUGLOG("%s - uid: %08x '%s'", __FUNCTION__, uid, (const char*)filename);
    m_recordings[uid].filename = filename;
  }

  return uid;
}

cRecording* cRecordingsCache::Lookup(uint32_t uid) {
  cMutexLock lock(&m_mutex);
  DEBUGLOG("%s - lookup uid: %08x", __FUNCTION__, uid);

  if(m_recordings.find(uid) == m_recordings.end()) {
    DEBUGLOG("%s - not found !", __FUNCTION__);
    return NULL;
  }

  cString filename = m_recordings[uid].filename;
  DEBUGLOG("%s - filename: %s", __FUNCTION__, (const char*)filename);

  cRecording* r = Recordings.GetByName(filename);
  DEBUGLOG("%s - recording %s", __FUNCTION__, (r == NULL) ? "not found !" : "found");

  return r;
}

void cRecordingsCache::SetPlayCount(uint32_t uid, int count)
{
  cMutexLock lock(&m_mutex);

  if(m_recordings.find(uid) == m_recordings.end())
    return;

  DEBUGLOG("%s - Set Playcount: %i", (const char*)m_recordings[uid].filename, count);
  m_recordings[uid].playcount = count;
  m_changed = true;
}

void cRecordingsCache::SetLastPlayedPosition(uint32_t uid, uint64_t position)
{
  cMutexLock lock(&m_mutex);

  if(m_recordings.find(uid) == m_recordings.end())
    return;

  DEBUGLOG("%s - Set Position: %llu", (const char*)m_recordings[uid].filename, position);
  m_recordings[uid].lastplayedposition = position;
}

int cRecordingsCache::GetPlayCount(uint32_t uid)
{
  cMutexLock lock(&m_mutex);

  if(m_recordings.find(uid) == m_recordings.end())
    return 0;

  //DEBUGLOG("%s - Get Playcount: %i", (const char*)m_recordings[uid].filename, m_recordings[uid].playcount]);

  return m_recordings[uid].playcount;
}

uint64_t cRecordingsCache::GetLastPlayedPosition(uint32_t uid)
{
  cMutexLock lock(&m_mutex);

  if(m_recordings.find(uid) == m_recordings.end())
    return 0;

  DEBUGLOG("%s - Get Position: %llu", (const char*)m_recordings[uid].filename, m_recordings[uid].lastplayedposition);
  return m_recordings[uid].lastplayedposition;
}

void cRecordingsCache::LoadResumeData()
{
  cMutexLock lock(&m_mutex);

  cString filename = AddDirectory(XVDRServerConfig.ConfigDirectory, RESUME_DATA_FILE);
  FILE* f = fopen((const char*)filename, "r");

  if(f == NULL)
  {
    ERRORLOG("unable to open resume data: %s", (const char*)filename);
    return;
  }

  uint32_t uid = 0;
  uint64_t pos = 0;
  int count = 0;

  while(fscanf(f, "%08x = %llu, %i", &uid, &pos, &count) != EOF)
  {
    // skip unknown entries
    if(m_recordings.find(uid) == m_recordings.end())
      continue;

    m_recordings[uid].lastplayedposition = pos;
    m_recordings[uid].playcount = count;

    uid = 0;
    pos = 0;
    count = 0;
  }

  fclose(f);
  return;
}

void cRecordingsCache::SaveResumeData()
{
  cMutexLock lock(&m_mutex);

  cString filename = AddDirectory(XVDRServerConfig.ConfigDirectory, RESUME_DATA_FILE);
  FILE* f = fopen((const char*)filename, "w");

  if(f == NULL)
  {
    ERRORLOG("unable to create resume data: %s", (const char*)filename);
    return;
  }

  std::map<uint32_t, struct RecEntry>::iterator i;
  for(i = m_recordings.begin(); i != m_recordings.end(); i++)
  {
    if(i->second.lastplayedposition != 0 || i->second.playcount != 0)
      fprintf(f, "%08x = %llu, %i\n", i->first, i->second.lastplayedposition, i->second.playcount);
  }

  fclose(f);
  return;
}

bool cRecordingsCache::Changed() {
  cMutexLock lock(&m_mutex);

  bool rc = m_changed;
  m_changed = false;

  return rc;
}

void cRecordingsCache::SetChanged() {
  cMutexLock lock(&m_mutex);
  m_changed = true;
}
