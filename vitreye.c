#include <SDL2/SDL.h>

#include "util.h"

#define WIDTH 800
#define HEIGHT 600
#define TILE_W 200
#define TILE_H 150
#define PADDING 20
#define FPS 30
#define NB_RECTS 64

typedef struct {
	SDL_Rect outer;
	SDL_Rect inner;
} Tile;

typedef struct {
	Tile *tiles;
	size_t len;
} TileLayout;

static SDL_Renderer *renderer = NULL;
static SDL_Window *window = NULL;
static Vec2i *rects = NULL;
static Vec2i winsize;
static Vec2i winpos;
static TileLayout layout = {NULL, 0};
static SDL_Rect *bgrects, *fgrects;

static void
gentestrects(Vec2i **rects, size_t count, Vec2i min, Vec2i max)
{
	size_t i;

	/* Generate the same pattern each time */
	srand(1);

	*rects = ecalloc(count, sizeof(SDL_Rect));
	for (i = 0; i < count; ++i) {
		(*rects)[i].x = (uint64_t)rand() * (max.x - min.x) / RAND_MAX + min.x;
		(*rects)[i].y = (uint64_t)rand() * (max.y - min.y) / RAND_MAX + min.y;
	}
}

static int
init(void)
{
	uint32_t sdlflags = SDL_INIT_VIDEO;
	if (!SDL_WasInit(sdlflags))
		if (SDL_Init(sdlflags))
			sdldie("cannot initialize SDL");
	atexit(SDL_Quit);

	window = SDL_CreateWindow("dustr", SDL_WINDOWPOS_UNDEFINED,
							 SDL_WINDOWPOS_UNDEFINED, WIDTH, HEIGHT,
							 SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
	if (!window)
		sdldie("cannot create SDL window");

	renderer = SDL_CreateRenderer(window, -1, 0);
	if (!renderer)
		sdldie("cannot create SDL renderer");
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

	gentestrects(&rects, NB_RECTS, (Vec2i){100, 100}, (Vec2i){2048, 2048});
	layout.len = NB_RECTS;
	layout.tiles = ecalloc(layout.len, sizeof(Tile));
	bgrects = ecalloc(layout.len, sizeof(SDL_Rect));
	fgrects = ecalloc(layout.len, sizeof(SDL_Rect));

	return 1;
}

static void
quit(void)
{
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	renderer = NULL;
	window = NULL;
	exit(0);
}

static void
keyboard(SDL_Event *e)
{
	switch(e->key.keysym.sym) {
	case SDLK_q:
	case SDLK_ESCAPE:
		quit();
		break;
	}
}

static int
getfreecell(int *occupied, size_t len, int width, Vec2i format)
{
	size_t i;
	int x, y, cx, cy;
	int empty;

	if (format.x > width)
		return -1;

	for (i = 0; i < len; i++) {
		x = i % width;
		y = i / width;
		if (x + format.x > width)
			continue;
		if (y + format.y > len / width)
			continue;
		empty = 1;
		for (cx = x; cx < x + format.x && empty; cx++)
			for (cy = y; cy < y + format.y && empty; cy++)
				if (occupied[cy * width + cx])
					empty = 0;
		if (empty) {  /* We found a free space */
			/* Mark space as occupied */
			for (cx = x; cx < x + format.x; cx++)
				for (cy = y; cy < y + format.y; cy++)
					occupied[cy * width + cx] = 1;
			return i;
		}
	}

	return -1;
}

static void
gentileslayout(TileLayout *layout)
{
	const int Y_MARGIN = 10;
	// FIXME There is no guarantee this is enough in the worst case
	const int NB_CELLS = NB_RECTS + 16;
	size_t i;
	int x = 0, y = 0;
	int margin;
	int nx, ny;
	int *occupied;
	SDL_Rect *r, *t;

	nx = winsize.x / TILE_W;
	ny = winsize.y / TILE_H;
	occupied = ecalloc(NB_CELLS, sizeof(int));
	margin = (winsize.x % TILE_W) / nx;

	for (i = 0; i < layout->len; ++i) {
		float ratiodiff = TILE_H / (float)TILE_W - rects[i].y / (float)rects[i].x;
		float ratio2;
		int j;
		Vec2i format;

		if (ratiodiff > 0.4 && nx > 1) {
			format = (Vec2i){2, 1};
		} else if (ratiodiff < -0.4) {
			format = (Vec2i){1, 2};
		} else {
			format = (Vec2i){1, 1};
		}
		/* Use a greedy algorithm to layout the tiles. We're  hoping for enough
		 * variety in the tiles to fill the holes left by high aspect ratio tiles */
		j = getfreecell(occupied, NB_CELLS, nx, format);
		if (j < 0)
			break;
		x = j % nx;
		y = j / nx;

		r = &layout->tiles[i].outer;
		t = &layout->tiles[i].inner;
		r->w = TILE_W * format.x + margin * (format.x - 1);
		r->h = TILE_H * format.y + Y_MARGIN * (format.y - 1);
		r->x = x * (TILE_W + margin) + margin / 2;
		r->y = y * (TILE_H + Y_MARGIN) + Y_MARGIN / 2;

		ratio2 = r->h / (float)r->w - rects[i].y / (float)rects[i].x;
		if (ratio2 > 0) {
			t->w = r->w;
			t->h = r->w * rects[i].y / rects[i].x;
			t->x = r->x;
			t->y = r->y + (r->h - t->h) / 2;
		} else {
			t->w = r->h * rects[i].x / rects[i].y;
			t->h = r->h;
			t->x = r->x + (r->w - t->w) / 2;
			t->y = r->y;
		}
	}

	FREE(occupied);
}

static void
resize()
{
	gentileslayout(&layout);
}

static void
draw()
{
	size_t i;

	SDL_RenderClear(renderer);

	for (i = 0; i < layout.len; i++) {
		SDL_Rect r = layout.tiles[i].outer;
		if (r.x > winsize.x && r.y > winsize.y) {
			bgrects[i] = (SDL_Rect){};
			fgrects[i] = (SDL_Rect){};
		} else {
			bgrects[i] = r;
			fgrects[i] = layout.tiles[i].inner;
		}
	}

	SDL_SetRenderDrawColor(renderer, 0x33, 0x33, 0x33, 0xff);
	SDL_RenderFillRects(renderer, bgrects, layout.len);

	SDL_SetRenderDrawColor(renderer, 0xee, 0xee, 0xee, 0xff);
	SDL_RenderFillRects(renderer, fgrects, layout.len);

	SDL_SetRenderDrawColor(renderer, 0x00, 0x00, 0x00, 0xff);
	SDL_RenderPresent(renderer);
}

int
main(int argc, char* argv[])
{
	uint32_t ticksnext = 0;
	init();

	for (;;) {
		SDL_Event e;
		uint32_t ticks = SDL_GetTicks();
		if (ticks < ticksnext)
			SDL_Delay(ticksnext - ticks);
		ticksnext = ticks + (1000 / FPS);

		while (SDL_PollEvent(&e)) {
			if (e.type == SDL_QUIT) {
				quit();
			} else if (e.type == SDL_KEYDOWN) {
				keyboard(&e);
			} else if (e.type == SDL_WINDOWEVENT
					&& (e.window.event == SDL_WINDOWEVENT_EXPOSED ||
					e.window.event == SDL_WINDOWEVENT_RESIZED)) {
				SDL_GetWindowSize(window, &winsize.x, &winsize.y);
				SDL_GetWindowPosition(window, &winpos.x, &winpos.y);
				resize();
			}
		}

		draw();
	}

	quit();
	return 0;
}
