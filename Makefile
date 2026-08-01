CXX ?= clang++
CXXFLAGS ?= -O3 -std=c++17 -Wall -Wextra -pthread

TARGET = false_sharing_demo
SRC = false_sharing_demo.cpp

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)

run: $(TARGET)
	./$(TARGET) --test all --threads 4 --iterations 300000000

sweep: $(TARGET)
	@echo "=== Running Multi-Thread Scaling Sweep ==="
	@for t in 1 2 4 8 16; do \
		echo ""; \
		echo "--------------------------------------------------------"; \
		echo " Testing with $$t Threads (100M iterations/thread)"; \
		echo "--------------------------------------------------------"; \
		./$(TARGET) --test all --threads $$t --iterations 100000000; \
	done

.PHONY: all clean run sweep
