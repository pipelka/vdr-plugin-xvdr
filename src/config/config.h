/*
 *      vdr-plugin-xvdr - XBMC server plugin for VDR
 *
 *      Copyright (C) 2010 Alwin Esch (Team XBMC)
 *      Copyright (C) 2010, 2011 Alexander Pipelka
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
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#ifndef XVDR_CONFIG_H
#define XVDR_CONFIG_H

#include <string.h>
#include <stdint.h>

#include <vdr/config.h>

// log output configuration

#ifdef CONSOLEDEBUG
#define DEBUGLOG(x...) printf("XVDR: "x)
#elif defined  DEBUG
#define DEBUGLOG(x...) dsyslog("XVDR: "x)
#else
#define DEBUGLOG(x...)
#endif

#ifdef CONSOLEDEBUG
#define INFOLOG(x...) printf("XVDR: "x)
#define ERRORLOG(x...) printf("XVDR-Error: "x)
#else
#define INFOLOG(x...) isyslog("XVDR: "x)
#define ERRORLOG(x...) esyslog("XVDR-Error: "x)
#endif

// default settings

#define ALLOWED_HOSTS_FILE  "allowed_hosts.conf"
#define FRONTEND_DEVICE     "/dev/dvb/adapter%d/frontend%d"
#define GENERAL_CONFIG_FILE "xvdr.conf"
#define RESUME_DATA_FILE    "resume.data"

#define LISTEN_PORT       34891
#define LISTEN_PORT_S    "34891"
#define DISCOVERY_PORT    34891

// backward compatibility

#if APIVERSNUM < 10701
#define FOLDERDELIMCHAR '~'
#endif


class cXVDRServerConfig : public cConfig<cSetupLine>
{
public:
  cXVDRServerConfig();

  void Load();

protected:

  bool Parse(const char* Name, const char* Value);

public:

  // Remote server settings
  cString ConfigDirectory;      // config directory path
  uint16_t listen_port;         // Port of remote server
  uint16_t stream_timeout;      // timeout in seconds for stream data
  cString PiconsURL;
};

// Global instance
extern cXVDRServerConfig XVDRServerConfig;

#endif // XVDR_CONFIG_H
