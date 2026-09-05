CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2
SRC      := src/isolate.cpp src/container.cpp
BIN      := isolate

$(BIN): $(SRC) src/container.h src/errors.h
	$(CXX) $(CXXFLAGS) -o $(BIN) $(SRC)

clean:
	rm -f $(BIN)

.PHONY: clean
