/**
 *  miniSphere JavaScript game engine
 *  Copyright (c) 2015-2017, Fat Cerberus
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 *  * Neither the name of miniSphere nor the names of its contributors may be
 *    used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
**/

#include "minisphere.h"
#include "image.h"

#include "color.h"
#include "galileo.h"
#include "transform.h"

struct image
{
	unsigned int    refcount;
	unsigned int    id;
	ALLEGRO_BITMAP* bitmap;
	unsigned int    cache_hits;
	image_lock_t    lock;
	unsigned int    lock_count;
	char*           path;
	color_t*        pixel_cache;
	rect_t          scissor_box;
	transform_t*    transform;
	int             width;
	int             height;
	image_t*        parent;
};

static void cache_pixels   (image_t* image);
static void uncache_pixels (image_t* image);

static image_t*     s_last_image = NULL;
static unsigned int s_next_image_id = 0;

image_t*
image_new(int width, int height)
{
	image_t* image;

	console_log(3, "creating image #%u at %dx%d", s_next_image_id, width, height);
	image = calloc(1, sizeof(image_t));
	if ((image->bitmap = al_create_bitmap(width, height)) == NULL)
		goto on_error;
	image->id = s_next_image_id++;
	image->width = al_get_bitmap_width(image->bitmap);
	image->height = al_get_bitmap_height(image->bitmap);
	image->scissor_box = rect(0, 0, image->width, image->height);
	image->transform = transform_new();
	transform_orthographic(image->transform, 0.0f, 0.0f, image->width, image->height, -1.0f, 1.0f);
	return image_ref(image);

on_error:
	free(image);
	return NULL;
}

image_t*
image_new_slice(image_t* parent, int x, int y, int width, int height)
{
	image_t* image;

	console_log(3, "creating image #%u as %dx%d subimage of image #%u", s_next_image_id, width, height, parent->id);
	image = calloc(1, sizeof(image_t));
	if (!(image->bitmap = al_create_sub_bitmap(parent->bitmap, x, y, width, height)))
		goto on_error;
	image->id = s_next_image_id++;
	image->width = al_get_bitmap_width(image->bitmap);
	image->height = al_get_bitmap_height(image->bitmap);
	image->parent = image_ref(parent);
	image->scissor_box = rect(0, 0, image->width, image->height);
	image->transform = transform_new();
	transform_orthographic(image->transform, 0.0f, 0.0f, image->width, image->height, -1.0f, 1.0f);
	return image_ref(image);

on_error:
	free(image);
	return NULL;
}

image_t*
image_clone(const image_t* it)
{
	image_t* image;

	console_log(3, "cloning image #%u from source image #%u",
		s_next_image_id, it->id);

	image = calloc(1, sizeof(image_t));
	if (!(image->bitmap = al_clone_bitmap(it->bitmap)))
		goto on_error;
	image->id = s_next_image_id++;
	image->width = al_get_bitmap_width(image->bitmap);
	image->height = al_get_bitmap_height(image->bitmap);
	image->scissor_box = rect(0, 0, image->width, image->height);
	image->transform = transform_new();
	transform_orthographic(image->transform, 0.0f, 0.0f, image->width, image->height, -1.0f, 1.0f);

	return image_ref(image);

on_error:
	free(image);
	return NULL;
}

image_t*
image_load(const char* filename)
{
	ALLEGRO_FILE* al_file = NULL;
	const char*   file_ext;
	size_t        file_size;
	uint8_t       first_16[16];
	image_t*      image;
	void*         slurp = NULL;

	console_log(2, "loading image #%u from '%s'", s_next_image_id, filename);

	image = calloc(1, sizeof(image_t));
	if (!(slurp = game_read_file(g_game, filename, &file_size)))
		goto on_error;
	al_file = al_open_memfile(slurp, file_size, "rb");

	// look at the first 16 bytes of the file to determine its actual type.
	// Allegro won't load it if the content doesn't match the file extension, so
	// we have to inspect the file ourselves.
	al_fread(al_file, first_16, 16);
	al_fseek(al_file, 0, ALLEGRO_SEEK_SET);
	file_ext = strrchr(filename, '.');
	if (memcmp(first_16, "BM", 2) == 0)
		file_ext = ".bmp";
	if (memcmp(first_16, "\211PNG\r\n\032\n", 8) == 0)
		file_ext = ".png";
	if (memcmp(first_16, "\xFF\xD8", 2) == 0)
		file_ext = ".jpg";

	if (!(image->bitmap = al_load_bitmap_f(al_file, file_ext)))
		goto on_error;
	al_fclose(al_file);
	free(slurp);
	image->width = al_get_bitmap_width(image->bitmap);
	image->height = al_get_bitmap_height(image->bitmap);
	image->scissor_box = rect(0, 0, image->width, image->height);
	image->transform = transform_new();
	transform_orthographic(image->transform, 0.0f, 0.0f, image->width, image->height, -1.0f, 1.0f);

	image->path = strdup(filename);
	image->id = s_next_image_id++;
	return image_ref(image);

on_error:
	console_log(2, "    failed to load image #%u", s_next_image_id++);
	if (al_file != NULL)
		al_fclose(al_file);
	free(slurp);
	free(image);
	return NULL;
}

image_t*
image_read(file_t* file, int width, int height)
{
	long                   file_pos;
	image_t*               image;
	uint8_t*               line_ptr;
	size_t                 line_size;
	ALLEGRO_LOCKED_REGION* lock = NULL;

	int i_y;

	console_log(3, "reading %dx%d image #%u from open file", width, height, s_next_image_id);
	image = calloc(1, sizeof(image_t));
	file_pos = file_position(file);
	if (!(image->bitmap = al_create_bitmap(width, height))) goto on_error;
	if (!(lock = al_lock_bitmap(image->bitmap, ALLEGRO_PIXEL_FORMAT_ABGR_8888, ALLEGRO_LOCK_WRITEONLY)))
		goto on_error;
	line_size = width * 4;
	for (i_y = 0; i_y < height; ++i_y) {
		line_ptr = (uint8_t*)lock->data + i_y * lock->pitch;
		if (file_read(file, line_ptr, 1, line_size) != 1)
			goto on_error;
	}
	al_unlock_bitmap(image->bitmap);
	image->id = s_next_image_id++;
	image->width = al_get_bitmap_width(image->bitmap);
	image->height = al_get_bitmap_height(image->bitmap);
	image->scissor_box = rect(0, 0, image->width, image->height);
	image->transform = transform_new();
	transform_orthographic(image->transform, 0.0f, 0.0f, image->width, image->height, -1.0f, 1.0f);
	return image_ref(image);

on_error:
	console_log(3, "    failed!");
	file_seek(file, file_pos, WHENCE_SET);
	if (lock != NULL) al_unlock_bitmap(image->bitmap);
	if (image != NULL) {
		if (image->bitmap != NULL) al_destroy_bitmap(image->bitmap);
		free(image);
	}
	return NULL;
}

image_t*
image_read_slice(file_t* file, image_t* parent, int x, int y, int width, int height)
{
	long          file_pos;
	image_t*      image;
	image_lock_t* lock = NULL;
	color_t       *p_line;

	int i_y;

	file_pos = file_position(file);
	if (!(image = image_new_slice(parent, x, y, width, height)))
		goto on_error;
	if (!(lock = image_lock(parent)))
		goto on_error;
	for (i_y = 0; i_y < height; ++i_y) {
		p_line = lock->pixels + x + (i_y + y) * lock->pitch;
		if (file_read(file, p_line, 1, width * 4) != 1)
			goto on_error;
	}
	image_unlock(parent, lock);
	return image;

on_error:
	file_seek(file, file_pos, WHENCE_SET);
	if (lock != NULL)
		image_unlock(parent, lock);
	image_unref(image);
	return NULL;
}

image_t*
image_ref(image_t* it)
{

	if (it != NULL)
		++it->refcount;
	return it;
}

void
image_unref(image_t* it)
{
	if (it == NULL || --it->refcount > 0)
		return;

	console_log(3, "disposing image #%u no longer in use",
		it->id);
	uncache_pixels(it);
	al_destroy_bitmap(it->bitmap);
	image_unref(it->parent);
	free(it->path);
	transform_unref(it->transform);
	free(it);
}

ALLEGRO_BITMAP*
image_bitmap(image_t* it)
{
	uncache_pixels(it);
	return it->bitmap;
}

int
image_height(const image_t* it)
{
	return it->height;
}

const char*
image_path(const image_t* it)
{
	return it->path;
}

int
image_width(const image_t* it)
{
	return it->width;
}

rect_t
image_get_scissor(const image_t* it)
{
	return it->scissor_box;
}

transform_t*
image_get_transform(const image_t* it)
{
	return it->transform;
}

void
image_set_scissor(image_t* it, rect_t value)
{
	rect_t bounds;

	it->scissor_box = value;
	if (it == s_last_image) {
		bounds = rect(0, 0, it->width, it->height);
		if (!do_rects_intersect(value, bounds)) {
			// workaround for Allegro bug: setting the clipping rectangle completely
			// out of bounds will cause a GL_INVALID_VALUE error, leading to mysterious
			// failures later.
			value = rect(0, 0, 0, 0);
		}
		al_set_clipping_rectangle(value.x1, value.y1, value.x2 - value.x1, value.y2 - value.y1);
	}
}

void
image_set_transform(image_t* it, transform_t* transform)
{
	transform_t* old_value;

	old_value = it->transform;
	it->transform = transform_ref(transform);
	transform_unref(old_value);
}

bool
image_apply_colormat(image_t* it, colormatrix_t matrix, int x, int y, int width, int height)
{
	image_lock_t* lock;
	color_t*      pixel;

	int i_x, i_y;

	if (!(lock = image_lock(it)))
		return false;
	uncache_pixels(it);
	for (i_x = x; i_x < x + width; ++i_x) for (i_y = y; i_y < y + height; ++i_y) {
		pixel = &lock->pixels[i_x + i_y * lock->pitch];
		*pixel = color_transform(*pixel, matrix);
	}
	image_unlock(it, lock);
	return true;
}

bool
image_apply_colormat_4(image_t* it, colormatrix_t ul_mat, colormatrix_t ur_mat, colormatrix_t ll_mat, colormatrix_t lr_mat, int x, int y, int w, int h)
{
	// this function might be difficult to understand at first. the implementation
	// is, however, much easier to follow than the one in Sphere. basically what it
	// boils down to is bilinear interpolation, but with matrices. it's much more
	// straightforward than it sounds.

	int           i1, i2;
	image_lock_t* lock;
	colormatrix_t mat_1, mat_2, mat_3;
	color_t*      pixel;

	int i_x, i_y;

	if (!(lock = image_lock(it)))
		return false;
	uncache_pixels(it);
	for (i_y = y; i_y < y + h; ++i_y) {
		// thankfully, we don't have to do a full bilinear interpolation every frame.
		// two thirds of the work is done in the outer loop, giving us two color matrices
		// which we then use in the inner loop to calculate the transforms for individual
		// pixels.
		i1 = y + h - 1 - i_y;
		i2 = i_y - y;
		mat_1 = colormatrix_lerp(ul_mat, ll_mat, i1, i2);
		mat_2 = colormatrix_lerp(ur_mat, lr_mat, i1, i2);
		for (i_x = x; i_x < x + w; ++i_x) {
			// calculate the final matrix for this pixel and transform it
			i1 = x + w - 1 - i_x;
			i2 = i_x - x;
			mat_3 = colormatrix_lerp(mat_1, mat_2, i1, i2);
			pixel = &lock->pixels[i_x + i_y * lock->pitch];
			*pixel = color_transform(*pixel, mat_3);

		}
	}
	image_unlock(it, lock);
	return true;
}

bool
image_apply_lookup(image_t* it, int x, int y, int width, int height, uint8_t red_lu[256], uint8_t green_lu[256], uint8_t blue_lu[256], uint8_t alpha_lu[256])
{
	ALLEGRO_BITMAP*        bitmap = image_bitmap(it);
	uint8_t*               pixel;
	ALLEGRO_LOCKED_REGION* lock;

	int i_x, i_y;

	if ((lock = al_lock_bitmap(bitmap, ALLEGRO_PIXEL_FORMAT_ABGR_8888, ALLEGRO_LOCK_READWRITE)) == NULL)
		return false;
	uncache_pixels(it);
	for (i_x = x; i_x < x + width; ++i_x) for (i_y = y; i_y < y + height; ++i_y) {
		pixel = (uint8_t*)lock->data + i_x * 4 + i_y * lock->pitch;
		pixel[0] = red_lu[pixel[0]];
		pixel[1] = green_lu[pixel[1]];
		pixel[2] = blue_lu[pixel[2]];
		pixel[3] = alpha_lu[pixel[3]];
	}
	al_unlock_bitmap(bitmap);
	return true;
}

void
image_blit(image_t* it, image_t* target_image, int x, int y)
{
	int             blend_mode_dest;
	int             blend_mode_src;
	int             blend_op;
	ALLEGRO_BITMAP* old_target;

	old_target = al_get_target_bitmap();
	al_set_target_bitmap(image_bitmap(target_image));
	al_get_blender(&blend_op, &blend_mode_src, &blend_mode_dest);
	al_set_blender(ALLEGRO_ADD, ALLEGRO_ONE, ALLEGRO_ZERO);
	al_draw_bitmap(image_bitmap(it), x, y, 0x0);
	al_set_blender(blend_op, blend_mode_src, blend_mode_dest);
	al_set_target_bitmap(old_target);
}

void
image_draw(image_t* it, int x, int y)
{
	al_draw_bitmap(it->bitmap, x, y, 0x0);
}

void
image_draw_masked(image_t* it, color_t mask, int x, int y)
{
	al_draw_tinted_bitmap(it->bitmap, nativecolor(mask), x, y, 0x0);
}

void
image_draw_scaled(image_t* it, int x, int y, int width, int height)
{
	al_draw_scaled_bitmap(it->bitmap,
		0, 0, al_get_bitmap_width(it->bitmap), al_get_bitmap_height(it->bitmap),
		x, y, width, height, 0x0);
}

void
image_draw_scaled_masked(image_t* it, color_t mask, int x, int y, int width, int height)
{
	al_draw_tinted_scaled_bitmap(it->bitmap, nativecolor(mask),
		0, 0, al_get_bitmap_width(it->bitmap), al_get_bitmap_height(it->bitmap),
		x, y, width, height, 0x0);
}

void
image_draw_tiled(image_t* it, int x, int y, int width, int height)
{
	image_draw_tiled_masked(it, color_new(255, 255, 255, 255), x, y, width, height);
}

void
image_draw_tiled_masked(image_t* it, color_t mask, int x, int y, int width, int height)
{
	ALLEGRO_COLOR native_mask = nativecolor(mask);
	int           img_w, img_h;
	bool          is_drawing_held;
	int           tile_w, tile_h;

	int i_x, i_y;

	img_w = it->width; img_h = it->height;
	if (img_w >= 16 && img_h >= 16) {
		// tile in hardware whenever possible
		ALLEGRO_VERTEX vbuf[] = {
			{ x, y, 0, 0, 0, native_mask },
			{ x + width, y, 0, width, 0, native_mask },
			{ x, y + height, 0, 0, height, native_mask },
			{ x + width, y + height, 0, width, height, native_mask }
		};
		al_draw_prim(vbuf, NULL, it->bitmap, 0, 4, ALLEGRO_PRIM_TRIANGLE_STRIP);
	}
	else {
		// texture smaller than 16x16, tile it in software (Allegro pads it)
		is_drawing_held = al_is_bitmap_drawing_held();
		al_hold_bitmap_drawing(true);
		for (i_x = width / img_w; i_x >= 0; --i_x) for (i_y = height / img_h; i_y >= 0; --i_y) {
			tile_w = i_x == width / img_w ? width % img_w : img_w;
			tile_h = i_y == height / img_h ? height % img_h : img_h;
			al_draw_tinted_bitmap_region(it->bitmap, native_mask,
				0, 0, tile_w, tile_h,
				x + i_x * img_w, y + i_y * img_h, 0x0);
		}
		al_hold_bitmap_drawing(is_drawing_held);
	}
}

void
image_fill(image_t* it, color_t color)
{
	int             clip_height;
	int             clip_width;
	int             clip_x;
	int             clip_y;
	ALLEGRO_BITMAP* old_target;

	uncache_pixels(it);
	al_get_clipping_rectangle(&clip_x, &clip_y, &clip_width, &clip_height);
	al_reset_clipping_rectangle();
	old_target = al_get_target_bitmap();
	al_set_target_bitmap(it->bitmap);
	al_clear_to_color(nativecolor(color));
	al_set_target_bitmap(old_target);
	al_set_clipping_rectangle(clip_x, clip_y, clip_width, clip_height);
}

bool
image_flip(image_t* it, bool is_h_flip, bool is_v_flip)
{
	int             draw_flags = 0x0;
	ALLEGRO_BITMAP* new_bitmap;
	ALLEGRO_BITMAP* old_target;

	if (!is_h_flip && !is_v_flip)  // this really shouldn't happen...
		return true;
	uncache_pixels(it);
	if (!(new_bitmap = al_create_bitmap(it->width, it->height)))
		return false;
	old_target = al_get_target_bitmap();
	al_set_target_bitmap(new_bitmap);
	if (is_h_flip)
		draw_flags |= ALLEGRO_FLIP_HORIZONTAL;
	if (is_v_flip)
		draw_flags |= ALLEGRO_FLIP_VERTICAL;
	al_draw_bitmap(it->bitmap, 0, 0, draw_flags);
	al_set_target_bitmap(old_target);
	al_destroy_bitmap(it->bitmap);
	it->bitmap = new_bitmap;
	return true;
}

color_t
image_get_pixel(image_t* it, int x, int y)
{
	if (it->pixel_cache == NULL) {
		console_log(4, "image_get_pixel() cache miss for image #%u", it->id);
		cache_pixels(it);
	}
	else
		++it->cache_hits;
	return it->pixel_cache[x + y * it->width];
}

image_lock_t*
image_lock(image_t* it)
{
	ALLEGRO_LOCKED_REGION* ll_lock;

	if (it->lock_count == 0) {
		if (!(ll_lock = al_lock_bitmap(it->bitmap, ALLEGRO_PIXEL_FORMAT_ABGR_8888_LE, ALLEGRO_LOCK_READWRITE)))
			return NULL;
		image_ref(it);
		it->lock.pixels = ll_lock->data;
		it->lock.pitch = ll_lock->pitch / 4;
		it->lock.num_lines = it->height;
	}
	++it->lock_count;
	return &it->lock;
}

void
image_render_to(image_t* it, transform_t* transform)
{
	rect_t            bounds;
	ALLEGRO_TRANSFORM matrix;
	rect_t            scissor;

	if (it != s_last_image) {
		al_set_target_bitmap(it->bitmap);
		shader_use(NULL, true);
	}
	bounds = rect(0, 0, it->width, it->height);
	scissor = it->scissor_box;
	if (!do_rects_intersect(scissor, bounds)) {
		// workaround for Allegro bug: setting the clipping rectangle completely
		// out of bounds will cause a GL_INVALID_VALUE error, leading to mysterious
		// failures later.
		scissor = rect(0, 0, 0, 0);
	}
	al_set_clipping_rectangle(scissor.x1, scissor.y1, scissor.x2 - scissor.x1, scissor.y2 - scissor.y1);
	al_use_projection_transform(transform_matrix(it->transform));
	if (transform != NULL) {
		al_use_transform(transform_matrix(transform));
	}
	else {
		al_identity_transform(&matrix);
		al_use_transform(&matrix);
	}
	s_last_image = it;
}

bool
image_replace_color(image_t* it, color_t color, color_t new_color)
{
	ALLEGRO_BITMAP*        bitmap;
	uint8_t*               pixel;
	ALLEGRO_LOCKED_REGION* lock;
	int                    w, h;

	int i_x, i_y;

	bitmap = image_bitmap(it);
	if ((lock = al_lock_bitmap(bitmap, ALLEGRO_PIXEL_FORMAT_ABGR_8888, ALLEGRO_LOCK_READWRITE)) == NULL)
		return false;
	uncache_pixels(it);
	w = al_get_bitmap_width(bitmap);
	h = al_get_bitmap_height(bitmap);
	for (i_x = 0; i_x < w; ++i_x) for (i_y = 0; i_y < h; ++i_y) {
		pixel = (uint8_t*)lock->data + i_x * 4 + i_y * lock->pitch;
		if (pixel[0] == color.r &&
		    pixel[1] == color.g &&
		    pixel[2] == color.b &&
		    pixel[3] == color.a)
		{
			pixel[0] = new_color.r;
			pixel[1] = new_color.g;
			pixel[2] = new_color.b;
			pixel[3] = new_color.a;
		}
	}
	al_unlock_bitmap(bitmap);
	return true;
}

bool
image_rescale(image_t* it, int width, int height)
{
	ALLEGRO_BITMAP* new_bitmap;
	ALLEGRO_BITMAP* old_target;

	if (width == it->width && height == it->height)
		return true;
	if (!(new_bitmap = al_create_bitmap(width, height)))
		return false;
	uncache_pixels(it);
	old_target = al_get_target_bitmap();
	al_set_blender(ALLEGRO_ADD, ALLEGRO_ONE, ALLEGRO_ZERO);
	al_set_target_bitmap(new_bitmap);
	al_draw_scaled_bitmap(it->bitmap, 0, 0, it->width, it->height, 0, 0, width, height, 0x0);
	al_set_target_bitmap(old_target);
	al_set_blender(ALLEGRO_ADD, ALLEGRO_ALPHA, ALLEGRO_INVERSE_ALPHA);
	al_destroy_bitmap(it->bitmap);
	it->bitmap = new_bitmap;
	it->width = al_get_bitmap_width(it->bitmap);
	it->height = al_get_bitmap_height(it->bitmap);
	return true;
}

bool
image_save(image_t* it, const char* filename)
{
	void*         buffer = NULL;
	size_t        file_size;
	bool          is_eof;
	ALLEGRO_FILE* memfile;
	size_t        next_buf_size;
	bool          result;

	next_buf_size = 65536;
	do {
		buffer = realloc(buffer, next_buf_size);
		memfile = al_open_memfile(buffer, next_buf_size, "wb");
		next_buf_size *= 2;
		al_save_bitmap_f(memfile, strrchr(filename, '.'), it->bitmap);
		file_size = al_ftell(memfile);
		is_eof = al_feof(memfile);
		al_fclose(memfile);
	} while (is_eof);
	result = game_write_file(g_game, filename, buffer, file_size);
	free(buffer);
	return result;
}

void
image_set_pixel(image_t* it, int x, int y, color_t color)
{
	ALLEGRO_BITMAP* old_target;

	uncache_pixels(it);
	old_target = al_get_target_bitmap();
	al_set_target_bitmap(it->bitmap);
	al_draw_pixel(x + 0.5, y + 0.5, nativecolor(color));
	al_set_target_bitmap(old_target);
}

void
image_unlock(image_t* it, image_lock_t* lock)
{
	// if the caller provides the wrong lock pointer, the image
	// won't be unlocked. this prevents accidental unlocking.
	if (lock != &it->lock) return;

	if (it->lock_count == 0 || --it->lock_count > 0)
		return;
	al_unlock_bitmap(it->bitmap);
	image_unref(it);
}

bool
image_write(image_t* it, file_t* file)
{
	color_t*      line_ptr;
	size_t        line_size;
	image_lock_t* lock;

	int i_y;

	console_log(3, "writing %dx%d image #%u to open file", it->width, it->height, it->id);
	if (!(lock = image_lock(it)))
		goto on_error;
	line_size = it->width * 4;
	for (i_y = 0; i_y < it->height; ++i_y) {
		line_ptr = lock->pixels + i_y * lock->pitch;
		if (file_write(file, line_ptr, 1, line_size) != 1)
			goto on_error;
	}
	image_unlock(it, lock);
	return true;

on_error:
	console_log(3, "    couldn't write image to file");
	image_unlock(it, lock);
	return false;
}

static void
cache_pixels(image_t* image)
{
	color_t*      cache;
	image_lock_t* lock;
	void          *psrc, *pdest;

	int i;

	free(image->pixel_cache); image->pixel_cache = NULL;
	if (!(lock = image_lock(image)))
		goto on_error;
	if (!(cache = malloc(image->width * image->height * 4)))
		goto on_error;
	console_log(4, "creating new pixel cache for image #%u", image->id);
	for (i = 0; i < image->height; ++i) {
		psrc = lock->pixels + i * lock->pitch;
		pdest = cache + i * image->width;
		memcpy(pdest, psrc, image->width * 4);
	}
	image_unlock(image, lock);
	image->pixel_cache = cache;
	image->cache_hits = 0;
	return;

on_error:
	if (lock != NULL)
		al_unlock_bitmap(image->bitmap);
}

static void
uncache_pixels(image_t* image)
{
	if (image->pixel_cache == NULL)
		return;
	console_log(4, "pixel cache invalidated for image #%u, hits: %u", image->id, image->cache_hits);
	free(image->pixel_cache);
	image->pixel_cache = NULL;
}
