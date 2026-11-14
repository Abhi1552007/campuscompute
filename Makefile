# CampusCompute - Makefile
# Build:   make            Run tests:  make test
# Clean:   make clean      Package:    make dist   (creates campuscompute_src.zip)

CC      := gcc
CFLAGS  := -Wall -Wextra -std=gnu11 -O2 -g -Iinclude
LDLIBS  := -pthread -lrt
TARGET  := campuscompute

SRC     := $(wildcard src/*.c)
OBJ     := $(patsubst src/%.c,build/%.o,$(SRC))
DEPS    := $(wildcard include/*.h)

.PHONY: all clean test dist

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

build/%.o: src/%.c $(DEPS) | build
	$(CC) $(CFLAGS) -c $< -o $@

build:
	mkdir -p build

# Quick smoke test of every command (output goes to the terminal)
test: $(TARGET)
	./$(TARGET) help
	./$(TARGET) fcfs tests/jobs.txt
	./$(TARGET) rr tests/jobs.txt -q 2
	./$(TARGET) priority tests/jobs.txt
	./$(TARGET) compare tests/jobs.txt
	./$(TARGET) banker tests/safe.txt
	./$(TARGET) banker tests/unsafe.txt
	./$(TARGET) logger 3 5
	./$(TARGET) ipc 5 3 8 6 2
	./$(TARGET) worker 3
	./$(TARGET) worker 4 2 exit
	./$(TARGET) rlimit all
	./$(TARGET) ipc-status

dist: clean
	rm -f campuscompute_src.zip
	zip -r campuscompute_src.zip Makefile README.md TEAM_GUIDE.md include src tests
	unzip -t campuscompute_src.zip

clean:
	rm -rf build $(TARGET) campuscompute.log
