
CODEGEN_FLAGS := -O1 -g -Wall -Wextra -pthread
CFLAGS :=  $(CODEGEN_FLAGS)
CXXFLAGS := -std=c++03 $(CODEGEN_FLAGS)
CPPFLAGS :=

.PHONY: all
all: tiles2048

tiles2048: tiles2048.o glfontstash.o tinythread.o
	g++ $(CXXFLAGS) -o $@ $^ -lfreetype -lglfw -lGL

glfontstash.o: glfontstash.c | fontstash.h glfontstash.h
	gcc $(CFLAGS) $(CPPFLAGS) \
	  -isystem /usr/include/freetype2 -DFONS_USE_FREETYPE \
	  -w -o $@ -c $<

tinythread.o: tinythread.cpp | tinythread.h
	g++ $(CXXFLAGS) -o $@ -c $^

tiles2048.o: tiles2048.cpp | glfontstash.h fontstash.h tinythread.h
	g++ $(CXXFLAGS) $(CPPFLAGS) -o $@ -c $<

.PHONY: clean fullclean
clean:
	rm -f tiles2048.o tinythread.o glfontstash.o tiles2048
