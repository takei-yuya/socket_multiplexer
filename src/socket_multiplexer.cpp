#include <sys/socket.h>

#include <iostream>
#include <sstream>

#include "logger.hpp"
#include "socket_multiplexer.hpp"
#include "socket_utils.hpp"

SocketMultiplexer::SocketMultiplexer(const Config& config)
  : config_(config)
  , slave_files_map_lock_(), slave_files_map_()
  , rand_(std::random_device()())
  , master_socket_(-1), control_socket_(-1)
  , master_thread_(), control_thread_() {
}

SocketMultiplexer::~SocketMultiplexer() {
}

void SocketMultiplexer::Run() {
  master_thread_ = std::thread(&SocketMultiplexer::MainLoop, this);
  control_thread_ = std::thread(&SocketMultiplexer::ControlLoop, this);
}

std::string SocketMultiplexer::Shutdown() {
  LOG(INFO) << "Shutdown()";
  NoINTR([this](){ return shutdown(this->master_socket_, SHUT_RDWR); });
  NoINTR([this](){ return shutdown(this->control_socket_, SHUT_RDWR); });
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
    LOG(ERROR) << "No sockets for this user(uid: " << uid << ")";
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
      LOG(INFO) << "Rusted socket " << *it;
      slave_socket_files->second.erase(it);
      continue;
    }
    LOG(INFO) << "Use socket " << *it;
    return fd;
  }
  return -1;
}

void SocketMultiplexer::MainLoop() {
  master_socket_ = CreateSocket(config_.master_socket_path);
  Unlinker u_master_socket(config_.master_socket_path);
  FDCloser c_master_socket(master_socket_);
  if (master_socket_ < 0) {
    ELOG(ERROR, "CreateSocket()", errno);
    return;
  }

  while (true) {
    int master_fd = AcceptSocket(master_socket_);
    if (master_fd < 0 && errno == EINVAL) {
      break;
    }
    if (master_fd < 0) {
      ELOG(ERROR, "AcceptSocket()", errno);
      break;
    }

    std::thread t([this](int master_fd) {
      FDCloser c_master_fd(master_fd);
      int uid;
      int ret = PeekSocketCredentials(master_fd, NULL, &uid, NULL);
      if (ret < 0) {
        ELOG(ERROR, "PeekSocketCredentials()", errno);
        return;
      }

      int slave_fd = this->TryConnectActiveSocket(uid);
      FDCloser c_slave_fd(slave_fd);
      if (slave_fd < 0) {
        ELOG(ERROR, "TryConnectActiveSocket()", errno);
        return;
      }

      SocketCoupler(master_fd, slave_fd);
    }, master_fd);
    t.detach();
  }
}

void SocketMultiplexer::ControlLoop() {
  control_socket_ = CreateSocket(config_.control_socket_path);
  Unlinker u_control_socket(config_.control_socket_path);
  FDCloser c_control_socket(control_socket_);
  if (control_socket_ < 0) {
    ELOG(ERROR, "CreateSocket()", errno);
    return;
  }

  while (true) {
    int control_fd = AcceptSocket(control_socket_);
    if (control_fd < 0 && errno == EINVAL) {
      break;
    }
    if (control_fd < 0) {
      ELOG(ERROR, "AcceptSocket()", errno);
      break;
    }

    std::thread t([this](int control_fd) {
      FDCloser c_control_fd(control_fd);
      char buf[1024];
      int uid;
      int ret = PeekSocketCredentials(control_fd, NULL, &uid, NULL);
      if (ret < 0) {
        ELOG(ERROR, "PeekSocketCredentials()", errno);
        return;
      }

      while (true) {
        int recved = recv(control_fd, buf, 1024, 0);
        if (recved < 0) {
          ELOG(ERROR, "recv", errno);
          break;
        }
        if (recved == 0) {
          break;
        }
        std::istringstream iss(std::string(buf, recved));
        std::string line;
        while (std::getline(iss, line)) {
          std::string result = this->DispatchCommand(uid, line);
          send(control_fd, result.c_str(), result.size(), 0);
        }
      }
    }, control_fd);
    t.detach();
  }
}

std::string SocketMultiplexer::DispatchCommand(int uid, const std::string& line) {
  std::istringstream iss(line);
  std::string command, arg;
  iss >> command >> std::ws;
  std::getline(iss, arg);

  if (command.empty()) {
    return "";
  }
  if (command == "QUIT") {
    // FIXME: should check uid? (allow only process owner?)
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
  LOG(ERROR) << "Unknwon command '" << command << "(" << arg << ")'";
  return "Unknwon command " + command + "\n";
}

std::string SocketMultiplexer::AddSocket(int uid, const std::string& socket) {
  LOG(INFO) << "AddSocket(" << uid << ", '" << socket << "')";
  // NOTE: rwlock
  std::lock_guard<std::mutex> lock(slave_files_map_lock_);
  Files &slave_socket_files = slave_files_map_[uid];
  slave_socket_files.insert(socket);
  return std::string("ADDed ") + socket + "\n";
}

std::string SocketMultiplexer::DeleteSocket(int uid, const std::string& socket) {
  LOG(INFO) << "DeleteSocket('" << uid << ", " << socket << "')";
  // NOTE: rwlock
  std::lock_guard<std::mutex> lock(slave_files_map_lock_);
  Files &slave_socket_files = slave_files_map_[uid];
  slave_socket_files.erase(socket);
  return std::string("DELETEed ") + socket + "\n";
}

std::string SocketMultiplexer::ClearSocket(int uid) {
  LOG(INFO) << "ClearSocket(" << uid << ")";
  // NOTE: rwlock
  std::lock_guard<std::mutex> lock(slave_files_map_lock_);
  Files().swap(slave_files_map_[uid]);
  return std::string("CLEARed") + "\n";
}

std::string SocketMultiplexer::ListSocket(int uid) const {
  LOG(INFO) << "ListSocket(" << uid << ")";
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
