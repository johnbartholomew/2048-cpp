
CODEGEN_FLAGS := -O2 -g -flto -Wall -Wextra
CFLAGS :=  $(CODEGEN_FLAGS)
CXXFLAGS := -std=c++03 $(CODEGEN_FLAGS)
CPPFLAGS :=

.PHONY: all
all: tiles2048

tiles2048: tiles2048.o glfontstash.o
	g++ $(CXXFLAGS) -o $@ $^ -lfreetype -lglfw -lGL

glfontstash.o: glfontstash.c | fontstash.h glfontstash.h
	gcc $(CFLAGS) $(CPPFLAGS) \
	  -isystem /usr/include/freetype2 -DFONS_USE_FREETYPE \
	  -w -o $@ -c $<

tiles2048.o: tiles2048.cpp
	g++ $(CXXFLAGS) $(CPPFLAGS) -o $@ -c $<

.PHONY: clean fullclean
clean:
	rm -f tiles2048.o glfontstash.o tiles2048
