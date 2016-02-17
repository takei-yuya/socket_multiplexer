#ifndef SOCKET_MULTIPLEXER_SOCKET_UTILS_H_
#define SOCKET_MULTIPLEXER_SOCKET_UTILS_H_

#include <unistd.h>
#include <string>

template <typename F>
int NoINTR(F f) {
  while(true) {
    int ret = f();
    if (ret == -1 && errno == EINTR) {
      continue;
    }
    return ret;
  }
}

int CreateSocket(const std::string& socket_name);
int ConnectSocket(const std::string& socket_name);
int AcceptSocket(int socket);
void SocketCoupler(int fd1, int fd2);

#endif
