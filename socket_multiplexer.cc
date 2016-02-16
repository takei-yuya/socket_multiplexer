#include <sys/socket.h>
#include <sys/un.h>

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>

#include <string>
#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <sstream>
#include <unordered_set>

#include "socket.h"

class SocketMultiplexer {
public:
  struct Config {
    std::string control_socket_path;
    std::string master_socket_path;
  };

  SocketMultiplexer() = delete;
  SocketMultiplexer(SocketMultiplexer&) = delete;

  SocketMultiplexer(const Config& config)
    : config_(config), slave_socket_files_()
    , master_socket_(-1), control_socket_(-1) {
  }

  ~SocketMultiplexer() {
    NoINTR([&](){ return close(master_socket_); });
    NoINTR([&](){ return close(control_socket_); });
  }

  void Run() {
    master_thread_ = std::thread(&SocketMultiplexer::MainLoop, this);
    control_thread_ = std::thread(&SocketMultiplexer::ControlLoop, this);
  }

  std::string Shutdown() {
    std::cout << "Shutdown()" << std::endl;
    NoINTR([&](){ return shutdown(master_socket_, SHUT_RDWR); });
    NoINTR([&](){ return shutdown(control_socket_, SHUT_RDWR); });
    return "";
  }

  void Wait() {
    if (master_thread_.joinable())
      master_thread_.join();
    if (control_thread_.joinable())
      control_thread_.join();
  }

private:
  int TryConnectActiveSocket() {
    // NOTE: rwlock
    std::lock_guard<std::mutex> lock(slave_socket_files_lock_);

    while (slave_socket_files_.size() > 0) {
      auto it = slave_socket_files_.cbegin();
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

  void MainLoop() {
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

  void ControlLoop() {
    char buf[1024];
    control_socket_ = CreateSocket(config_.control_socket_path);
    if (control_socket_ < 0) {
      return;
    }

    while (true) {
      int control_fd = AcceptSocket(control_socket_);
      std::cout << "AcceptSocket: " << control_fd << std::endl;
      if (control_fd == -1) {
        perror("AcceptSocket()");
        break;
      }
      while (true) {
        int recved = recv(control_fd, buf, 1024, 0);
        std::cout << "recved = " << recved << std::endl;
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

  std::string DispatchCommand(const std::string& line) {
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

  std::string AddSocket(const std::string& socket) {
    std::cout << "AddSocket('" << socket << "')" << std::endl;
    // NOTE: rwlock
    std::lock_guard<std::mutex> lock(slave_socket_files_lock_);
    slave_socket_files_.insert(socket);
    return std::string("ADDed ") + socket + "\n";
  }

  std::string DeleteSocket(const std::string& socket) {
    std::cout << "DeleteSocket('" << socket << "')" << std::endl;
    // NOTE: rwlock
    std::lock_guard<std::mutex> lock(slave_socket_files_lock_);
    slave_socket_files_.erase(socket);
    return std::string("DELETEed ") + socket + "\n";
  }

  std::string ClearSocket() {
    std::cout << "ClearSocket()" << std::endl;
    // NOTE: rwlock
    std::lock_guard<std::mutex> lock(slave_socket_files_lock_);
    std::unordered_set<std::string>().swap(slave_socket_files_);
    return std::string("CLEARed") + "\n";
  }

  std::string ListSocket() const {
    std::cout << "ListSocket()" << std::endl;
    // NOTE: rwlock
    std::lock_guard<std::mutex> lock(slave_socket_files_lock_);
    std::string result;
    for (auto it = slave_socket_files_.begin(); it != slave_socket_files_.end(); ++it) {
      result += *it + "\n";
    }
    return result;
  }

  const Config config_;
  mutable std::mutex slave_socket_files_lock_;
  std::unordered_set<std::string> slave_socket_files_;

  int master_socket_;
  int control_socket_;

  std::thread master_thread_;
  std::thread control_thread_;
};

std::unique_ptr<SocketMultiplexer> socket_multiplexer;

int main(int argc, char** argv) {
  const std::string control_socket_path = "/tmp/socket_multiplexer_control";
  const std::string interface_socket = "/tmp/sock_proxy";

  unlink(control_socket_path.c_str());
  unlink(interface_socket.c_str());

  SocketMultiplexer::Config config = { control_socket_path, interface_socket };

  socket_multiplexer.reset(new SocketMultiplexer(config));

  IgnoreSigPipe();
  signal(SIGINT, [](int sig){ socket_multiplexer->Shutdown(); });

  socket_multiplexer->Run();
  socket_multiplexer->Wait();
}

