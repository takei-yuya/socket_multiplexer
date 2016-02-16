#include <unistd.h>
#include <signal.h>

#include "socket_multiplexer.h"

std::unique_ptr<SocketMultiplexer> socket_multiplexer;

int main(int argc, char** argv) {
  const std::string control_socket_path = "/tmp/socket_multiplexer_control";
  const std::string interface_socket = "/tmp/socket_multiplexer";

  unlink(control_socket_path.c_str());
  unlink(interface_socket.c_str());

  SocketMultiplexer::Config config = { control_socket_path, interface_socket };

  socket_multiplexer.reset(new SocketMultiplexer(config));

  signal(SIGPIPE, SIG_IGN);
  signal(SIGINT, [](int sig){ socket_multiplexer->Shutdown(); });

  socket_multiplexer->Run();
  socket_multiplexer->Wait();
}

