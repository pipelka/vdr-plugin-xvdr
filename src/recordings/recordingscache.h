/*
 *      vdr-plugin-xvdr - XBMC server plugin for VDR
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

#ifndef XVDR_RECORDINGSCACHE_H
#define XVDR_RECORDINGSCACHE_H

#include <stdint.h>
#include <map>
#include <vdr/thread.h>
#include <vdr/tools.h>
#include <vdr/recording.h>
#include "db/storage.h"

class cRecordingsCache
{
protected:

  cRecordingsCache();

  virtual ~cRecordingsCache();

public:

  static cRecordingsCache& GetInstance();

  uint32_t Register(cRecording* recording);

  cRecording* Lookup(uint32_t uid);

  void SetPlayCount(uint32_t uid, int count);

  void SetLastPlayedPosition(uint32_t uid, uint64_t position);

  int GetPlayCount(uint32_t uid);

  uint64_t GetLastPlayedPosition(uint32_t uid);

  void SetPosterUrl(uint32_t uid, const char* url);

  void SetBackgroundUrl(uint32_t uid, const char* url);

  void SetMovieID(uint32_t uid, uint32_t id);

  cString GetPosterUrl(uint32_t uid);

  cString GetBackgroundUrl(uint32_t uid);

  void gc();

protected:

  void Update();

  void CreateDB();

private:

  XVDR::Storage& m_storage;
};


#endif // XVDR_RECORDINGSCACHE_H
