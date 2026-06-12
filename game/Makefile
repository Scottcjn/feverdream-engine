# fd-game — the MIT side of the firewall. Links SDL2 only: NO POV-Ray headers,
# NO POV archives. It talks to fd-daemon over the PROTOCOL.md socket.
CXXFLAGS := -O2 -Wall -pipe

fd-game: fd-game.cpp
	$(CXX) $(CXXFLAGS) -std=c++11 -o $@ fd-game.cpp -lSDL2 -lm

all: fd-game

clean:
	rm -f fd-game *.ppm
