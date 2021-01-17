#include <libffmpegthumbnailer/videothumbnailerc.h>
#include <SDL2/SDL.h>

#include "util.h"

#define WIDTH 800
#define HEIGHT 600
#define TILE_W 200
#define TILE_H 150
#define PADDING 20
#define FPS 30
//#define NB_RECTS 64

typedef struct {
	SDL_Rect outer;
	SDL_Rect inner;
} Tile;

typedef struct {
	Tile *tiles;
	size_t len;
	size_t allocated;
	Vec2i margin;
	Vec2i div;
} TileLayout;

static const int SCROLL_SENSITIVITY = 30;

static SDL_Renderer *renderer = NULL;
static SDL_Window *window = NULL;
static SDL_Texture **textures = NULL;
static Vec2i *rects = NULL;
static Vec2i winsize;
static Vec2i winpos;
static TileLayout layout = {NULL, 0, {0, 0}, {0, 0}};
static SDL_Rect *bgrects, *fgrects;
static int scrollpos = 0;
static int *cellsidx = NULL;
static char **filenames = NULL;

static int NB_RECTS = 64;
static int NB_CELLS = 0;

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

static void
mouse(SDL_Event *e)
{
	switch (e->type) {
	case SDL_MOUSEBUTTONUP:
		if (e->button.button == SDL_BUTTON_LEFT) {
			/* Print selected image and quit */
			int x = e->button.x - layout.margin.x / 2;
			int y = -scrollpos + e->button.y - layout.margin.y / 2;
			int i = y / (TILE_H + layout.margin.y) * layout.div.x + x / (TILE_W + layout.margin.x);
			if (i < NB_CELLS && cellsidx[i] >= 0) {
				fprintf(stdout, "%s\n", filenames[cellsidx[i]]);
				quit();
			}
		}
		break;
	}
}

static void
scroll(SDL_Event *e)
{
	int sign = e->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1;

	scrollpos += sign * SCROLL_SENSITIVITY * e->wheel.y;
	if (scrollpos > 0)
		scrollpos = 0;
}

static int
getfreecell(int *cellsidx, size_t len, size_t rectidx, int width, Vec2i format)
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
				if (cellsidx[cy * width + cx] >= 0)
					empty = 0;
		if (empty) {  /* We found a free space */
			/* Mark space as occupied with the index of the rect */
			for (cx = x; cx < x + format.x; cx++)
				for (cy = y; cy < y + format.y; cy++)
					cellsidx[cy * width + cx] = rectidx;
			return i;
		}
	}

	return -1;
}

static int
newtile(Vec2i *rect, TileLayout *layout, size_t idx, Tile *tile)
{
	SDL_Rect *r, *t;
	float ratiodiff = TILE_H / (float)TILE_W - rect->y / (float)rect->x;
	float ratio2;
	int j, x, y;
	Vec2i format;

	if (ratiodiff > 0.4 && layout->div.x > 1) {
		format = (Vec2i){2, 1};
	} else if (ratiodiff < -0.4) {
		format = (Vec2i){1, 2};
	} else {
		format = (Vec2i){1, 1};
	}
	/* Use a greedy algorithm to layout the tiles. We're  hoping for enough
	 * variety in the tiles to fill the holes left by high aspect ratio tiles */
	j = getfreecell(cellsidx, NB_CELLS, idx, layout->div.x, format);
	if (j < 0) {
		fprintf(stderr, "Couldn't layout all images.\n");
		return 0;
	}
	x = j % layout->div.x;
	y = j / layout->div.x;

	r = &tile->outer;
	t = &tile->inner;
	r->w = TILE_W * format.x + layout->margin.x * (format.x - 1);
	r->h = TILE_H * format.y + layout->margin.y * (format.y - 1);
	r->x = x * (TILE_W + layout->margin.x) + layout->margin.x / 2;
	r->y = y * (TILE_H + layout->margin.y) + layout->margin.y / 2;

	ratio2 = r->h / (float)r->w - rect->y / (float)rect->x;
	if (ratio2 > 0) {
		t->w = r->w;
		t->h = r->w * rect->y / rect->x;
		t->x = r->x;
		t->y = r->y + (r->h - t->h) / 2;
	} else {
		t->w = r->h * rect->x / rect->y;
		t->h = r->h;
		t->x = r->x + (r->w - t->w) / 2;
		t->y = r->y;
	}
	return 1;
}

static void
gentileslayout(TileLayout *layout)
{
	size_t i;
	Tile tile;

	/* Reset cells index */
	for (i = 0; i < NB_CELLS; i++)
		cellsidx[i] = -1;

	layout->div.x = winsize.x / TILE_W;
	layout->div.y = winsize.y / TILE_H;
	layout->margin.x = (winsize.x % TILE_W) / layout->div.x;
	layout->margin.y = 10;

	for (i = 0; i < layout->len; ++i) {
		if (newtile(&rects[i], layout, i, &tile))
			layout->tiles[i] = tile;
	}
}

static int
loadthumbnail(const char *path, video_thumbnailer *vt, image_data *data,
		size_t idx, Vec2i *rect, SDL_Texture **texture, TileLayout *layout)
{
	Tile tile;
	if (video_thumbnailer_generate_thumbnail_to_buffer(vt, path, data)) {
		data->image_data_width = 1;
		data->image_data_height = 1;
		data->image_data_ptr = NULL;
	}
	*rect = (Vec2i){data->image_data_width, data->image_data_height};
	/* /!\ SDL_PIXELFORMAT_RGB888 means XRGB8888.
	 *     Use SDL_PIXELFORMAT_RGB24 instead for tightly packed data. */
	*texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24,
								SDL_TEXTUREACCESS_STATIC,
								data->image_data_width,
								data->image_data_height);
	if (!data->image_data_ptr)
		return 0;

	SDL_UpdateTexture(*texture, NULL, data->image_data_ptr, data->image_data_width * 3);
	if (idx < layout->len && newtile(rect, layout, idx, &tile))
		layout->tiles[idx] = tile;

	return 1;
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
		bgrects[i] = layout.tiles[i].outer;
		fgrects[i] = layout.tiles[i].inner;
		bgrects[i].y += scrollpos;
		fgrects[i].y += scrollpos;
	}

	SDL_SetRenderDrawColor(renderer, 0x33, 0x33, 0x33, 0xff);
	SDL_RenderFillRects(renderer, bgrects, layout.len);

	SDL_SetRenderDrawColor(renderer, 0xee, 0xee, 0xee, 0xff);
	SDL_RenderFillRects(renderer, fgrects, layout.len);
	for (i = 0; i < layout.len; i++)
		if (SDL_RenderCopy(renderer, textures[i], NULL, &(fgrects[i])))
			printf("RenderCopy error for i = %lu: %s\n", i, SDL_GetError());

	SDL_SetRenderDrawColor(renderer, 0x00, 0x00, 0x00, 0xff);
	SDL_RenderPresent(renderer);
}

int
main(int argc, char* argv[])
{
	int i;
	uint32_t ticksnext = 0;
	video_thumbnailer *thumbnailer = NULL;
	image_data *imgdata = NULL;

	init();

	thumbnailer = video_thumbnailer_create();
	imgdata = video_thumbnailer_create_image_data();
	// FIXME Check that size is correct when taking padding into account
	video_thumbnailer_set_size(thumbnailer, TILE_W, TILE_H);
	thumbnailer->maintain_aspect_ratio = 1;
	// TODO detect if file is animated
	thumbnailer->overlay_film_strip = 0;
	thumbnailer->seek_percentage = 15;
	thumbnailer->thumbnail_image_type = Rgb;

	NB_RECTS = argc - 1;
	textures = ecalloc(NB_RECTS, sizeof(*textures));
	rects = ecalloc(NB_RECTS, sizeof(*rects));
	filenames = ecalloc(NB_RECTS, sizeof(char*));

	for (i = 1; i < argc; i++) {
		filenames[i - 1] = argv[i];
	}


	//gentestrects(&rects, NB_RECTS, (Vec2i){100, 100}, (Vec2i){2048, 2048});
	// FIXME There is no guarantee this is enough in the worst case
	// Maybe 3x NB_RECTS is enough to account for multi-cells rects
	NB_CELLS = NB_RECTS * 3;
	layout.len = 0;
	layout.allocated = NB_RECTS;
	layout.tiles = ecalloc(layout.allocated, sizeof(Tile));
	bgrects = ecalloc(layout.allocated, sizeof(SDL_Rect));
	fgrects = ecalloc(layout.allocated, sizeof(SDL_Rect));
	cellsidx = ecalloc(NB_CELLS, sizeof(int));

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
			} else if (e.type == SDL_MOUSEWHEEL) {
				scroll(&e);
			} else if (e.type == SDL_MOUSEBUTTONUP ||
					e.type == SDL_MOUSEBUTTONDOWN) {
				mouse(&e);
			} else if (e.type == SDL_WINDOWEVENT
					&& (e.window.event == SDL_WINDOWEVENT_EXPOSED ||
					e.window.event == SDL_WINDOWEVENT_RESIZED)) {
				SDL_GetWindowSize(window, &winsize.x, &winsize.y);
				SDL_GetWindowPosition(window, &winpos.x, &winpos.y);
				resize();
			}
		}

		draw();


		// Load one thumbnail per frame, until we've loaded everything
		if (layout.len < layout.allocated) {
			i = layout.len++;
			if (!loadthumbnail(filenames[i], thumbnailer, imgdata, i, &rects[i], &textures[i], &layout))
				fprintf(stderr, "Couldn't generate thumbnail for %s\n", filenames[i]);
		}
	}

	quit();
	return 0;
}
