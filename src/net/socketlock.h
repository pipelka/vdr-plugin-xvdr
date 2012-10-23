#ifndef XVDR_SOCKETLOCK_H
#define XVDR_SOCKETLOCK_H

#include <map>
#include <vdr/thread.h>

class cSocketMutex : public std::map<int, cMutex> {
};

class cSocketLock : public cMutexLock {
public:

  cSocketLock(int sock) : cMutexLock(&m_sockets[sock]) {
  }

  static void erase(int sock);

private:

  static cSocketMutex m_sockets;

};

#endif // XVDR_SOCKETLOCK_H
