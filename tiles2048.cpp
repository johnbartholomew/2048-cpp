#define STBI_HEADER_FILE_ONLY
#include "stb_image.c"

#define CRAZY_VERBOSE_CACHE_DEBUGGER 0
#define USE_CACHE_VERIFICATION_MAP 0
#define PRINT_BOARD_STATE 0
#define PRINT_CACHE_STATS 0

#include <GLFW/glfw3.h>

#if USE_CACHE_VERIFICATION_MAP
#include <map>
#endif

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <climits>
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
		assert(nfree >= 0 && nfree <= NUM_TILES);
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

	void place(int count, AnimState *anim, RNG &rng) {
		assert(count > 0);
		uint8_t free[NUM_TILES];
		int nfree = count_free(free);
		while (count && nfree) {
			int value = (rng.next_n(10) < 9 ? 1 : 2);
			int which = rng.next_n(nfree);
			assert(which >= 0 && which < nfree);

			state[free[which]] = value;
			if (anim) { anim->new_tile(free[which], value); }

			// could do this by swapping the last value into free[which],
			// but that changes the order of slots which means that
			// place(1); place(1); would behave differently to place(2);
			for (int i = which + 1; i < nfree; ++i) {
				assert(i < NUM_TILES);
				free[i-1] = free[i];
			}
			--nfree;
			--count;
		}
	}

	bool tilt(int dx, int dy, AnimState *anim) {
		assert((dx && !dy) || (dy && !dx));

		int begin = ((dx | dy) > 0 ? NUM_TILES - 1 : 0);
		int step_major = -(dx*TILES_X + dy);
		int step_minor = -(dy*TILES_X + dx);
		int n = (dx ? TILES_Y : TILES_X);
		int m = (dx ? TILES_X : TILES_Y);

		bool moved = false;

		for (int i = 0; i < n; ++i) {
			int stop = begin + m*step_minor;
			int from = begin, to = begin;

			int last_value = 0;
			int last_from = from;
			while (from != stop) {
				if (state[from]) {
					if (last_value) {
						if (last_value == state[from]) {
							if (anim) { anim->merge(last_from, from, to, last_value); }
							moved = true;
							state[to] = last_value + 1;
							last_value = 0;
						} else {
							if (anim) { anim->slide(last_from, to, last_value); }
							if (last_from != to) { moved = true; }
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
				if (anim) { anim->slide(last_from, to, last_value); }
				if (last_from != to) { moved = true; }
				state[to] = last_value;
				to += step_minor;
			}
			while (to != stop) {
				if (anim) { anim->blank(to); }
				state[to] = 0;
				to += step_minor;
			}

			begin += step_major;
		}

		return moved;
	}

	bool move(int dir, AnimState *anim, RNG &rng) {
		assert(dir >= 0 && dir < 4);
		if (anim) { anim->reset(); }
		bool moved = tilt(DIR_DX[dir], DIR_DY[dir], anim);
		if (moved) { place(1, anim, rng); }
		return moved;
	}
};

struct BoardHistory {
	enum { MAX_UNDO = 4096 };
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

	void reset(const Board &board, const RNG &initial_state) {
		clear_history();
		rngs[0] = initial_state;
		boards[0] = board;
	}

	void new_game(AnimState &anim) {
		clear_history();
		boards[0].place(2, &anim, rngs[0]);
	}

	const Board &get() const { return boards[current]; }
	const RNG &get_rng() const { return rngs[current]; }

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

	void move(int dir, AnimState &anim) {
		Board next_state = boards[current];
		RNG next_rng = rngs[current];
		bool moved = next_state.move(dir, &anim, next_rng);

		if (moved) {
			current = (current + 1) % MAX_UNDO;
			boards[current] = next_state;
			rngs[current] = next_rng;
			if (undo_avail < MAX_UNDO) { ++undo_avail; }
			redo_avail = 0;
		}
	}
};

static uint64_t pack_board_state(const Board &board) {
	uint64_t k = 0u;
	assert(NUM_TILES == 16);
	for (int i = 0; i < NUM_TILES; ++i) {
		assert(board.state[i] < 16);
		k = (k << 4) | board.state[i];
	}
	return k;
}

static void unpack_board_state(Board &board, const uint64_t state) {
	assert(NUM_TILES == 16);
	uint64_t k = state;
	for (int i = 0; i < 16; ++i) {
		board.state[15 - i] = (k & 0x0F);
		k >>= 4;
	}
}

static uint64_t mix64(uint64_t key) {
	// from: https://gist.github.com/badboy/6267743
	key = (~key) + (key << 21); // key = (key << 21) - key - 1;
	key = key ^ (key >> 24);
	key = key * 265;
	key = key ^ (key >> 14);
	key = key * 21;
	key = key ^ (key >> 28);
	key = key + (key << 31);
	return key;
}

#if CRAZY_VERBOSE_CACHE_DEBUGGER
template <typename T>
void print_blob(const T &v) {
	uint8_t buf[sizeof(T)];
	memcpy(buf, &v, sizeof(T));
	for (int i = 0; i < sizeof(T); ++i) { printf("%02x", buf[i]); }
}
#endif

template <typename T>
class BoardCache {
		enum {
			ENTRY_COUNT = (1 << 15),
			BUCKET_SIZE = 8,
			BUCKET_COUNT = ENTRY_COUNT / BUCKET_SIZE,
			BUCKET_INDEX_MASK = (BUCKET_COUNT - 1)
		};
		struct Bucket {
			uint64_t keys[BUCKET_SIZE];
			T values[BUCKET_SIZE];
		};

		const T *bucket_get(const Bucket &bucket, const uint64_t k) const {
			for (int i = 0; i < BUCKET_SIZE; ++i) {
				if (bucket.keys[i] == k) { return &bucket.values[i]; }
			}
			return 0;
		}

#if USE_CACHE_VERIFICATION_MAP
		typedef std::map<uint64_t, T> MapT;
		typedef typename MapT::iterator MapIterT;
		typedef typename MapT::value_type MapValueT;
#endif

	public:
		BoardCache(): m_buckets(0) {
			m_buckets = static_cast<Bucket*>(calloc(BUCKET_COUNT, sizeof(Bucket)));
		}

		~BoardCache() {
			free(m_buckets);
		}

		void reset() {
			memset(m_buckets, 0, BUCKET_COUNT * sizeof(Bucket));
		}

		void *where(const Board &board) { return where(pack_board_state(board)); }

		const void *where(const Board &board) const { return where(pack_board_state(board)); }

		void *where(const uint64_t k) {
			const uint64_t h = mix64(k);
			return static_cast<void*>(&m_buckets[h & BUCKET_INDEX_MASK]);
		}

		const void *where(const uint64_t k) const {
			const uint64_t h = mix64(k);
			return static_cast<const void*>(&m_buckets[h & BUCKET_INDEX_MASK]);
		}

		const T *get(const uint64_t k, const void *where) const {
			assert(k);
			assert(where);
			const Bucket &bucket = *static_cast<const Bucket*>(where);
			for (int i = 0; i < BUCKET_SIZE; ++i) {
				if (bucket.keys[i] == k) {
#if CRAZY_VERBOSE_CACHE_DEBUGGER
					printf("%016lx: get (found) ", k);
					print_blob(bucket.values[i]);
					printf(" : ");
					print_blob(m_verifier.find(k)->second);
					printf("\n");
#endif
#if USE_CACHE_VERIFICATION_MAP
					assert(m_verifier.count(k));
					assert(memcmp(&m_verifier.find(k)->second, &bucket.values[i], sizeof(T)) == 0);
#endif
					return &bucket.values[i];
				}
			}
#if CRAZY_VERBOSE_CACHE_DEBUGGER
			printf("%016lx: get (not found)\n", k);
#endif
			//assert(m_verifier.count(k) == 0); // not actually necessarily true
			return 0;
		}

		void put(const uint64_t k, void *where, const T &value) {
			assert(k);
			assert(where);
			Bucket &bucket = *static_cast<Bucket*>(where);
			for (int i = 0; i < BUCKET_SIZE; ++i) {
				if (bucket.keys[i] == k) {
#if CRAZY_VERBOSE_CACHE_DEBUGGER
					printf("%016lx: replace ", k);
					print_blob(bucket.values[i]);
					printf(" : ");
					print_blob(m_verifier.find(k)->second);
					printf("\n");
#endif
#if USE_CACHE_VERIFICATION_MAP
					assert(m_verifier.count(k));
					assert(memcmp(&m_verifier.find(k)->second, &bucket.values[i], sizeof(T)) == 0);
					m_verifier[k] = value;
#endif
					bucket.values[i] = value;
					return;
				}
			}
#if CRAZY_VERBOSE_CACHE_DEBUGGER
			printf("%016lx: put (new)\n", k);
#endif
			//assert(m_verifier.count(k) == 0); // not actually necessarily true
			for (int i = BUCKET_SIZE-1; i > 0; --i) {
				bucket.keys[i] = bucket.keys[i-1];
				bucket.values[i] = bucket.values[i-1];
			}
			bucket.keys[0] = k;
			bucket.values[0] = value;
#if USE_CACHE_VERIFICATION_MAP
			m_verifier[k] = value;
#endif
		}

		const T *get(const Board &board) const {
			const uint64_t k = pack_board_state(board);
			return get(k, where(k));
		}

		void put(const Board &board, const T &value) {
			const uint64_t k = pack_board_state(board);
			put(k, where(k), value);
		}

	private:
		Bucket *m_buckets;
#if USE_CACHE_VERIFICATION_MAP
		MapT m_verifier;
#endif
};

typedef int (*Evaluator)(const Board &board);

class Searcher {
	public:
		Searcher(): evalfn(0), num_moves(0), best_first_move(-1) {}

		int search(Evaluator evalfn, const Board &board, const RNG &rng, int lookahead) {
			assert(evalfn);
			this->evalfn = evalfn;
			this->num_moves = 0;
			this->best_first_move = -1;
			return do_search(board, rng, lookahead, &best_first_move);
		}

		int get_num_moves() const { return num_moves; }
		int get_best_first_move() const { return best_first_move; }

	protected:
		int eval_board(const Board &board) { return evalfn(board); }
		void tally_move() { ++num_moves; }

	private:
		Evaluator evalfn;
		int num_moves;
		int best_first_move;

		virtual int do_search(const Board &board, const RNG &rng, int lookahead, int *move) = 0;
};

class SearcherCheat : public Searcher {
	private:
		int do_search_real(const Board &board, const RNG &rng, int lookahead, int *move) {
			if (move) { *move = -1; }
			if (lookahead == 0) { return eval_board(board); }

			Board next_state;
			RNG next_rng;
			int best_score = INT_MIN;
			for (int i = 0; i < 4; ++i) {
				next_state = board;
				next_rng = rng;
				if (!next_state.move(i, 0, next_rng)) { continue; } // ignore null moves
				tally_move();
				int score = do_search_real(next_state, next_rng, lookahead - 1, 0);
				if (score > best_score) {
					best_score = score;
					if (move) { *move = i; }
				}
			}
			return best_score;
		}

		virtual int do_search(const Board &board, const RNG &rng, int lookahead, int *move) {
			assert(lookahead >= 0);
			return do_search_real(board, rng, lookahead, move);
		}
};

class SearcherNaiveMinimax : public Searcher {
	private:
		int do_search_real(const Board &board, int lookahead, int *move) {
			if (move) { *move = -1; }
			if (lookahead == 0) { return eval_board(board); }

			int best_score;
			Board next_state;
			if (lookahead & 1) {
				// minimise
				best_score = INT_MAX;
				for (int i = 0; i < NUM_TILES; ++i) {
					if (board.state[i]) { continue; } // can only place tiles in empty cells
					for (int value = 1; value < 3; ++value) {
						next_state = board;
						next_state.state[i] = value;
						int score = do_search_real(next_state, lookahead - 1, 0);
						if (score < best_score) {
							best_score = score;
						}
					}
				}
			} else {
				// maximise
				best_score = INT_MIN;
				for (int i = 0; i < 4; ++i) {
					next_state = board;
					if (!next_state.tilt(DIR_DX[i], DIR_DY[i], 0)) { continue; } // ignore null moves
					tally_move();
					int score = do_search_real(next_state, lookahead - 1, 0);
					if (score > best_score) {
						best_score = score;
						if (move) { *move = i; }
					}
				}
			}
			return best_score;
		}

		virtual int do_search(const Board &board, const RNG& /*rng*/, int lookahead, int *move) {
			assert(lookahead >= 0);
			return do_search_real(board, lookahead*2, move);
		}
};

static int imin(int a, int b) { return (a < b ? a : b); }
static int imax(int a, int b) { return (a > b ? a : b); }

class SearcherAlphaBeta : public Searcher {
	private:
		int num_pruned;

		int do_search_mini(const Board &board, int alpha, int beta, int lookahead) {
			Board next_state;
			for (int i = 0; i < NUM_TILES; ++i) {
				if (board.state[i]) { continue; } // can only place tiles in empty cells
				for (int value = 1; value < 3; ++value) {
					next_state = board;
					next_state.state[i] = value;
					beta = imin(beta, do_search_maxi(next_state, alpha, beta, lookahead - 1, 0));
					if (alpha >= beta) { ++num_pruned; return beta; }
				}
			}
			return beta;
		}

		int do_search_maxi(const Board &board, int alpha, int beta, int lookahead, int *move) {
			if (move) { *move = -1; }
			if (lookahead == 0) { return eval_board(board); }
			// final score must be *at least* alpha and *at most* beta
			// alpha <= score <= beta
			Board next_state;
			for (int i = 0; i < 4; ++i) {
				next_state = board;
				if (!next_state.tilt(DIR_DX[i], DIR_DY[i], 0)) { continue; } // ignore null moves
				tally_move();
				int score = do_search_mini(next_state, alpha, beta, lookahead - 1);
				if (score > alpha) {
					alpha = score;
					if (move) { *move = i; }
				}
				if (alpha >= beta) { ++num_pruned; return alpha; }
			}
			return alpha;
		}

		virtual int do_search(const Board &board, const RNG& /*rng*/, int lookahead, int *move) {
			assert(lookahead >= 0);
			num_pruned = 0;
			int score = do_search_maxi(board, INT_MIN, INT_MAX, lookahead*2, move);
#if PRINT_CACHE_STATS
			printf("(alpha-beta) alpha-beta pruned %d\n", num_pruned);
#endif
			return score;
		}
};

class SearcherCachingMinimax : public Searcher {
	private:
		struct Info { static const Info NIL; int lookahead; int score; };
		BoardCache<Info> cache;
		enum { STAT_DEPTH = 20 };
		int num_cached[STAT_DEPTH];

		void tally_cache_hit(int lookahead) {
			++num_cached[imin(lookahead, STAT_DEPTH - 1)];
		}

		int do_search_real(const Board &board, int lookahead, int *move) {
			if (move) { *move = -1; }

			const uint64_t board_k = pack_board_state(board);
			void *cache_loc = cache.where(board_k);
			const Info *cached = cache.get(board_k, cache_loc);
			if (cached && cached->lookahead == lookahead) {
				tally_cache_hit(lookahead);
				return cached->score;
			}

			int best_score;

			if (lookahead == 0) {
				best_score = eval_board(board);
			} else {
				Board next_state;
				if (lookahead & 1) {
					// minimise
					best_score = INT_MAX;
					for (int i = 0; i < NUM_TILES; ++i) {
						if (board.state[i]) { continue; } // can only place tiles in empty cells
						for (int value = 1; value < 3; ++value) {
							next_state = board;
							next_state.state[i] = value;
							int score = do_search_real(next_state, lookahead - 1, 0);
							if (score < best_score) {
								best_score = score;
							}
						}
					}
				} else {
					// maximise
					best_score = INT_MIN;
					for (int i = 0; i < 4; ++i) {
						next_state = board;
						if (!next_state.tilt(DIR_DX[i], DIR_DY[i], 0)) { continue; } // ignore null moves
						tally_move();
						int score = do_search_real(next_state, lookahead - 1, 0);
						if (score > best_score) {
							best_score = score;
							if (move) { *move = i; }
						}
					}
				}
			}

			const Info new_cached = { lookahead, best_score };
			cache.put(board_k, cache_loc, new_cached);
			return best_score;
		}

		virtual int do_search(const Board &board, const RNG& /*rng*/, int lookahead, int *move) {
			assert(lookahead >= 0);
			memset(num_cached, 0, sizeof(num_cached));
			cache.reset();
			int score = do_search_real(board, lookahead*2, move);
#if PRINT_CACHE_STATS
			printf("(caching-minimax) cache hits:");
			for (int i = 0; i < imin(lookahead*2, STAT_DEPTH); ++i) { printf(" %d", num_cached[i]); }
			printf("\n");
#endif
			return score;
		}
};

const SearcherCachingMinimax::Info SearcherCachingMinimax::Info::NIL = { -1, INT_MIN };

class SearcherCachingAlphaBeta : public Searcher {
	private:
		enum { SCORE_UNKNOWN, SCORE_EXACT, SCORE_LOWER_BOUND, SCORE_UPPER_BOUND };
		struct Info { static const Info NIL; int16_t lookahead; int16_t type; int score; };
		BoardCache<Info> cache;
		enum { STAT_DEPTH = 20 };
		int num_cached[STAT_DEPTH];
		int num_pruned;

		void tally_cache_hit(int lookahead) {
			++num_cached[imin(lookahead, STAT_DEPTH - 1)];
		}

		bool check_cached(const Info * const cached, int alpha, int beta, int lookahead, int &output) {
			bool cache_valid = false;
			if (cached && cached->lookahead == lookahead) {
				switch (cached->type) {
					case SCORE_EXACT: cache_valid = true; break;
					case SCORE_UPPER_BOUND: cache_valid = (cached->score <= alpha); break;
					case SCORE_LOWER_BOUND: cache_valid = (cached->score >= beta); break;
				}
			}
			if (cache_valid) {
				tally_cache_hit(lookahead);
				output = cached->score;
			}
			return cache_valid;
		}

		int do_search_mini(const Board &board, int alpha, int beta, int lookahead) {
			assert(alpha < beta);

			const uint64_t board_k = pack_board_state(board);
			void * const cache_loc = cache.where(board_k);

			const Info * const cached = cache.get(board_k, cache_loc);
			int cache_output;
			if (check_cached(cached, alpha, beta, lookahead, cache_output)) { return cache_output; }

			int cache_type = SCORE_LOWER_BOUND;
			Board next_state;
			for (int i = 0; i < NUM_TILES; ++i) {
				if (board.state[i]) { continue; } // can only place tiles in empty cells
				for (int value = 1; value < 3; ++value) {
					next_state = board;
					next_state.state[i] = value;
					int score = do_search_maxi(next_state, alpha, beta, lookahead - 1, 0);
					if (score < beta) {
						beta = score;
						cache_type = SCORE_EXACT;
					}
					if (alpha >= beta) {
						++num_pruned;
						cache_type = SCORE_UPPER_BOUND;
						goto prune;
					}
				}
			}
prune:
			const Info new_cached = { (int16_t)lookahead, (int16_t)cache_type, beta };
			cache.put(board_k, cache_loc, new_cached);
			return beta;
		}

		int do_search_maxi(const Board &board, int alpha, int beta, int lookahead, int *move) {
			if (move) { *move = -1; }
			assert(alpha < beta);

			const uint64_t board_k = pack_board_state(board);
			void * const cache_loc = cache.where(board_k);

			const Info * const cached = cache.get(board_k, cache_loc);
			int cache_output;
			if (check_cached(cached, alpha, beta, lookahead, cache_output)) { return cache_output; }

			if (lookahead == 0) {
				int score = eval_board(board);
				const Info new_cached = { 0, SCORE_EXACT, score };
				cache.put(board_k, cache_loc, new_cached);
				return score;
			} else {
				int cache_type = SCORE_UPPER_BOUND;
				Board next_state;
				for (int i = 0; i < 4; ++i) {
					next_state = board;
					if (!next_state.tilt(DIR_DX[i], DIR_DY[i], 0)) { continue; } // ignore null moves
					tally_move();
					int score = do_search_mini(next_state, alpha, beta, lookahead - 1);
					if (score > alpha) {
						alpha = score;
						cache_type = SCORE_EXACT;
						if (move) { *move = i; }
					}
					if (alpha >= beta) {
						++num_pruned;
						cache_type = SCORE_LOWER_BOUND;
						goto prune;
					}
				}
prune:
				const Info new_cached = { (int16_t)lookahead, (int16_t)cache_type, alpha };
				cache.put(board_k, cache_loc, new_cached);
				return alpha;
			}
		}

		virtual int do_search(const Board &board, const RNG& /*rng*/, int lookahead, int *move) {
			assert(lookahead >= 0);
			memset(num_cached, 0, sizeof(num_cached));
			num_pruned = 0;
			cache.reset();
			int score = do_search_maxi(board, INT_MIN, INT_MAX, lookahead*2, move);
#if PRINT_CACHE_STATS
			printf("(caching-alpha-beta) alpha-beta pruned %d\n", num_pruned);
			printf("(caching-alpha-beta) cache hits:");
			for (int i = 0; i < imin(lookahead*2, STAT_DEPTH); ++i) { printf(" %d", num_cached[i]); }
			printf("\n");
#endif
			return score;
		}
};

const SearcherCachingAlphaBeta::Info SearcherCachingAlphaBeta::Info::NIL = { -1, SCORE_UNKNOWN, INT_MIN };

static int monotonicity(const uint8_t *begin, int stride, int n) {
	int total = (n - 2);
	int i;
	for (i = 0; (*begin == 0) && i < n; ++i) { begin += stride; }
	int last_value = *begin, last_sign = 0;
	for (; i < n; ++i) {
		if (*begin) {
			int delta = (*begin - last_value);
			int sign = (0 < delta) - (delta < 0);
			if (sign) {
				if (last_sign && last_sign != sign) { --total; }
				last_sign = sign;
			}
			last_value = *begin;
		}
		begin += stride;
	}
	return total;
}

static int ai_score_monotonicity(const Board &board) {
	int total = 0;
	// monotonicity of rows
	for (int i = 0; i < TILES_Y; ++i) {
		total += monotonicity(&board.state[i*TILES_X], 1, TILES_X);
	}
	// monotonicity of columns
	for (int j = 0; j < TILES_Y; ++j) {
		total += monotonicity(&board.state[j], TILES_X, TILES_Y);
	}
	return total;
}

static int ai_eval_board(const Board &board) {
	// try to maximise monotonicity
	return ai_score_monotonicity(board);
	// try to maximise free space
	//return board.count_free();
}

static int ai_move(Searcher &searcher, Evaluator evalfn, const Board &board, const RNG &rng, int lookahead, int *score = 0) {
	int best_score = searcher.search(evalfn, board, rng, lookahead);
	int best_move = searcher.get_best_first_move();
#if 1 || PRINT_CACHE_STATS
	printf("tried %d moves!\n", searcher.get_num_moves());
#endif
	if (score) { *score = best_score; }
	return best_move;
}

#if 1
static bool automove(BoardHistory &history, AnimState &anim) {
	const int lookahead = 5;
	//SearcherCheat searcher;
	SearcherCachingAlphaBeta searcher;
	int move = ai_move(searcher, &ai_eval_board, history.get(), history.get_rng(), lookahead);
	if (move != -1) {
		history.move(move, anim);
		return true;
	} else {
		return false;
	}
}

#else

static bool automove(BoardHistory &history, AnimState &anim) {
	const int lookahead = 3;

	//SearcherCheat searcher;
	SearcherNaiveMinimax searcher_a;
	SearcherAlphaBeta searcher_b;
	SearcherCachingMinimax searcher_c;
	SearcherCachingAlphaBeta searcher_d;

	const Board &board = history.get();
	const RNG &rng = history.get_rng();
#if PRINT_BOARD_STATE
	printf("AI move, board state: %016lx rng %08x,%08x,%08x,%08x (lookahead = %d)\n",
			pack_board_state(board), rng.x, rng.y, rng.z, rng.w, lookahead);
#endif

	int move_a = ai_move(searcher_a, &ai_eval_board, board, rng, lookahead);
	int move_b = ai_move(searcher_b, &ai_eval_board, board, rng, lookahead);
	assert(move_a == move_b);
	int move_c = ai_move(searcher_c, &ai_eval_board, board, rng, lookahead);
	assert(move_a == move_c);
	int move_d = ai_move(searcher_d, &ai_eval_board, board, rng, lookahead);
	assert(move_a == move_d);

	int move = move_a;
	if (move != -1) {
		history.move(move, anim);
		return true;
	} else {
		return false;
	}
}
#endif

static void tile_verts(int value, float x, float y, float scale) {
	x += 64.0f; // centre of the tile
	y += 64.0f; // centre of the tile
	const float extent = scale * 64.0f;
	const float u = (value % 4) * 0.25f;
	const float v = (value / 4) * 0.25f;
	glTexCoord2f(u + 0.00f, v + 0.00f); glVertex2f(x - extent, y - extent);
	glTexCoord2f(u + 0.25f, v + 0.00f); glVertex2f(x + extent, y - extent);
	glTexCoord2f(u + 0.25f, v + 0.25f); glVertex2f(x + extent, y + extent);
	glTexCoord2f(u + 0.00f, v + 0.25f); glVertex2f(x - extent, y + extent);
}

static void render_anim(float alpha, const Board& /*board*/, const AnimState &anim) {
	glColor4ub(255, 255, 255, 255);
	glBegin(GL_QUADS);
	for (int i = 0; i < anim.ntiles; ++i) {
		const TileAnim &tile = anim.tiles[i];
		tile_verts(tile.value, tile.x.eval(alpha), tile.y.eval(alpha), tile.scale.eval(alpha));
	}
	glEnd();
}

static void render_static(const Board &board) {
	glColor4ub(255, 255, 255, 255);
	glBegin(GL_QUADS);
	for (int i = 0; i < NUM_TILES; ++i) {
		const int value = board.state[i];
		if (value) {
			float x, y;
			tile_idx_to_xy(i, &x, &y);
			tile_verts(value, x, y, 1.0f);
		}
	}
	glEnd();
}

static void render(int wnd_w, int wnd_h, float alpha, const Board &board, const AnimState &anim) {
	glClear(GL_COLOR_BUFFER_BIT);

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
	if (alpha < 1.0) {
		render_anim(alpha, board, anim);
	} else {
		render_static(board);
	}
}

// -------- GLOBAL STATE -----------------------------------------------------------------------

static BoardHistory s_history;
static AnimState s_anim;
static double s_anim_time0;
static double s_anim_time1;
static bool s_autoplay;

static const double ANIM_TIME_NORMAL = 0.2;
static const double ANIM_TIME_AUTOPLAY = 0.05;

static void start_anim(const double len) {
	if (s_anim.tiles_changed()) {
		s_anim_time0 = glfwGetTime();
		s_anim_time1 = s_anim_time0 + len;
	} else {
		s_anim_time0 = s_anim_time1 = 0.0;
	}
}

#if 0
static void stop_anim() {
	s_anim_time0 = s_anim_time1 = 0.0;
}
#endif

static void handle_key(GLFWwindow * /*wnd*/, int key, int /*scancode*/, int action, int /*mods*/) {
	if (action == GLFW_PRESS) {
		if (key == GLFW_KEY_ESCAPE) {
			exit(0);
		} else {
			if (s_autoplay) {
				if (key == GLFW_KEY_P) { s_autoplay = false; }
			} else {
				s_anim.reset();
				switch (key) {
					case GLFW_KEY_RIGHT: { s_history.move(MOVE_RIGHT, s_anim); } break;
					case GLFW_KEY_LEFT:  { s_history.move(MOVE_LEFT, s_anim); } break;
					case GLFW_KEY_DOWN:  { s_history.move(MOVE_DOWN, s_anim); } break;
					case GLFW_KEY_UP:    { s_history.move(MOVE_UP, s_anim); } break;
					case GLFW_KEY_Z:     { s_history.undo(); } break;
					case GLFW_KEY_X:     { s_history.redo(); } break;
					case GLFW_KEY_N:     { s_history.new_game(s_anim); } break;
					case GLFW_KEY_H:     { automove(s_history, s_anim); } break;
					case GLFW_KEY_P:     { s_autoplay = automove(s_history, s_anim); } break;
				}
				start_anim(s_autoplay ? ANIM_TIME_AUTOPLAY : ANIM_TIME_NORMAL);
			}
		}
	}
}

int main(int /*argc*/, char** /*argv*/) {
	glfwInit();
	GLFWwindow *wnd = glfwCreateWindow(700, 700, "2048", NULL, NULL);

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

	s_autoplay = false;
	s_anim.reset();
	s_history.reset();

#if 0
	{
		Board board;
		RNG rng;
		unpack_board_state(board, 0x7100630035102200ul);
		rng.x = 0xdec687c8u;
		rng.y = 0x2c30e98bu;
		rng.z = 0xa20ee555u;
		rng.w = 0x8587a82eu;
		s_history.reset(board, rng);
		s_anim.reset();
		start_anim(0.0);
	}
#else
	s_history.new_game(s_anim);
	start_anim(ANIM_TIME_NORMAL);
#endif

	glfwSetKeyCallback(wnd, &handle_key);

	while (!glfwWindowShouldClose(wnd)) {
		double t = glfwGetTime();
		float alpha = (t - s_anim_time0) / (s_anim_time1 - s_anim_time0);
		assert(alpha >= 0.0f);
		if (alpha > 1.0f) { alpha = 1.0f; }

		int wnd_w, wnd_h;
		glfwGetFramebufferSize(wnd, &wnd_w, &wnd_h);

		render(wnd_w, wnd_h, alpha, s_history.get(), s_anim);

		glfwSwapBuffers(wnd);

		// if we're not animating then be nice and don't spam the CPU & GPU
		const bool anim_done = (t >= s_anim_time1);
		if (anim_done) {
			if (s_autoplay) {
				s_anim.reset();
				s_autoplay = automove(s_history, s_anim);
				start_anim(ANIM_TIME_AUTOPLAY);
				glfwPollEvents();
			} else {
				glfwWaitEvents();
			}
		} else {
			glfwPollEvents();
		}
	}
	glfwTerminate();
	return 0;
}

// vim: set ts=4 sw=4 noet:
