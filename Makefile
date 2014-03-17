
CODEGEN_FLAGS := -O0 -g -Wall -Wextra -Wshadow
CFLAGS :=  $(CODEGEN_FLAGS)
CXXFLAGS := -std=c++03 $(CODEGEN_FLAGS)
CPPFLAGS :=

.PHONY: all
all: tiles2048 tiles.png

tiles2048: tiles2048.o stb_image.o gl_core21.o
	g++ $(CXXFLAGS) -o $@ $^ -lglfw -lGL

gl_core21.o: gl_core21.c
	gcc $(CFLAGS) $(CPPFLAGS) -o $@ -c $<

stb_image.o: stb_image.c
	gcc $(CFLAGS) $(CPPFLAGS) -w -o $@ -c $<

tiles2048.o: tiles2048.cpp
	g++ $(CXXFLAGS) $(CPPFLAGS) -o $@ -c $<

tiles.png: tiles.svg
	inkscape --export-png=$@ --export-area-page $<

.PHONY: clean
clean:
	rm -f tiles2048.o stb_image.o gl_core21.o tiles2048 tiles.png
