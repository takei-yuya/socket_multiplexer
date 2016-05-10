#include <sys/socket.h>

#include <iostream>
#include <sstream>

#include "socket_multiplexer.h"
#include "socket_utils.h"

SocketMultiplexer::SocketMultiplexer(const Config& config)
  : config_(config)
  , slave_files_map_lock_(), slave_files_map_()
  , rand_(std::random_device()())
  , master_socket_(-1), control_socket_(-1)
  , master_thread_(), control_thread_() {
}

SocketMultiplexer::~SocketMultiplexer() {
  if (master_socket_ != -1) {
    NoINTR([&](){ return close(master_socket_); });
    NoINTR([&](){ return unlink(config_.master_socket_path.c_str()); });
  }

  if (control_socket_ != -1) {
    NoINTR([&](){ return close(control_socket_); });
    NoINTR([&](){ return unlink(config_.control_socket_path.c_str()); });
  }
}

void SocketMultiplexer::Run() {
  master_thread_ = std::thread(&SocketMultiplexer::MainLoop, this);
  control_thread_ = std::thread(&SocketMultiplexer::ControlLoop, this);
}

std::string SocketMultiplexer::Shutdown() {
  std::cout << "Shutdown()" << std::endl;
  NoINTR([&](){ return shutdown(master_socket_, SHUT_RDWR); });
  NoINTR([&](){ return shutdown(control_socket_, SHUT_RDWR); });
  return "";
}

void SocketMultiplexer::Wait() {
  if (master_thread_.joinable())
    master_thread_.join();
  if (control_thread_.joinable())
    control_thread_.join();
}

int SocketMultiplexer::TryConnectActiveSocket(int uid) {
  // NOTE: rwlock
  std::lock_guard<std::mutex> lock(slave_files_map_lock_);

  FilesMap::iterator slave_socket_files = slave_files_map_.find(uid);
  if (slave_socket_files == slave_files_map_.end() || slave_socket_files->second.size() == 0) {
    std::cout << "No sockets for this user(uid: " << uid << ")" << std::endl;
    return -1;
  }

  while (slave_socket_files->second.size() > 0) {
    auto it = slave_socket_files->second.cbegin();
    {
      std::uniform_int_distribution<int> selector(0, slave_socket_files->second.size() - 1);
      for (int i = 0; i < selector(rand_); ++i) {
        ++it;
      }
    }

    int fd = ConnectSocket(*it);
    if (fd < 0) {
      std::cout << "Rusted socket " << *it << std::endl;
      slave_socket_files->second.erase(it);
      continue;
    }
    std::cout << "Use socket " << *it << std::endl;
    return fd;
  }
  return -1;
}

void SocketMultiplexer::MainLoop() {
  master_socket_ = CreateSocket(config_.master_socket_path);
  if (master_socket_ < 0) {
    return;
  }

  while (true) {
    int master = AcceptSocket(master_socket_);
    if (master < 0) {
      break;
    }

    std::thread t([&]() {
      int uid;
      int ret = PeekSocketCredentials(master, NULL, &uid, NULL);
      if (ret < 0) {
        perror("PeekSocketCredentials");
        close(master);
        return;
      }

      int slave = TryConnectActiveSocket(uid);
      if (slave < 0) {
        close(master);
        return;
      }

      SocketCoupler(master, slave);
      close(master);
      close(slave);
    });
    t.detach();
  }
  close(master_socket_);
}

void SocketMultiplexer::ControlLoop() {
  char buf[1024];
  control_socket_ = CreateSocket(config_.control_socket_path);
  if (control_socket_ < 0) {
    return;
  }

  while (true) {
    int control_fd = AcceptSocket(control_socket_);
    if (control_fd == -1 && errno == EINVAL) {
      break;
    }
    if (control_fd == -1) {
      perror("AcceptSocket()");
      break;
    }

    std::thread t([&]() {
      int uid;
      int ret = PeekSocketCredentials(control_fd, NULL, &uid, NULL);
      if (ret < 0) {
        perror("PeekSocketCredentials");
        close(control_fd);
        return;
      }

      while (true) {
        int recved = recv(control_fd, buf, 1024, 0);
        if (recved < 0) {
          perror("recv");
          close(control_fd);
          break;
        }
        if (recved == 0) {
          break;
        }
        std::istringstream iss(std::string(buf, recved));
        std::string line;
        while (std::getline(iss, line)) {
          std::string result = DispatchCommand(uid, line);
          send(control_fd, result.c_str(), result.size(), 0);
        }
      }
      close(control_fd);
    });
    t.detach();
  }
  close(control_socket_);
}

std::string SocketMultiplexer::DispatchCommand(int uid, const std::string& line) {
  std::istringstream iss(line);
  std::string command, arg;
  iss >> command >> std::ws;
  std::getline(iss, arg);

  if (command == "QUIT") {
    return Shutdown();
  }
  if (command == "ADD") {
    return AddSocket(uid, arg);
  }
  if (command == "DELETE") {
    return DeleteSocket(uid, arg);
  }
  if (command == "LIST") {
    return ListSocket(uid);
  }
  return "Unknwon command " + command + "\n";
}

std::string SocketMultiplexer::AddSocket(int uid, const std::string& socket) {
  std::cout << "AddSocket(" << uid << ", '" << socket << "')" << std::endl;
  // NOTE: rwlock
  std::lock_guard<std::mutex> lock(slave_files_map_lock_);
  Files &slave_socket_files = slave_files_map_[uid];
  slave_socket_files.insert(socket);
  return std::string("ADDed ") + socket + "\n";
}

std::string SocketMultiplexer::DeleteSocket(int uid, const std::string& socket) {
  std::cout << "DeleteSocket('" << uid << ", " << socket << "')" << std::endl;
  // NOTE: rwlock
  std::lock_guard<std::mutex> lock(slave_files_map_lock_);
  Files &slave_socket_files = slave_files_map_[uid];
  slave_socket_files.erase(socket);
  return std::string("DELETEed ") + socket + "\n";
}

std::string SocketMultiplexer::ClearSocket(int uid) {
  std::cout << "ClearSocket(" << uid << ")" << std::endl;
  // NOTE: rwlock
  std::lock_guard<std::mutex> lock(slave_files_map_lock_);
  Files().swap(slave_files_map_[uid]);
  return std::string("CLEARed") + "\n";
}

std::string SocketMultiplexer::ListSocket(int uid) const {
  std::cout << "ListSocket(" << uid << ")" << std::endl;
  // NOTE: rwlock rlock
  std::lock_guard<std::mutex> lock(slave_files_map_lock_);
  FilesMap::const_iterator slave_socket_files = slave_files_map_.find(uid);
  std::string result;
  if (slave_socket_files == slave_files_map_.end()) {
    return result;
  }
  for (auto it = slave_socket_files->second.begin(); it != slave_socket_files->second.end(); ++it) {
    result += *it + "\n";
  }
  return result;
}
