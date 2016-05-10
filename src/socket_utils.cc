#include "socket_utils.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>

#include <functional>

int CreateSocket(const std::string& socket_name) {
  int sock = NoINTR([&](){ return socket(AF_UNIX, SOCK_STREAM, 0); });
  if (sock < 0) {
    return -1;
  }

  struct sockaddr_un addr = { 0 };
  addr.sun_family = AF_UNIX;
  socket_name.copy(addr.sun_path, sizeof(addr.sun_path));
  addr.sun_path[std::min(socket_name.size(), sizeof(addr.sun_path))] = '\0';

  {
    int one = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_PASSCRED, &one, sizeof(one)) != 0) {
      return -1;
    }
  }

  if (NoINTR([&](){ return bind(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)); }) != 0) {
    return -1;
  }

  if (NoINTR([&](){ return listen(sock, 16); }) != 0) {
    return -1;
  }

  chmod(addr.sun_path, 0777);

  return sock;
}

int AcceptSocket(int socket) {
  int fd = NoINTR([&](){ return accept(socket, NULL, NULL); });
  return fd;
}

int ConnectSocket(const std::string& socket_name) {
  int sock = NoINTR([&](){ return socket(AF_UNIX, SOCK_STREAM, 0); });
  if (sock < 0) {
    return -1;
  }

  struct sockaddr_un addr = { 0 };
  addr.sun_family = AF_UNIX;
  socket_name.copy(addr.sun_path, sizeof(addr.sun_path));
  addr.sun_path[std::min(socket_name.size(), sizeof(addr.sun_path))] = '\0';

  if (NoINTR([&](){ return connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)); }) != 0) {
    return -1;
  }

  return sock;
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
}

int SelectOne(int fd) {
  fd_set rfds;
  FD_ZERO(&rfds);
  FD_SET(fd, &rfds);

  return NoINTR([&](){ return select(fd + 1, &rfds, NULL, NULL, NULL); });
}

#include <iostream>

int PeekSocketCredentials(int fd, int *pid, int *uid, int *gid) {
  union {
    struct cmsghdr cmsg;
    // FIXME: Sender will not send SCM_RIGHTS, but alloc some bytes to recv SCM_CREDENTIALS in just case.
    char cmsg_buf[CMSG_SPACE(sizeof(struct ucred) + 16 * sizeof(int))];
  } cmsg_un = { 0 };

  struct msghdr msg = { 0 };
  msg.msg_name = NULL;
  msg.msg_namelen = 0;
  msg.msg_iov = NULL;
  msg.msg_iovlen = 0;
  msg.msg_control = &cmsg_un.cmsg_buf;
  msg.msg_controllen = sizeof(cmsg_un.cmsg_buf);
  msg.msg_flags = 0;

  SelectOne(fd);
  int recved = NoINTR([&]() { return recvmsg(fd, &msg, 0); });
  if (recved < 0) {
    return  -1;
  }

  for (struct cmsghdr *cmptr = CMSG_FIRSTHDR(&msg); cmptr != NULL; cmptr = CMSG_NXTHDR(&msg, cmptr)) {
    if (cmptr->cmsg_level == SOL_SOCKET && cmptr->cmsg_type == SCM_CREDENTIALS) {
      struct ucred *uc = reinterpret_cast<struct ucred *>(CMSG_DATA(cmptr));
      if (uc->pid == 0) {
        // empty data
        return -1;
      }
      if (pid) *pid = uc->pid;
      if (uid) *uid = uc->uid;
      if (gid) *gid = uc->gid;
    }
  }

  return 0;
}
