#include <sys/socket.h>

#include <iostream>
#include <sstream>

#include "socket_multiplexer.h"
#include "socket_utils.h"

SocketMultiplexer::SocketMultiplexer(const Config& config)
  : config_(config), slave_socket_files_()
  , rand_(std::random_device()())
  , master_socket_(-1), control_socket_(-1) {
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

int SocketMultiplexer::TryConnectActiveSocket() {
  // NOTE: rwlock
  std::lock_guard<std::mutex> lock(slave_socket_files_lock_);

  while (slave_socket_files_.size() > 0) {
    auto it = slave_socket_files_.cbegin();
    {
      std::uniform_int_distribution<int> selector(0, slave_socket_files_.size() - 1);
      for (int i = 0; i < selector(rand_); ++i) {
        ++it;
      }
    }

    int fd = ConnectSocket(*it);
    if (fd < 0) {
      std::cout << "Rusted socket " << *it << std::endl;
      slave_socket_files_.erase(it);
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
    int slave = TryConnectActiveSocket();
    if (slave < 0) {
      close(master);
      continue;
    }

    std::thread t(SocketCoupler, master, slave);
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
    std::cout << "AcceptSocket: " << control_fd << std::endl;
    if (control_fd == -1) {
      perror("AcceptSocket()");
      break;
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
        std::string result = DispatchCommand(line);
        send(control_fd, result.c_str(), result.size(), 0);
      }
    }

    close(control_fd);
  }
  close(control_socket_);
}

std::string SocketMultiplexer::DispatchCommand(const std::string& line) {
  std::istringstream iss(line);
  std::string command, arg;
  iss >> command >> arg;

  std::cout << "Command: " << command << " " << arg << std::endl;

  if (command == "QUIT") {
    return Shutdown();
  }
  if (command == "ADD") {
    return AddSocket(arg);
  }
  if (command == "DELETE") {
    return DeleteSocket(arg);
  }
  if (command == "LIST") {
    return ListSocket();
  }
  return "Unknwon command " + command + "\n";
}

std::string SocketMultiplexer::AddSocket(const std::string& socket) {
  std::cout << "AddSocket('" << socket << "')" << std::endl;
  // NOTE: rwlock
  std::lock_guard<std::mutex> lock(slave_socket_files_lock_);
  slave_socket_files_.insert(socket);
  return std::string("ADDed ") + socket + "\n";
}

std::string SocketMultiplexer::DeleteSocket(const std::string& socket) {
  std::cout << "DeleteSocket('" << socket << "')" << std::endl;
  // NOTE: rwlock
  std::lock_guard<std::mutex> lock(slave_socket_files_lock_);
  slave_socket_files_.erase(socket);
  return std::string("DELETEed ") + socket + "\n";
}

std::string SocketMultiplexer::ClearSocket() {
  std::cout << "ClearSocket()" << std::endl;
  // NOTE: rwlock
  std::lock_guard<std::mutex> lock(slave_socket_files_lock_);
  std::unordered_set<std::string>().swap(slave_socket_files_);
  return std::string("CLEARed") + "\n";
}

std::string SocketMultiplexer::ListSocket() const {
  std::cout << "ListSocket()" << std::endl;
  // NOTE: rwlock
  std::lock_guard<std::mutex> lock(slave_socket_files_lock_);
  std::string result;
  for (auto it = slave_socket_files_.begin(); it != slave_socket_files_.end(); ++it) {
    result += *it + "\n";
  }
  return result;
}
