/*
 *      vdr-plugin-xvdr - XVDR server plugin for VDR
 *
 *      Copyright (C) 2011 Alexander Pipelka
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

#include <stdio.h>
#define __STDC_FORMAT_MACROS // Required for format specifiers
#include <inttypes.h>

#include "config/config.h"
#include "recordingscache.h"
#include "tools/hash.h"

cRecordingsCache::cRecordingsCache() : m_changed(false) {
  cMutexLock lock(&m_mutex);

  // initialize cache
  Update();
}

void cRecordingsCache::Update() {
  for (cRecording *recording = Recordings.First(); recording; recording = Recordings.Next(recording)) {
    RegisterNoLock(recording);
  }
}

cRecordingsCache::~cRecordingsCache() {
}

cRecordingsCache& cRecordingsCache::GetInstance() {
  static cRecordingsCache singleton;
  return singleton;
}

uint32_t cRecordingsCache::Register(cRecording* recording) {
  cMutexLock lock(&m_mutex);

  return RegisterNoLock(recording);
}

uint32_t cRecordingsCache::RegisterNoLock(cRecording* recording) {
  cString filename = recording->FileName();
  uint32_t uid = CreateStringHash(filename);

  m_recordings[uid].filename = filename;
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

  if(isempty(filename)) {
    DEBUGLOG("%s - empty filename for uid: %08x !", __FUNCTION__, uid);
    return NULL;
  }

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

void cRecordingsCache::SetPosterUrl(uint32_t uid, const char* url)
{
  cMutexLock lock(&m_mutex);

  if(m_recordings.find(uid) == m_recordings.end())
    return;

  DEBUGLOG("%s - Set Poster URL: %s", (const char*)m_recordings[uid].filename, url);
  m_recordings[uid].posterUrl = url;
}

void cRecordingsCache::SetBackgroundUrl(uint32_t uid, const char* url)
{
  cMutexLock lock(&m_mutex);

  if(m_recordings.find(uid) == m_recordings.end())
    return;

  DEBUGLOG("%s - Set Background URL: %s", (const char*)m_recordings[uid].filename, url);
  m_recordings[uid].backgroundUrl = url;
}

void cRecordingsCache::SetMovieID(uint32_t uid, const char* id)
{
  cMutexLock lock(&m_mutex);

  if(m_recordings.find(uid) == m_recordings.end())
    return;

  DEBUGLOG("%s - Set movie id: %s", (const char*)m_recordings[uid].filename, id);
  m_recordings[uid].movieId = id;
}

int cRecordingsCache::GetPlayCount(uint32_t uid)
{
  cMutexLock lock(&m_mutex);

  if(m_recordings.find(uid) == m_recordings.end())
    return 0;

  //DEBUGLOG("%s - Get Playcount: %i", (const char*)m_recordings[uid].filename, m_recordings[uid].playcount]);

  return m_recordings[uid].playcount;
}

const char* cRecordingsCache::GetPosterUrl(uint32_t uid)
{
  cMutexLock lock(&m_mutex);

  if(m_recordings.find(uid) == m_recordings.end())
    return 0;

  return (const char*)m_recordings[uid].posterUrl;
}

const char* cRecordingsCache::GetBackgroundUrl(uint32_t uid)
{
  cMutexLock lock(&m_mutex);

  if(m_recordings.find(uid) == m_recordings.end())
    return 0;

  return (const char*)m_recordings[uid].backgroundUrl;
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

  char* posterUrl = NULL;
  char* backgroundUrl = NULL;
  char* movieId = NULL;

  while(fscanf(f, "%08x = %llu, %i, %m[^,], %m[^,], %m", &uid, &pos, &count, &posterUrl, &backgroundUrl, &movieId) > 0)
  {
    m_recordings[uid].lastplayedposition = pos;
    m_recordings[uid].playcount = count;
    m_recordings[uid].posterUrl = posterUrl;
    m_recordings[uid].backgroundUrl = backgroundUrl;
    m_recordings[uid].movieId = movieId;

    free(posterUrl);
    free(backgroundUrl);
    free(movieId);

    uid = 0;
    pos = 0;
    count = 0;
    posterUrl = NULL;
    backgroundUrl = NULL;
    movieId = NULL;
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
    //if(i->second.lastplayedposition != 0 || i->second.playcount != 0)
      fprintf(
        f, "%08x = %llu, %i, %s, %s, %s\n",
        i->first,
        i->second.lastplayedposition,
        i->second.playcount,
        (const char*)i->second.posterUrl,
        (const char*)i->second.backgroundUrl,
        (const char*)i->second.movieId);
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

void cRecordingsCache::gc() {
  cMutexLock lock(&m_mutex);

  Update();

  std::map<uint32_t, struct RecEntry>::iterator i = m_recordings.begin();

  while(i != m_recordings.end()) {
    if(!isempty(i->second.filename) && Recordings.GetByName(i->second.filename) == NULL) {
      INFOLOG("removing outdated recording (%08x) '%s' from cache", i->first, (const char*)i->second.filename);
      std::map<uint32_t, struct RecEntry>::iterator n = i++;
      m_recordings.erase(n);
    }
    else {
      i++;
    }
  }
}
