CXX      := c++
CXXFLAGS := -std=c++17 -Wall -Wextra -DMESHTASTIC_UI_MSG -I../../src -I.
SRC_ROOT := ../..

UNITY_URL := https://raw.githubusercontent.com/ThrowTheSwitch/Unity/v2.6.0/src
UNITY_FILES := unity.c unity.h unity_internals.h

# ConvModel tests (Phase 1-3 / journey tests)
CONV_TARGET := run_tests

# UIState state-machine tests (Phase 4)
STATE_TARGET := run_state_tests

.PHONY: all test clean

all: test

test: $(CONV_TARGET) $(STATE_TARGET)
	./$(CONV_TARGET)
	./$(STATE_TARGET)

# unity.c must be compiled as C; C++ sources compiled by CXX
unity.o: unity.h unity_internals.h
	$(CC) -c unity.c -o unity.o

$(CONV_TARGET): unity.o
	$(CXX) $(CXXFLAGS) \
		$(SRC_ROOT)/src/graphics/msgui/ConvModel.cpp \
		test_main.cpp unity.o -o $@

$(STATE_TARGET): unity.o
	$(CXX) $(CXXFLAGS) \
		$(SRC_ROOT)/src/graphics/msgui/ConvModel.cpp \
		$(SRC_ROOT)/src/graphics/msgui/UIState.cpp \
		test_state.cpp unity.o -o $@

# Download Unity source on first use
unity.c unity.h unity_internals.h:
	@echo "Downloading Unity test framework..."
	curl -sSL $(UNITY_URL)/unity.c            -o unity.c
	curl -sSL $(UNITY_URL)/unity.h            -o unity.h
	curl -sSL $(UNITY_URL)/unity_internals.h  -o unity_internals.h

clean:
	rm -f $(CONV_TARGET) $(STATE_TARGET) unity.c unity.h unity_internals.h *.o
