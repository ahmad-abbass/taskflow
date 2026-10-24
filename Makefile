CXX ?= g++
CPPFLAGS := -Iinclude
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror
SOURCES := src/actions.cpp src/scheduler.cpp src/snapshot.cpp

.PHONY: all test sanitize clean demo
all: build/taskflow

build:
	mkdir -p build

build/taskflow: $(SOURCES) src/main.cpp $(shell find include -name '*.hpp') | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SOURCES) src/main.cpp -o $@

build/tests: $(SOURCES) tests/tests.cpp tests/test_support.hpp $(shell find include -name '*.hpp') | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SOURCES) tests/tests.cpp -o $@

test: build/tests build/taskflow
	./build/tests
	python3 tests/cli_test.py ./build/taskflow

sanitize: | build
	$(CXX) $(CPPFLAGS) -std=c++17 -O1 -g -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer $(SOURCES) tests/tests.cpp -o build/tests-sanitize
	./build/tests-sanitize

demo: build/taskflow
	./build/taskflow < examples/first-session.txt

clean:
	rm -rf build
