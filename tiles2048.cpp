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

	void reset(uint32_t seed = 0u) {
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
		uint32_t value;
		do { value = next32(); } while (value >= range);
		value = (value / ((range - 1) / (uint32_t)n + 1));
		assert(value < (uint32_t)n);
		return value;
	}
};

typedef uint8_t BoardState[NUM_TILES];

#define PRINT_ANIM 0

struct Slide {
	uint16_t from;
	uint16_t to;
};

struct Board;
struct TileAnimState;

struct AnimState {
	enum TileStatus {
		TILE_BLANK     = 0,
		TILE_SLIDE_DST = (1 << 0),
		TILE_SLIDE_SRC = (1 << 1),
		TILE_MERGE     = (1 << 2),
		TILE_NEW       = (1 << 3),

		TILE_STATIONARY = (TILE_SLIDE_DST | TILE_SLIDE_SRC)
	};
	uint8_t status[NUM_TILES];
	Slide slides[NUM_TILES];
	int new_tiles[NUM_TILES];
	int num_slides;
	int num_new_tiles;

	int evaluate(float alpha, const Board &board, TileAnimState *tiles) const;

	bool tiles_changed() const {
		return (num_new_tiles | num_slides);
	}

	void reset() {
		num_slides = 0;
		num_new_tiles = 0;
		memset(status, 0, sizeof(status));
	}

	void add_slide(int from, int to) {
		assert(num_slides < NUM_TILES);
		assert(to >= 0 && to < NUM_TILES);
		assert(from >= 0 && from < NUM_TILES);
		slides[num_slides].from = from;
		slides[num_slides].to = to;
		++num_slides;
	}

	void add_new_tile(int where) {
		assert(num_new_tiles < NUM_TILES);
		assert(where >= 0 && where < NUM_TILES);
		new_tiles[num_new_tiles] = where;
		++num_new_tiles;
	}

	void merge(int from, int to) {
		if (from != to) { add_slide(from, to); }
		if (!status[to]) { add_new_tile(to); }
		status[from] |= TILE_SLIDE_SRC;
		status[to] |= TILE_SLIDE_DST;
		status[to] |= TILE_MERGE;
#if PRINT_ANIM
		printf("merge %d,%d -> %d,%d\n",
				from % TILES_X, from / TILES_X,
				to % TILES_X, to / TILES_X);
#endif
	}

	void slide(int from, int to) {
		if (from != to) { add_slide(from, to); }
		status[from] |= TILE_SLIDE_SRC;
		status[to] |= TILE_SLIDE_DST;
#if PRINT_ANIM
		printf("slide %d,%d -> %d,%d\n",
				from % TILES_X, from / TILES_X,
				to % TILES_X, to / TILES_X);
#endif
	}

	void blank(int /*where*/) {}

	void new_tile(int where) {
		add_new_tile(where);
		status[where] |= TILE_NEW;
#if PRINT_ANIM
		printf("new tile @ %d,%d\n", where % TILES_X, where / TILES_X);
#endif
	}
};

struct Board {
	RNG rng;
	BoardState state;

	void reset(uint32_t seed = 0u) {
		rng.reset(seed);
		memset(&state, 0, sizeof(state));
	}

	int count_free(uint8_t *free = 0) const {
		int nfree = 0;
		for (int i = 0; i < NUM_TILES; ++i) {
			if (free && (state[i] == 0)) { free[nfree] = i; }
			nfree += (state[i] == 0);
		}
		assert(nfree >= 0);
		return nfree;
	}

	bool has_direct_matches() const {
		/* check rows */
		for (int i = 0; i < TILES_Y; ++i) {
			const uint8_t *at = (state + i*TILES_X);
			for (int j = 1; j < TILES_X; ++j) {
				if (at[0] && (at[0] == at[1])) { return true; }
				++at;
			}
		}

		/* check columns */
		for (int j = 0; j < TILES_X; ++j) {
			const uint8_t *at = (state + j);
			for (int i = 1; i < TILES_Y; ++i) {
				if (at[0] && (at[0] == at[TILES_X])) { return true; }
				at += TILES_X;
			}
		}
		return false;
	}

	bool finished() const {
		return (count_free() == 0 && !has_direct_matches());
	}

	void place(AnimState &anim) {
		uint8_t free[NUM_TILES];
		int nfree = count_free(free);
		if (nfree) {
			int value = (rng.next_n(10) < 9 ? 1 : 2);
			int which = rng.next_n(nfree);
			state[free[which]] = value;
			anim.new_tile(free[which]);
		}
	}

	void tilt(int dx, int dy, AnimState &anim) {
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
			int last_from = from;
			while (from != stop) {
#if 0
				printf("%d : [%d,%d] %d -> [%d,%d] %d\n",
						last_value, from % TILES_X,from / TILES_X, state[from],
						to % TILES_X, to / TILES_X, state[to]);
#endif
				if (state[from]) {
					if (last_value) {
						if (last_value == state[from]) {
							anim.merge(last_from, to);
							anim.merge(from, to);
							state[to] = last_value + 1;
							last_value = 0;
						} else {
							anim.slide(last_from, to);
							int tmp = state[from];
							state[to] = last_value;
							last_value = tmp;
							last_from = from;
						}
						to += step_minor;
					} else {
						last_value = state[from];
						last_from = from;
					}
				}
				from += step_minor;
			}
			if (last_value) {
				anim.slide(last_from, to);
				state[to] = last_value;
				to += step_minor;
			}
			while (to != stop) {
				anim.blank(to);
				state[to] = 0;
				to += step_minor;
			}

			begin += step_major;
		}
	}

	void move(int dir, AnimState &anim) {
		assert(dir >= 0 && dir < 4);
		anim.reset();
		tilt(DIR_DX[dir], DIR_DY[dir], anim);
		if (anim.tiles_changed()) { place(anim); }
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

	void place(AnimState &anim) {
		push().place(anim);
	}

	void tilt(int dx, int dy, AnimState &anim) {
		push().tilt(dx, dy, anim);
	}

	void move(int dir, AnimState &anim) {
		push().move(dir, anim);
	}
};

static BoardHistory s_history;
static AnimState s_anim;
static double s_anim_time0;
static double s_anim_time1;
static const double ANIM_TIME = 0.25;

static void handle_key(GLFWwindow * /*wnd*/, int key, int /*scancode*/, int action, int /*mods*/) {
	if (action == GLFW_PRESS) {
		s_anim.reset();

		switch (key) {
			case GLFW_KEY_ESCAPE: { exit(0); } break;
			case GLFW_KEY_SPACE:  { s_history.place(s_anim); } break;
			case GLFW_KEY_RIGHT:  { s_history.move(MOVE_RIGHT, s_anim); } break;
			case GLFW_KEY_LEFT:   { s_history.move(MOVE_LEFT, s_anim); } break;
			case GLFW_KEY_DOWN:   { s_history.move(MOVE_DOWN, s_anim); } break;
			case GLFW_KEY_UP:     { s_history.move(MOVE_UP, s_anim); } break;
			case GLFW_KEY_Z:      { s_history.undo(); } break;
			case GLFW_KEY_X:      { s_history.redo(); } break;
			case GLFW_KEY_R:      { s_history.reset(); } break;
		}

		if (s_anim.tiles_changed()) {
			s_anim_time0 = glfwGetTime();
			s_anim_time1 = s_anim_time0 + ANIM_TIME;
		}
	}
}

struct TileAnimState {
	int value;
	float x;
	float y;
	float scale;
};

float clamp(float x, float a, float b) {
	return (x < a ? a : (x > b ? b : x));
}

int AnimState::evaluate(float alpha, const Board &board, TileAnimState *tiles) const {
	assert(tiles);
	int ntiles = 0;
	for (int i = 0; i < 4; ++i) {
		for (int j = 0; j < 4; ++j) {
			TileAnimState &tile = tiles[ntiles++];
			tile.value = status[i*TILES_X + j];
			tile.x = j * 128.0f;
			tile.y = i * 128.0f;
			tile.scale = 1.0f;
		}
	}
#if 0
	int ntiles = 0;
	for (int i = 0; i < 4; ++i) {
		for (int j = 0; j < 4; ++j) {
			const uint8_t flags = status[i*TILES_X + j];
			int value = board.state[i*4+j];
			if (flags != TILE_STATIONARY) { continue; }
			// if (!value) { continue; }
			TileAnimState &tile = tiles[ntiles++];
			tile.value = value;
			tile.x = j * 128.0f;
			tile.y = i * 128.0f;
			tile.scale = 1.0f;
		}
	}

	const float motion_alpha = clamp(alpha * (1.0f / 0.75f), 0.0f, 1.0f);
	const float pop_alpha = clamp((alpha - 0.75f) * (1.0f / 0.25f), 0.0f, 1.0f);

	for (int i = 0; i < num_slides; ++i) {
		const Slide &slide = slides[i];
		assert(slide.to != slide.from);

		if ((status[slide.to] & TILE_MERGE) != 0 && (motion_alpha == 1.0f)) {
			continue;
		}

		const float x0 = 128.0f * (slide.from % TILES_X);
		const float y0 = 128.0f * (slide.from / TILES_X);
		const float x1 = 128.0f * (slide.to % TILES_X);
		const float y1 = 128.0f * (slide.to / TILES_X);

		TileAnimState &tile = tiles[ntiles++];
		if (status[slide.to] & TILE_MERGE) {
			tile.value = board.state[slide.to] - 1;
		} else {
			tile.value = board.state[slide.to];
		}
		const float inv_alpha = 1.0f - motion_alpha;
		tile.x = inv_alpha*x0 + motion_alpha*x1;
		tile.y = inv_alpha*y0 + motion_alpha*y1;
		tile.scale = 1.0f;
	}

	if (motion_alpha == 1.0f) {
		for (int i = 0; i < num_new_tiles; ++i) {
			const int where = new_tiles[i];
			TileAnimState &tile = tiles[ntiles++];
			tile.value = board.state[where];
			tile.x = 128.0f * (where % TILES_X);
			tile.y = 128.0f * (where / TILES_X);
			if (pop_alpha <= 0.5f) {
				tile.scale = pop_alpha * 2.4f;
			} else {
				tile.scale = 1.4f - pop_alpha * 0.4f;
			}
		}
	}

#endif
	assert(ntiles < NUM_TILES*2);

	return ntiles;
}

static void render_anim(float alpha, const Board &board, const AnimState &anim) {
	TileAnimState tiles[NUM_TILES * 2];
	const int ntiles = anim.evaluate(alpha, board, tiles);

	for (int i = 0; i < ntiles; ++i) {
		const TileAnimState &tile = tiles[i];
		const float u = (tile.value % 4) * 0.25f;
		const float v = (tile.value / 4) * 0.25f;
		glColor4f(1.0f, 1.0f, 1.0f, tile.scale);
		glTexCoord2f(u + 0.00f, v + 0.00f); glVertex2f(tile.x, tile.y);
		glTexCoord2f(u + 0.25f, v + 0.00f); glVertex2f(tile.x + 128.0f, tile.y);
		glTexCoord2f(u + 0.25f, v + 0.25f); glVertex2f(tile.x + 128.0f, tile.y + 128.0f);
		glTexCoord2f(u + 0.00f, v + 0.25f); glVertex2f(tile.x, tile.y + 128.0f);
	}
}

static void render_static(const Board &board) {
	printf("render static\n");
	for (int i = 0; i < NUM_TILES; ++i) {
		const int value = board.state[i];
		if (value == 0) { continue; }
		const float x = 128.0f * (i % TILES_X);
		const float y = 128.0f * (i / TILES_X);
		const float u = (value % 4) * 0.25f;
		const float v = (value / 4) * 0.25f;
		glTexCoord2f(u + 0.00f, v + 0.00f); glVertex2f(x, y);
		glTexCoord2f(u + 0.25f, v + 0.00f); glVertex2f(x + 128.0f, y);
		glTexCoord2f(u + 0.25f, v + 0.25f); glVertex2f(x + 128.0f, y + 128.0f);
		glTexCoord2f(u + 0.00f, v + 0.25f); glVertex2f(x, y + 128.0f);
	}
}

int main(int /*argc*/, char** /*argv*/) {
	glfwInit();
	GLFWwindow *wnd = glfwCreateWindow(600, 768, "2048", NULL, NULL);

	s_history.reset();
	s_anim.reset();

	glfwSetKeyCallback(wnd, &handle_key);

	glfwMakeContextCurrent(wnd);
	ogl_LoadFunctions();

	int tiles_tex_w, tiles_tex_h;
	uint8_t *tiles_tex_data = stbi_load("tiles.png", &tiles_tex_w, &tiles_tex_h, 0, 4);
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
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, tiles_tex_w, tiles_tex_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, tiles_tex_data);
	glEnable(GL_TEXTURE_2D);
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glClearColor(250/255.0f, 248/255.0f, 239/255.0f, 1.0f);

	s_anim_time0 = s_anim_time1 = glfwGetTime();

	while (!glfwWindowShouldClose(wnd)) {
		double t = glfwGetTime();
		float alpha = (t - s_anim_time0) / ANIM_TIME;
		assert(alpha >= 0.0f);
		if (alpha > 1.0f) { alpha = 1.0f; }

		glClear(GL_COLOR_BUFFER_BIT);

		int wnd_w, wnd_h;
		glfwGetFramebufferSize(wnd, &wnd_w, &wnd_h);
		glViewport(0, 0, wnd_w, wnd_h);
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glOrtho(0.0, (double)wnd_w, (double)wnd_h, 0.0, -1.0, 1.0);
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
		glTranslatef((float)wnd_w * 0.5f - 256.0f, (float)wnd_h * 0.5f - 256.0f, 0.0f);

		glDisable(GL_TEXTURE_2D);
		glColor4ub(187, 173, 160, 255);
		glBegin(GL_QUADS);
		glVertex2f(-16.0f, 528.0f);
		glVertex2f(528.0f, 528.0f);
		glVertex2f(528.0f, -16.0f);
		glVertex2f(-16.0f, -16.0f);
		glEnd();

		glEnable(GL_TEXTURE_2D);
		glColor4ub(255, 255, 255, 255);
		glBegin(GL_QUADS);
		if (alpha < 1.0) {
			render_anim(alpha, s_history.get(), s_anim);
		} else {
			render_static(s_history.get());
		}
		glEnd();

		glfwSwapBuffers(wnd);

		// if we're not animating then be nice and don't spam the CPU & GPU
		if (t >= s_anim_time1) { glfwWaitEvents(); } else { glfwPollEvents(); }
	}
	glfwTerminate();
	return 0;
}

// vim: set ts=4 sw=4 noet:
