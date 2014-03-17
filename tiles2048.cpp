#define STBI_HEADER_FILE_ONLY
#include "stb_image.c"

#include "gl_core21.h"
#include <GLFW/glfw3.h>

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <stdint.h>

enum BoardConfig {
	TILES_X = 4,
	TILES_Y = 4,
	NUM_TILES = TILES_X * TILES_Y,
	MAX_POWER = 15
};

// note: if you change this you must change DIR_DX and DIR_DY
enum MoveDir {
	MOVE_LEFT,
	MOVE_RIGHT,
	MOVE_UP,
	MOVE_DOWN
};
// note: depends on order of values in enum MoveDir
static const int DIR_DX[4] = { -1, 1, 0, 0 };
static const int DIR_DY[4] = { 0, 0, -1, 1 };

struct RNG {
	uint32_t x, y, z, w;

	RNG() { reset(); }
	explicit RNG(uint32_t seed) { reset(seed); }
	RNG(const RNG &from): x(from.x), y(from.y), z(from.z), w(from.w) {}
	
	void reset(uint32_t seed = 0u) {
		uint32_t x, y, z, w;
		x = seed ? seed : 123456789u;
		y = x^(x<<13); y ^= (y >> 17); y ^= (y << 5);
		z = y^(y<<13); z ^= (z >> 17); z ^= (z << 5);
		w = z^(z<<13); w ^= (w >> 17); w ^= (w << 5);
	}

	uint32_t next32() {
		uint32_t t = x^(x<<15); t = (w^(w>>21)) ^ (t^(t>>4));
		x = y; y = z; z = w; w = t;
		return t;
	}

	uint64_t next64() {
		const uint64_t a = next32();
		const uint64_t b = next32();
		return (a << 32) | b;
	}

	int next_n(int n) {
		// see: http://www.azillionmonkeys.com/qed/random.html
		assert(n > 0);
		const uint32_t range = UINT32_MAX - (UINT32_MAX % n);
		uint32_t x;
		do { x = next32(); } while (x >= range);
		x = (x / ((range - 1) / (uint32_t)n + 1));
		assert(x < (uint32_t)n);
		return x;
	}
};

struct Board {
	RNG rng;
	uint8_t board[NUM_TILES];

	void reset(uint32_t seed = 0u) {
		rng.reset(seed);
		memset(board, 0, sizeof(board));
	}

	void place() {
		uint8_t free[NUM_TILES];
		int nfree = 0;
		for (int i = 0; i < NUM_TILES; ++i) { if (board[i] == 0) { free[nfree++] = i; } }
		assert(nfree > 0);
		int value = (rng.next_n(10) < 9 ? 1 : 2);
		int which = rng.next_n(nfree);
		board[free[which]] = value;
	}

	void tilt(int dx, int dy) {
		assert((dx && !dy) || (dy && !dx));

		int begin = ((dx | dy) > 0 ? NUM_TILES - 1 : 0);
		int step_major = -(dx*TILES_X + dy);
		int step_minor = -(dy*TILES_X + dx);
		int n = (dx ? TILES_Y : TILES_X);
		int m = (dx ? TILES_X : TILES_Y);

		for (int i = 0; i < n; ++i) {
			int stop = begin + m*step_minor;
			int from = begin, to = begin;

			int last_value = 0;
			while (from != stop) {
#if 0
				printf("%d : [%d,%d] %d -> [%d,%d] %d\n",
						last_value, from % TILES_X,from / TILES_X, board[from],
						to % TILES_X, to / TILES_X, board[to]);
#endif
				if (board[from]) {
					if (last_value) {
						if (last_value == board[from]) {
							board[to] = last_value + 1;
							last_value = 0;
						} else {
							int tmp = board[from];
							board[to] = last_value;
							last_value = tmp;
						}
						to += step_minor;
					} else {
						last_value = board[from];
					}
				}
				from += step_minor;
			}
			if (last_value) {
				board[to] = last_value;
				to += step_minor;
			}
			while (to != stop) {
				board[to] = 0;
				to += step_minor;
			}

			begin += step_major;
		}
	}

	void move(int dir) {
		assert(dir >= 0 && dir < 4);
		tilt(DIR_DX[dir], DIR_DY[dir]);
	}
};

struct BoardHistory {
	enum { MAX_UNDO = 2048 };
	Board boards[MAX_UNDO];
	int current;
	int undo_avail;
	int redo_avail;

	void reset(uint32_t seed = 0u) {
		current = 0;
		undo_avail = 0;
		redo_avail = 0;
		boards[0].reset(seed);
	}

	const Board &get() const { return boards[current]; }
	Board &get() { return boards[current]; }

	Board &push() {
		if (undo_avail < MAX_UNDO) { ++undo_avail; }
		redo_avail = 0;
		int from = current;
		current = (current + 1) % MAX_UNDO;
		memcpy(&boards[current], &boards[from], sizeof(Board));
		return boards[current];
	}

	Board &undo() {
		if (undo_avail) {
			--undo_avail;
			++redo_avail;
			current = (current + MAX_UNDO - 1) % MAX_UNDO;
		}
		return boards[current];
	}

	Board &redo() {
		if (redo_avail) {
			--redo_avail;
			++undo_avail;
			current = (current + 1) % MAX_UNDO;
		}
		return boards[current];
	}

	void place() {
		push().place();
	}

	void tilt(int dx, int dy) {
		push().tilt(dx, dy);
	}

	void move(int dir) {
		push().move(dir);
	}
};

static BoardHistory s_history;

static void handle_key(GLFWwindow *wnd, int key, int scancode, int action, int mods) {
	if (action == GLFW_PRESS) {
		switch (key) {
			case GLFW_KEY_ESCAPE: { exit(0); } break;
			case GLFW_KEY_SPACE: { s_history.place(); } break;
			case GLFW_KEY_RIGHT: { s_history.move(MOVE_RIGHT); } break;
			case GLFW_KEY_LEFT:  { s_history.move(MOVE_LEFT); } break;
			case GLFW_KEY_DOWN:  { s_history.move(MOVE_DOWN); } break;
			case GLFW_KEY_UP:    { s_history.move(MOVE_UP); } break;
			case GLFW_KEY_Z:     { s_history.undo(); } break;
			case GLFW_KEY_X:     { s_history.redo(); } break;
		}
	}
}

int main(int argc, char **argv) {
	glfwInit();
	GLFWwindow *wnd = glfwCreateWindow(600, 768, "2048", NULL, NULL);

	s_history.reset();

	glfwSetKeyCallback(wnd, &handle_key);

	glfwMakeContextCurrent(wnd);
	ogl_LoadFunctions();

	int tiles_tex_w, tiles_tex_h;
	uint8_t *tiles_tex_data = stbi_load("tiles.png", &tiles_tex_w, &tiles_tex_h, 0, 3);
	GLuint tex_id;
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glGenTextures(1, &tex_id);
	glBindTexture(GL_TEXTURE_2D, tex_id);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, tiles_tex_w, tiles_tex_h, 0, GL_RGB, GL_UNSIGNED_BYTE, tiles_tex_data);
	glEnable(GL_TEXTURE_2D);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);

	float inv_tex_w = 1.0f / tiles_tex_w;
	float inv_tex_h = 1.0f / tiles_tex_h;

	while (!glfwWindowShouldClose(wnd)) {
		glfwWaitEvents();
		glClear(GL_COLOR_BUFFER_BIT);

		int wnd_w, wnd_h;
		glfwGetFramebufferSize(wnd, &wnd_w, &wnd_h);
		glViewport(0, 0, wnd_w, wnd_h);
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glOrtho(0.0, (double)wnd_w, 0.0, (double)wnd_h, -1.0, 1.0);
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
		glTranslatef((float)wnd_w * 0.5f - 256.0f, (float)wnd_h * 0.5f - 256.0f, 0.0f);

		glBegin(GL_QUADS);
		for (int i = 0; i < 4; ++i) {
			for (int j = 0; j < 4; ++j) {
				float y = (3 - i) * 128.0f;
				float x = j * 128.0f;
				int value = s_history.get().board[i*4+j];
				float u = (value % 4) * 0.25f;
				float v = (value / 4) * 0.25f;
				glTexCoord2f(u + 0.00f, v + 0.25f); glVertex2f(x, y);
				glTexCoord2f(u + 0.25f, v + 0.25f); glVertex2f(x+128.0f, y);
				glTexCoord2f(u + 0.25f, v + 0.00f); glVertex2f(x+128.0f, y+128.0f);
				glTexCoord2f(u + 0.00f, v + 0.00f); glVertex2f(x, y+128.0f);
			}
		}
		glEnd();

		glfwSwapBuffers(wnd);
	}
	glfwTerminate();
	return 0;
}

// vim: set ts=4 sw=4 noet:
