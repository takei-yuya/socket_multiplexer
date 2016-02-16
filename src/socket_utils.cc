#include "socket_utils.h"

#include <sys/socket.h>
#include <sys/un.h>

#include <functional>

int CreateSocket(const std::string& socket_name) {
  int sock = NoINTR([&](){ return socket(PF_LOCAL, SOCK_STREAM, 0); });
  if (sock < 0) {
    return -1;
  }

  struct sockaddr_un addr = { 0 };
  addr.sun_family = AF_UNIX;
  socket_name.copy(addr.sun_path, sizeof(addr.sun_path));

  if (NoINTR([&](){ return bind(sock, (struct sockaddr*)&addr, sizeof(addr)); }) != 0) {
    return -1;
  }

  if (NoINTR([&](){ return listen(sock, 16); }) != 0) {
    return -1;
  }

  return sock;
}

int ConnectSocket(const std::string& socket_name) {
  int sock = NoINTR([&](){ return socket(PF_LOCAL, SOCK_STREAM, 0); });
  if (sock < 0) {
    return -1;
  }

  struct sockaddr_un addr = { 0 };
  addr.sun_family = AF_UNIX;
  socket_name.copy(addr.sun_path, sizeof(addr.sun_path));

  if (NoINTR([&](){ return connect(sock, (struct sockaddr*)&addr, sizeof(addr)); }) != 0) {
    return -1;
  }

  return sock;
}

int AcceptSocket(int socket) {
  int fd = NoINTR([&](){ return accept(socket, NULL, NULL); });
  return fd;
}

void SocketCoupler(int fd1, int fd2) {
  const size_t kBufferSize = 1024;
  char buf[kBufferSize];

  while (true) {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(fd1, &rfds);
    FD_SET(fd2, &rfds);

    int ret = NoINTR([&](){ return select(std::max(fd1, fd2) + 1, &rfds, NULL, NULL, NULL); });
    if (ret == -1) {
      break;
    }

    int input, output;
    if (FD_ISSET(fd1, &rfds)) {
      input = fd1;
      output = fd2;
    } else if(FD_ISSET(fd2, &rfds)) {
      input = fd2;
      output = fd1;
    }

    int recved = NoINTR([&](){ return recv(input, buf, kBufferSize, 0); });
    if (recved < 0) {
      break;
    }
    if (recved == 0) {  // May be EOF
      break;
    }

    int sended = NoINTR([&](){ return send(output, buf, recved, 0); });
    if (sended < 0) {
      break;
    }
  }
  close(fd1);
  close(fd2);
}

