/*
 *      vdr-plugin-xvdr - XVDR server plugin for VDR
 *
 *      Copyright (C) 2010 Alwin Esch (Team XBMC)
 *      Copyright (C) 2010, 2011 Alexander Pipelka
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

#include <netdb.h>
#include <poll.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include <vdr/plugin.h>
#include <vdr/shutdown.h>

#include "xvdrserver.h"
#include "xvdrclient.h"
#include "xvdrchannels.h"
#include "live/channelcache.h"
#include "recordings/recordingscache.h"
#include "net/os-config.h"

//#define ENABLE_CHANNELTRIGGER 1

unsigned int cXVDRServer::m_IdCnt = 0;

class cAllowedHosts : public cSVDRPhosts
{
public:
  cAllowedHosts(const cString& AllowedHostsFile)
  {
    if (!Load(AllowedHostsFile, true, true))
    {
      ERRORLOG("Invalid or missing '%s'. falling back to 'svdrphosts.conf'.", *AllowedHostsFile);
      cString Base = cString::sprintf("%s/../svdrphosts.conf", *XVDRServerConfig.ConfigDirectory);
      if (!Load(Base, true, true))
      {
        ERRORLOG("Invalid or missing %s. Adding 127.0.0.1 to list of allowed hosts.", *Base);
        cSVDRPhost *localhost = new cSVDRPhost;
        if (localhost->Parse("127.0.0.1"))
          Add(localhost);
        else
          delete localhost;
      }
    }
  }
};

cXVDRServer::cXVDRServer(int listenPort) : cThread("VDR XVDR Server")
{
  m_IPv4Fallback = false;
  m_ServerPort  = listenPort;

  if(*XVDRServerConfig.ConfigDirectory)
  {
    m_AllowedHostsFile = cString::sprintf("%s/" ALLOWED_HOSTS_FILE, *XVDRServerConfig.ConfigDirectory);
  }
  else
  {
    ERRORLOG("cXVDRServer: missing ConfigDirectory!");
    m_AllowedHostsFile = cString::sprintf("/video/" ALLOWED_HOSTS_FILE);
  }

  m_ServerFD = socket(AF_INET6, SOCK_STREAM, 0);
  if(m_ServerFD == -1)
  {
    // trying to fallback to IPv4
    m_ServerFD = socket(AF_INET, SOCK_STREAM, 0);
    if(m_ServerFD == -1)
      return;
    else
      m_IPv4Fallback = true;
  }

  fcntl(m_ServerFD, F_SETFD, fcntl(m_ServerFD, F_GETFD) | FD_CLOEXEC);

  int one = 1;
  setsockopt(m_ServerFD, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(int));

  // setting the server socket option to listen on IPv4 and IPv6 simultaneously
  int no = 0;
  if (!m_IPv4Fallback && setsockopt(m_ServerFD, IPPROTO_IPV6, IPV6_V6ONLY, (void *)&no, sizeof(no)) < 0)
    ERRORLOG("cXVDRServer: setsockopt failed (errno=%d: %s)", errno, strerror(errno));

  struct sockaddr_storage s;
  memset(&s, 0, sizeof(s));
  if (!m_IPv4Fallback)
  {
    ((struct sockaddr_in6 *)&s)->sin6_family = AF_INET6;
    ((struct sockaddr_in6 *)&s)->sin6_port = htons(m_ServerPort);
  }
  else
  {
    ((struct sockaddr_in *)&s)->sin_family = AF_INET;
    ((struct sockaddr_in *)&s)->sin_port = htons(m_ServerPort);
  }

  int x = bind(m_ServerFD, (struct sockaddr *)&s, sizeof(s));
  if (x < 0)
  {
    close(m_ServerFD);
    INFOLOG("Unable to start XVDR Server, port already in use ?");
    m_ServerFD = -1;
    return;
  }

  listen(m_ServerFD, 10);

  Start();

  INFOLOG("XVDR Server started");
  INFOLOG("Channel streaming timeout: %i seconds", XVDRServerConfig.stream_timeout);
  return;
}

cXVDRServer::~cXVDRServer()
{
  Cancel(-1);
  for (ClientList::iterator i = m_clients.begin(); i != m_clients.end(); i++)
  {
    delete (*i);
  }
  m_clients.erase(m_clients.begin(), m_clients.end());
  Cancel();

  cChannelCache::SaveChannelCacheData();

  INFOLOG("XVDR Server stopped");
}

void cXVDRServer::NewClientConnected(int fd)
{
  struct sockaddr_storage sin;
  socklen_t len = sizeof(sin);
  in_addr_t *ipv4_addr = NULL;

  if (getpeername(fd, (struct sockaddr *)&sin, &len))
  {
    ERRORLOG("getpeername() failed, dropping new incoming connection %d", m_IdCnt);
    close(fd);
    return;
  }

  if (!m_IPv4Fallback)
  {
    if (IN6_IS_ADDR_V4MAPPED(&((struct sockaddr_in6 *)&sin)->sin6_addr) ||
        IN6_IS_ADDR_V4COMPAT(&((struct sockaddr_in6 *)&sin)->sin6_addr))
      ipv4_addr = &((struct sockaddr_in6 *)&sin)->sin6_addr.s6_addr32[3];
  }
  else
    ipv4_addr = &((struct sockaddr_in *)&sin)->sin_addr.s_addr;

  // Acceptable() method only supports in_addr_t argument so we're currently checking IPv4 hosts only
  if (ipv4_addr)
  {
    cAllowedHosts AllowedHosts(m_AllowedHostsFile);
    if (!AllowedHosts.Acceptable(*ipv4_addr))
    {
      ERRORLOG("Address not allowed to connect (%s)", *m_AllowedHostsFile);
      close(fd);
      return;
    }
  }

  if (fcntl(fd, F_SETFL, fcntl (fd, F_GETFL) | O_NONBLOCK) == -1)
  {
    ERRORLOG("Error setting control socket to nonblocking mode");
    close(fd);
    return;
  }

  int val = 1;
  setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &val, sizeof(val));

#ifndef __FreeBSD__
  val = 30;
  setsockopt(fd, SOL_TCP, TCP_KEEPIDLE, &val, sizeof(val));

  val = 15;
  setsockopt(fd, SOL_TCP, TCP_KEEPINTVL, &val, sizeof(val));

  val = 5;
  setsockopt(fd, SOL_TCP, TCP_KEEPCNT, &val, sizeof(val));
#endif

  val = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val));

  if (!m_IPv4Fallback)
    INFOLOG("Client %s:%i with ID %d connected.", xvdr_inet_ntoa(((struct sockaddr_in6 *)&sin)->sin6_addr), ((struct sockaddr_in6 *)&sin)->sin6_port, m_IdCnt);
  else
    INFOLOG("Client %s:%i with ID %d connected.", inet_ntoa(((struct sockaddr_in *)&sin)->sin_addr), ((struct sockaddr_in *)&sin)->sin_port, m_IdCnt);
  cXVDRClient *connection = new cXVDRClient(fd, m_IdCnt);
  m_clients.push_back(connection);
  m_IdCnt++;
}

void cXVDRServer::Action(void)
{
  fd_set fds;
  struct timeval tv;
  cTimeMs channelReloadTimer;
  cTimeMs channelCacheTimer;
  cTimeMs recordingReloadTimer;

  bool channelReloadTrigger = false;
  bool recordingReloadTrigger = false;
  uint64_t channelsHash = 0;

  SetPriority(19);

  // get initial state of the recordings
  int recState = -1;
  int recStateOld = -1;

  Recordings.StateChanged(recState);
  recStateOld = recState;

  while (Running())
  {
    FD_ZERO(&fds);
    FD_SET(m_ServerFD, &fds);

    tv.tv_sec = 0;
    tv.tv_usec = 250*1000;

    int r = select(m_ServerFD + 1, &fds, NULL, NULL, &tv);
    if (r == -1)
    {
      ERRORLOG("failed during select");
      continue;
    }
    if (r == 0)
    {
      // remove disconnected clients
      bool bChanged = false;
      for (ClientList::iterator i = m_clients.begin(); i != m_clients.end();)
      {

        if (!(*i)->Active())
        {
          INFOLOG("Client with ID %u seems to be disconnected, removing from client list", (*i)->GetID());
          delete (*i);
          i = m_clients.erase(i);
          bChanged = true;
        }
        else {
          i++;
        }
      }
      // store channel cache
      if(bChanged) {
        cChannelCache::SaveChannelCacheData();
      }

      // trigger clients to reload the modified channel list
      if(m_clients.size() > 0)
      {
        uint64_t hash = XVDRChannels.CheckUpdates();
        XVDRChannels.Lock(false);

        if(hash != channelsHash)
        {
          channelReloadTrigger = true;
          channelReloadTimer.Set(0);
        }
        if(channelReloadTrigger && channelReloadTimer.Elapsed() >= 10*1000)
        {
          INFOLOG("Checking for channel updates ...");
          for (ClientList::iterator i = m_clients.begin(); i != m_clients.end(); i++)
            (*i)->ChannelChange();
          channelReloadTrigger = false;
          INFOLOG("Done.");
        }

        XVDRChannels.Unlock();
        channelsHash = hash;
      }

      // reset inactivity timeout as long as there are clients connected
      if(m_clients.size() > 0) {
        ShutdownHandler.SetUserInactiveTimeout();
      }

      // store channel cache
      if(m_clients.size() > 0 && channelCacheTimer.Elapsed() >= 60*1000) {
        cChannelCache::SaveChannelCacheData();
        channelCacheTimer.Set(0);
      }

      // check for recording changes
      Recordings.StateChanged(recState);
      if(recState != recStateOld || cRecordingsCache::GetInstance().Changed())
      {
        recordingReloadTrigger = true;
        recordingReloadTimer.Set(2000);
        INFOLOG("Recordings state changed (%i)", recState);
        recStateOld = recState;
      }

      // update recordings
      if(recordingReloadTrigger && recordingReloadTimer.TimedOut()) {
        INFOLOG("Requesting clients to reload recordings list");

        for (ClientList::iterator i = m_clients.begin(); i != m_clients.end(); i++) {
          (*i)->RecordingsChange();
        }

        recordingReloadTrigger = false;
      }

      // no connect request -> continue waiting
      continue;
    }

    int fd = accept(m_ServerFD, 0, 0);
    if (fd >= 0)
    {
      NewClientConnected(fd);
    }
    else
    {
      ERRORLOG("accept failed");
    }
  }
  return;
}
