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
int AcceptSocket(int socket);
int ConnectSocket(const std::string& socket_name);
void SocketCoupler(int fd1, int fd2);

int SelectOne(int fd);
int PeekSocketCredentials(int fd, int *pid, int *uid, int *gid);

class FDCloser {
public:
  explicit FDCloser(int fd) : fd_(fd) {}
  ~FDCloser() {
    Close();
  }

  FDCloser() = delete;
  FDCloser(const FDCloser&) = delete;
  FDCloser& operator=(const FDCloser&) = delete;

  void Close() {
    if (fd_ < 0) return;
    NoINTR([&](){ return close(Release()); });
  }

  int Release() {
    int fd = -1;
    std::swap(fd, fd_);
    return fd;
  }

private:
  int fd_;
};

class Unlinker {
public:
  explicit Unlinker(const std::string& path) : path_(path) {}
  ~Unlinker() {
    NoINTR([&](){ return unlink(path_.c_str()); });
  }

  Unlinker() = delete;
  Unlinker(const Unlinker&) = delete;
  Unlinker& operator=(const Unlinker&) = delete;

private:
  std::string path_;
};

#endif
