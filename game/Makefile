# fd-game — the MIT side of the firewall. Links SDL2 + Lua 5.4 only: NO
# POV-Ray headers, NO POV archives. It talks to fd-daemon over the
# PROTOCOL.md socket; the world + gameplay tuning live in arena.lua.
CXXFLAGS := -O2 -Wall -pipe $(shell pkg-config --cflags lua5.4)
LUALIBS  := $(shell pkg-config --libs lua5.4)

fd-game: fd-game.cpp fd_audio.h
	$(CXX) $(CXXFLAGS) -std=c++11 -o $@ fd-game.cpp -lSDL2 $(LUALIBS) -ldl -lm

# optional GPU post (CUDA, local RTX 4070) — dlopened at runtime via FD_GPU=1;
# fd-game builds and runs fine without it
libfdpost.so: fd_post.cu
	nvcc -O3 -shared -Xcompiler -fPIC fd_post.cu -o $@

gpu: libfdpost.so

all: fd-game

clean:
	rm -f fd-game libfdpost.so *.ppm
