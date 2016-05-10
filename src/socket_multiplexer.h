#ifndef SOCKET_MULTIPLEXER_SOCKET_MULTIPLEXER_H_
#define SOCKET_MULTIPLEXER_SOCKET_MULTIPLEXER_H_

#include <mutex>
#include <thread>
#include <unordered_set>
#include <unordered_map>
#include <random>

class SocketMultiplexer {
public:
  struct Config {
    std::string control_socket_path;
    std::string master_socket_path;
  };

  SocketMultiplexer() = delete;
  SocketMultiplexer(SocketMultiplexer&) = delete;
  SocketMultiplexer& operator=(const SocketMultiplexer&) = delete;

  SocketMultiplexer(const Config& config);

  ~SocketMultiplexer();

  void Run();
  void Wait();
  std::string Shutdown();

private:
  int TryConnectActiveSocket(int uid);

  void MainLoop();
  void ControlLoop();

  std::string DispatchCommand(int uid, const std::string& line);
  std::string AddSocket(int uid, const std::string& socket);
  std::string DeleteSocket(int uid, const std::string& socket);
  std::string ClearSocket(int uid);
  std::string ListSocket(int uid) const;

  const Config config_;
  typedef std::unordered_set<std::string> Files;
  typedef std::unordered_map<int, Files> FilesMap;
  mutable std::mutex slave_files_map_lock_;
  FilesMap slave_files_map_;

  mutable std::mt19937 rand_;

  int master_socket_;
  int control_socket_;

  std::thread master_thread_;
  std::thread control_thread_;
};

#endif
