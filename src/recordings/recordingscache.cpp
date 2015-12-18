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

#include <stdio.h>
#define __STDC_FORMAT_MACROS // Required for format specifiers
#include <inttypes.h>

#include "config/config.h"
#include "recordingscache.h"
#include "tools/hash.h"
#include <string>

cRecordingsCache::cRecordingsCache() : m_storage(XVDR::Storage::getInstance()) {
  // create db schema
  CreateDB();

  // initialize cache
  Update();
}

void cRecordingsCache::Update() {
  for (cRecording *recording = Recordings.First(); recording; recording = Recordings.Next(recording)) {
    Register(recording);
  }
}

cRecordingsCache::~cRecordingsCache() {
}

cRecordingsCache& cRecordingsCache::GetInstance() {
  static cRecordingsCache singleton;
  return singleton;
}

uint32_t cRecordingsCache::Register(cRecording* recording) {
  cString filename = recording->FileName();
  uint32_t uid = CreateStringHash(filename);

  // try to update existing record
  m_storage.Exec(
    "INSERT OR IGNORE INTO recordings(recid, filename) VALUES(%u, %Q);",
    uid,
    (const char*)filename);

  return uid;
}

cRecording* cRecordingsCache::Lookup(uint32_t uid) {
  DEBUGLOG("%s - lookup uid: %08x", __FUNCTION__, uid);

  sqlite3_stmt* s = m_storage.Query("SELECT filename FROM recordings WHERE recid=%u;", uid);

  if(s == NULL) {
    DEBUGLOG("%s - not found !", __FUNCTION__);
    return NULL;
  }

  cString filename;
  if(sqlite3_step(s) == SQLITE_ROW) {
    filename = (const char*)sqlite3_column_text(s, 0);
  }

  sqlite3_finalize(s);

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
  m_storage.Exec(
    "UPDATE recordings SET playcount=%i WHERE recid=%u;",
    count,
    uid);
}

void cRecordingsCache::SetLastPlayedPosition(uint32_t uid, uint64_t position)
{
  m_storage.Exec(
    "UPDATE recordings SET position=%llu WHERE recid=%u;",
    position,
    uid);
}

void cRecordingsCache::SetPosterUrl(uint32_t uid, const char* url)
{
  m_storage.Exec(
    "UPDATE recordings SET posterurl=%Q WHERE recid=%u;",
    url,
    uid);
}

void cRecordingsCache::SetBackgroundUrl(uint32_t uid, const char* url)
{
  m_storage.Exec(
    "UPDATE recordings SET backgroundurl=%Q WHERE recid=%u;",
    url,
    uid);
}

void cRecordingsCache::SetMovieID(uint32_t uid, uint32_t id)
{
  m_storage.Exec(
    "UPDATE recordings SET externalid=%u WHERE recid=%u;",
    id,
    uid);
}

int cRecordingsCache::GetPlayCount(uint32_t uid)
{
  sqlite3_stmt* s = m_storage.Query("SELECT playcount FROM recordings WHERE recid=%u;", uid);

  if(s == NULL) {
    return 0;
  }

  int count;
  if(sqlite3_step(s) == SQLITE_ROW) {
    count = sqlite3_column_int(s, 0);
  }

  sqlite3_finalize(s);
  return count;
}

cString cRecordingsCache::GetPosterUrl(uint32_t uid)
{
  cString url = "";
  sqlite3_stmt* s = m_storage.Query("SELECT posterurl FROM recordings WHERE recid=%u;", uid);

  if(s == NULL) {
    return url;
  }

  if(sqlite3_step(s) == SQLITE_ROW) {
    const char* u = (const char*)sqlite3_column_text(s, 0);
    INFOLOG("posterurl for %u: %s", uid, u);
    if(u != NULL) {
      url = u;
    }
  }

  sqlite3_finalize(s);
  return url;
}

cString cRecordingsCache::GetBackgroundUrl(uint32_t uid)
{
  cString url = "";
  sqlite3_stmt* s = m_storage.Query("SELECT backgroundurl FROM recordings WHERE recid=%u;", uid);

  if(s == NULL) {
    return url;
  }

  if(sqlite3_step(s) == SQLITE_ROW) {
    const char* u = (const char*)sqlite3_column_text(s, 0);
    if(u != NULL) {
      url = u;
    }
  }

  sqlite3_finalize(s);
  return url;
}

uint64_t cRecordingsCache::GetLastPlayedPosition(uint32_t uid)
{
  sqlite3_stmt* s = m_storage.Query("SELECT position FROM recordings WHERE recid=%u;", uid);

  if(s == NULL) {
    return 0;
  }

  uint64_t position;
  if(sqlite3_step(s) == SQLITE_ROW) {
    position = sqlite3_column_int64(s, 0);
  }

  sqlite3_finalize(s);
  return position;
}

void cRecordingsCache::gc() {
  Update();

  sqlite3_stmt* s = m_storage.Query("SELECT recid, filename FROM recordings;");

  if(s == NULL) {
    return;
  }

  // check all recordings in the cache

  while(sqlite3_step(s) == SQLITE_ROW) {
    uint32_t recid = (uint32_t)sqlite3_column_int(s, 0);
    const char* filename = (const char*)sqlite3_column_text(s, 1);

    // remove orphaned entry
    if(Recordings.GetByName(filename) == NULL) {
      INFOLOG("removing outdated recording '%s' from cache", filename);
      m_storage.Exec("DELETE FROM recordings WHERE recid=%u;", recid);
    }
  }

  sqlite3_finalize(s);
}

void cRecordingsCache::CreateDB() {
    std::string schema =
	"CREATE TABLE IF NOT EXISTS recordings (\n"
	"  recid INTEGER PRIMARY KEY,\n"
	"  filename TEXT NOT NULL,\n"
	"  position BIGINT DEFAULT 0,\n"
	"  playcount INTEGER DEFAULT 0,\n"
	"  posterurl TEXT,\n"
	"  backgroundurl TEXT,\n"
	"  externalid INTEGER\n"
	");\n"
	"CREATE INDEX IF NOT EXISTS recordings_externalid on recordings(externalid);\n"
	"CREATE UNIQUE INDEX IF NOT EXISTS recordings_filename on recordings(filename);\n";

    if(m_storage.Exec(schema) != SQLITE_OK) {
	ERRORLOG("Unable to create database schema for recordings");
    }
}
