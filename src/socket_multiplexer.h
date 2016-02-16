#ifndef SOCKET_MULTIPLEXER_SOCKET_MULTIPLEXER_H_
#define SOCKET_MULTIPLEXER_SOCKET_MULTIPLEXER_H_

#include <mutex>
#include <thread>
#include <unordered_set>

class SocketMultiplexer {
public:
  struct Config {
    std::string control_socket_path;
    std::string master_socket_path;
  };

  SocketMultiplexer() = delete;
  SocketMultiplexer(SocketMultiplexer&) = delete;

  SocketMultiplexer(const Config& config);

  ~SocketMultiplexer();

  void Run();
  void Wait();
  std::string Shutdown();

private:
  int TryConnectActiveSocket();

  void MainLoop();
  void ControlLoop();

  std::string DispatchCommand(const std::string& line);
  std::string AddSocket(const std::string& socket);
  std::string DeleteSocket(const std::string& socket);
  std::string ClearSocket();
  std::string ListSocket() const;

  const Config config_;
  mutable std::mutex slave_socket_files_lock_;
  std::unordered_set<std::string> slave_socket_files_;

  int master_socket_;
  int control_socket_;

  std::thread master_thread_;
  std::thread control_thread_;
};

#endif
