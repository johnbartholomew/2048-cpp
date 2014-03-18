
CODEGEN_FLAGS := -Os -flto -Wall -Wextra
CFLAGS :=  $(CODEGEN_FLAGS)
CXXFLAGS := -std=c++03 $(CODEGEN_FLAGS)
CPPFLAGS :=

.PHONY: all
all: tiles2048 tiles.png

tiles2048: tiles2048.o stb_image.o
	g++ $(CXXFLAGS) -o $@ $^ -lglfw -lGL

stb_image.o: stb_image.c
	gcc $(CFLAGS) $(CPPFLAGS) -w -o $@ -c $<

tiles2048.o: tiles2048.cpp
	g++ $(CXXFLAGS) $(CPPFLAGS) -o $@ -c $<

tiles.png: tiles.svg
	inkscape --export-png=$@ --export-area-page $<
	optipng -clobber -strip all -i 0 $@

.PHONY: clean fullclean
clean:
	rm -f tiles2048.o stb_image.o tiles2048
fullclean: clean
	rm -f tiles.png
