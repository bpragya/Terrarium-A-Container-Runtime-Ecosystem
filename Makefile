CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2
LDLIBS   := -lsqlite3
SRC      := src/isolate.cpp src/container.cpp src/creatures.cpp
HDR      := src/container.h src/creatures.h src/states.h src/errors.h
BIN      := isolate

$(BIN): $(SRC) $(HDR)
	$(CXX) $(CXXFLAGS) -o $(BIN) $(SRC) $(LDLIBS)

clean:
	rm -f $(BIN)

.PHONY: clean
