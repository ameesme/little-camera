# Host build of the display rendering code, for previewing UI without hardware.
#
#   make            build ./preview
#   make scenes     render one BMP per scene into out/
#   make open       render scenes and open them (macOS)
#   make clean

CXX      ?= c++
CXXFLAGS ?= -std=c++17 -O1 -g -Wall -Wextra -Wno-unused-parameter
INCLUDES  = -I shim -I .

SRCS = preview.cpp sim_panel.cpp sim_arduino.cpp ../../src/display.cpp
BIN  = preview
OUT  = out

SCENES = splash sleep sleep-content sleep-glum sleep-sad sleep-breath viewfinder capture save slide toast gallery gallery-empty

$(BIN): $(SRCS) shim/Arduino.h shim/SPI.h sim_panel.h ../../src/display.cpp ../../src/display.h ../../src/pbm_header.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SRCS) -o $(BIN)

scenes: $(BIN)
	@mkdir -p $(OUT)
	@for s in $(SCENES); do ./$(BIN) $$s --out $(OUT)/$$s.bmp --scale 2; done

open: scenes
	@open $(OUT)/*.bmp

clean:
	rm -rf $(BIN) $(BIN).dSYM $(OUT)

help:
	@./$(BIN) 2>&1 || true

.PHONY: scenes open clean help
