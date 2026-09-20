# Renders chirps from src/chirp.h to WAV for auditioning without hardware.
#
#   make bands        # out/band-{sad,glum,content,happy}.wav + out/ladder.wav
#   make play         # macOS: afplay them in order
#   ./chirp --happiness 40 --count 6 --seed 99

CXX      ?= c++
CXXFLAGS ?= -std=c++17 -O1 -g -Wall -Wextra -Wno-unused-parameter

chirp: chirp.cpp ../../src/chirp.h
	$(CXX) $(CXXFLAGS) chirp.cpp -o chirp

bands: chirp
	./chirp --bands --ladder

play: bands
	@for f in out/band-happy.wav out/band-content.wav out/band-glum.wav out/band-sad.wav out/ladder.wav; do \
	  echo $$f; afplay $$f; done

clean:
	rm -rf chirp chirp.dSYM out

.PHONY: bands play clean
