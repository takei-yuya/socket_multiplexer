SRCS= main.cc socket_multiplexer.cc socket_utils.cc
OBJS= $(SRCS:.cc=.o)
BIN= socket_multiplexer

CXXFLAGS= -g -std=c++11
LDFLAGS= -lpthread

PHONY=
PHONY+= all
all: $(BIN)

$(BIN): $(OBJS)
	$(CXX) $(LDFLAGS) -o $@ $^

PHONY+= clean
clean:
	$(RM) $(OBJS) $(BIN)

.PHONY: $(PHONY)
