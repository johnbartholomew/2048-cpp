#define STBI_HEADER_FILE_ONLY
#include "stb_image.c"

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

enum EasingStyle {
	EASE_LINEAR
};

static float clampf(float x, float a, float b) {
	return (x < a ? a : (x > b ? b : x));
}

struct AnimCurve {
	enum { MAX_KEYS = 8 };
	float ky[MAX_KEYS];
	float kt[MAX_KEYS];
	int nkeys;
	// int easing;

	void reset() {
		nkeys = 0;
	}

	void push(float t, float y) {
		assert(nkeys < MAX_KEYS);
		assert(t >= 0.0f && t <= 1.0f);
		assert(nkeys == 0 || t > kt[nkeys-1]);
		ky[nkeys] = y;
		kt[nkeys] = t;
		++nkeys;
	}

	float eval(float at) const {
		if (nkeys == 0) { return 0.0f; }
		if (nkeys == 1) { return ky[0]; }

		at = clampf(at, 0.0f, 1.0f);
		for (int i = 1; i < nkeys; ++i) {
			if (at < kt[i]) {
				float alpha = (at - kt[i-1]) / (kt[i] - kt[i-1]);
				return (1.0f - alpha)*ky[i-1] + alpha*ky[i];
			}
		}
		return ky[nkeys - 1];
	}
};

static void tile_idx_to_xy(int where, float *x, float *y) {
	assert(where >= 0 && where < NUM_TILES);
	assert(x);
	assert(y);
	*x = 128.0f * (where % TILES_X);
	*y = 128.0f * (where / TILES_X);
}

struct TileAnim {
	int value;
	AnimCurve x;
	AnimCurve y;
	AnimCurve scale;
	void reset() {
		value = 15;
		x.reset();
		y.reset();
		scale.reset();
	}
};

struct Board;

struct AnimState {
	TileAnim tiles[NUM_TILES*2];
	int ntiles;
	bool moved;

	bool tiles_changed() const {
		return moved;
	}

	void reset() {
		ntiles = 0;
		moved = false;
	}

	void add_slide(int from, int to, int value) {
		assert(ntiles < NUM_TILES*2);
		assert(to >= 0 && to < NUM_TILES);
		assert(from >= 0 && from < NUM_TILES);

		float x0, y0, x1, y1;
		tile_idx_to_xy(from, &x0, &y0);
		tile_idx_to_xy(to, &x1, &y1);

		TileAnim &tile = tiles[ntiles++];
		tile.reset();
		tile.value = value;
		tile.x.push(0.0f, x0);
		tile.x.push(0.75f, x1);
		tile.y.push(0.0f, y0);
		tile.y.push(0.75f, y1);
		tile.scale.push(0.0f, 1.0f);
	}

	void add_slide_and_vanish(int from, int to, int value) {
		add_slide(from, to, value);
		tiles[ntiles-1].scale.push(0.7f, 1.0f);
		tiles[ntiles-1].scale.push(1.0f, 0.2f);
	}

	void add_pop_tile(int where, int value) {
		assert(ntiles < NUM_TILES*2);
		assert(where >= 0 && where < NUM_TILES);

		float x, y;
		tile_idx_to_xy(where, &x, &y);
		TileAnim &tile = tiles[ntiles++];
		tile.reset();
		tile.value = value;
		tile.x.push(0.0f, x);
		tile.y.push(0.0f, y);
		tile.scale.push(0.0f, 0.0f);
		tile.scale.push(0.4999f, 0.0f);
		tile.scale.push(0.5f, 0.2f);
		tile.scale.push(0.75f, 1.25f);
		tile.scale.push(1.0f, 1.0f);
	}

	void add_place_tile(int where, int value) {
		assert(ntiles < NUM_TILES*2);
		assert(where >= 0 && where < NUM_TILES);

		float x, y;
		tile_idx_to_xy(where, &x, &y);
		TileAnim &tile = tiles[ntiles++];
		tile.reset();
		tile.value = value;
		tile.x.push(0.0f, x);
		tile.y.push(0.0f, y);
		tile.scale.push(0.0f, 0.0f);
		tile.scale.push(0.6999f, 0.0f);
		tile.scale.push(0.7f, 0.2f);
		tile.scale.push(1.0f, 1.0f);
	}

	void merge(int from0, int from1, int to, int old_value) {
		add_slide_and_vanish(from0, to, old_value);
		add_slide_and_vanish(from1, to, old_value);
		add_pop_tile(to, old_value + 1);
		moved = true;
	}

	void slide(int from, int to, int value) {
		add_slide(from, to, value);
		if (from != to) { moved = true; }
	}

	void blank(int /*where*/) {}

	void new_tile(int where, int value) {
		add_place_tile(where, value);
		moved = true;
	}
};

struct Board {
	BoardState state;

	void reset() {
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

	void place(int count, AnimState &anim, RNG &rng) {
		assert(count > 0);
		uint8_t free[NUM_TILES];
		int nfree = count_free(free);
		while (count && nfree) {
			int value = (rng.next_n(10) < 9 ? 1 : 2);
			int which = rng.next_n(nfree);

			state[free[which]] = value;
			anim.new_tile(free[which], value);

			// could do this by swapping the last value into free[which],
			// but that changes the order of slots which means that
			// place(1); place(1); would behave differently to place(2);
			for (int i = which + 1; i < nfree; ++i) { free[i-1] = free[i]; }
			--nfree;
			--count;
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
				if (state[from]) {
					if (last_value) {
						if (last_value == state[from]) {
							anim.merge(last_from, from, to, last_value);
							state[to] = last_value + 1;
							last_value = 0;
						} else {
							anim.slide(last_from, to, last_value);
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
				anim.slide(last_from, to, last_value);
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

	void move(int dir, AnimState &anim, RNG &rng) {
		assert(dir >= 0 && dir < 4);
		anim.reset();
		tilt(DIR_DX[dir], DIR_DY[dir], anim);
		if (anim.tiles_changed()) { place(1, anim, rng); }
	}
};

struct BoardHistory {
	enum { MAX_UNDO = 2048 };
	Board boards[MAX_UNDO];
	RNG rngs[MAX_UNDO];
	int current;
	int undo_avail;
	int redo_avail;

	void clear_history() {
		// retain RNG state
		rngs[0] = rngs[current];
		current = 0;
		undo_avail = 0;
		redo_avail = 0;
		boards[0].reset();
	}

	void reset(uint32_t seed = 0u) {
		clear_history();
		rngs[0].reset(seed);
	}

	void reset(const RNG &initial_state) {
		clear_history();
		rngs[0] = initial_state;
	}

	void new_game(AnimState &anim) {
		clear_history();
		boards[0].place(2, anim, rngs[0]);
	}

	const Board &get() const { return boards[current]; }
	const RNG &get_rng() const { return rngs[current]; }

	void push() {
		if (undo_avail < MAX_UNDO) { ++undo_avail; }
		redo_avail = 0;
		int from = current;
		current = (current + 1) % MAX_UNDO;
		boards[current] = boards[from];
		rngs[current] = rngs[from];
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

#if 0
	void place(int count, AnimState &anim) {
		push();
		boards[current].place(count, anim, rngs[current]);
	}

	void tilt(int dx, int dy, AnimState &anim) {
		push();
		boards[current].tilt(dx, dy, anim);
	}
#endif

	void move(int dir, AnimState &anim) {
		push();
		boards[current].move(dir, anim, rngs[current]);
	}
};

static BoardHistory s_history;
static AnimState s_anim;
static double s_anim_time0;
static double s_anim_time1;
static const double ANIM_TIME = 0.2;

static void handle_key(GLFWwindow * /*wnd*/, int key, int /*scancode*/, int action, int /*mods*/) {
	if (action == GLFW_PRESS) {
		s_anim.reset();

		switch (key) {
			case GLFW_KEY_ESCAPE: { exit(0); } break;
			case GLFW_KEY_RIGHT:  { s_history.move(MOVE_RIGHT, s_anim); } break;
			case GLFW_KEY_LEFT:   { s_history.move(MOVE_LEFT, s_anim); } break;
			case GLFW_KEY_DOWN:   { s_history.move(MOVE_DOWN, s_anim); } break;
			case GLFW_KEY_UP:     { s_history.move(MOVE_UP, s_anim); } break;
			case GLFW_KEY_Z:      { s_history.undo(); } break;
			case GLFW_KEY_X:      { s_history.redo(); } break;
			case GLFW_KEY_N:      { s_history.new_game(s_anim); } break;
		}

		if (s_anim.tiles_changed()) {
			s_anim_time0 = glfwGetTime();
			s_anim_time1 = s_anim_time0 + ANIM_TIME;
		}
	}
}

static void render_anim(float alpha, const Board& /*board*/, const AnimState &anim) {
	for (int i = 0; i < anim.ntiles; ++i) {
		const TileAnim &tile = anim.tiles[i];
		float x = tile.x.eval(alpha) + 64.0f;
		float y = tile.y.eval(alpha) + 64.0f;
		float extent = tile.scale.eval(alpha) * 64.0f;
		const float u = (tile.value % 4) * 0.25f;
		const float v = (tile.value / 4) * 0.25f;
		glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
		glTexCoord2f(u + 0.00f, v + 0.00f); glVertex2f(x - extent, y - extent);
		glTexCoord2f(u + 0.25f, v + 0.00f); glVertex2f(x + extent, y - extent);
		glTexCoord2f(u + 0.25f, v + 0.25f); glVertex2f(x + extent, y + extent);
		glTexCoord2f(u + 0.00f, v + 0.25f); glVertex2f(x - extent, y + extent);
	}
}

static void render_static(const Board &board) {
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
	GLFWwindow *wnd = glfwCreateWindow(700, 700, "2048", NULL, NULL);

	s_history.reset();
	s_anim.reset();

	glfwSetKeyCallback(wnd, &handle_key);

	glfwMakeContextCurrent(wnd);

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

	s_history.new_game(s_anim);
	s_anim_time0 = glfwGetTime();
	s_anim_time1 = s_anim_time0 + ANIM_TIME;

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
