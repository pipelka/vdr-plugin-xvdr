#ifndef XVDR_SOCKETLOCK_H
#define XVDR_SOCKETLOCK_H

#include <map>
#include <vdr/thread.h>

class cSocketMutex : public std::map<int, cMutex> {
};

class cSocketLock {
public:

  cSocketLock(int sock) : m_socket(sock) {
    m_sockets[m_socket].Lock();
  }

  ~cSocketLock() {
    m_sockets[m_socket].Unlock();
  }

  static void erase(int sock);

private:

  static cSocketMutex m_sockets;

  int m_socket;
};

#endif // XVDR_SOCKETLOCK_H
