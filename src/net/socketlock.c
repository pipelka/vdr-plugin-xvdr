#include "socketlock.h"

cSocketMutex cSocketLock::m_sockets;

void cSocketLock::erase(int sock) {
  cSocketMutex::iterator i = m_sockets.find(sock);

  if(i == m_sockets.end())
    return;

  m_sockets.erase(sock);
}
