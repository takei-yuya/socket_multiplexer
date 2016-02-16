#include <unistd.h>
#include <signal.h>
#include <getopt.h>
#include <iostream>

#include "socket_multiplexer.h"
#include "config.h"

namespace {
const std::string kDefaultControlSocketFile = "/tmp/socket_multiplexer_control";
const std::string kDefaultMasterSocketFile = "/tmp/socket_multiplexer";
}

std::unique_ptr<SocketMultiplexer> socket_multiplexer;

void Usage(std::ostream& out, int argc, char** argv) {
  out
    << "Usage: " << argv[0] << std::endl
    << std::endl
    << "  -c, --control=PATH    control socket path (default: " << kDefaultControlSocketFile << ")" << std::endl
    << "  -s, --socket=PATH     interface socket path (default: " << kDefaultMasterSocketFile << ")" << std::endl
    << std::endl
    << "  -?, --help            display this help and exit" << std::endl
    << "  -v, --version         output version information and exit" << std::endl
  ;
}

int main(int argc, char** argv) {
  SocketMultiplexer::Config config = { kDefaultControlSocketFile, kDefaultMasterSocketFile };

  struct option long_options[] = {
    { "control",  required_argument,  0,  'c' },
    { "socket",   required_argument,  0,  's' },
    { "help",     no_argument,        0,  '?' },
    { "version",  no_argument,        0,  'v' },
    { 0, 0, 0, 0 }
  };

  while (true) {
    int ch = getopt_long(argc, argv, "?vc:s:", long_options, NULL);
    if (ch < 0) {
      break;
    }

    switch (ch) {
    case 'c':
      config.control_socket_path = optarg;
      break;

    case 's':
      config.master_socket_path = optarg;
      break;

    case '?':
      Usage(std::cout, argc, argv);
      exit(0);

    case 'v':
      std::cout << PACKAGE_STRING << std::endl;
      exit(0);

    default:
      Usage(std::cerr, argc, argv);
      exit(1);
    }
  }

  unlink(config.control_socket_path.c_str());
  unlink(config.master_socket_path.c_str());

  socket_multiplexer.reset(new SocketMultiplexer(config));

  signal(SIGPIPE, SIG_IGN);
  signal(SIGINT, [](int sig){ socket_multiplexer->Shutdown(); });

  socket_multiplexer->Run();
  socket_multiplexer->Wait();
}

