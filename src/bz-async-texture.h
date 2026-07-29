/* bz-async-texture.h
 *
 * Copyright 2025 Adam Masciola
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <gtk/gtk.h>
#include <libdex.h>

G_BEGIN_DECLS

#define BZ_TYPE_ASYNC_TEXTURE (bz_async_texture_get_type ())
G_DECLARE_FINAL_TYPE (BzAsyncTexture, bz_async_texture, BZ, ASYNC_TEXTURE, GObject)

BzAsyncTexture *
bz_async_texture_new (GFile *source,
                      GFile *cache_into);

BzAsyncTexture *
bz_async_texture_new_with_size (GFile *source,
                                 GFile *cache_into,
                                 int    width,
                                 int    height);

BzAsyncTexture *
bz_async_texture_new_lazy (GFile *source,
                           GFile *cache_into);

BzAsyncTexture *
bz_async_texture_new_lazy_with_size (GFile *source,
                                     GFile *cache_into,
                                     int    width,
                                     int    height);

GFile *
bz_async_texture_get_source (BzAsyncTexture *self);

const char *
bz_async_texture_get_source_uri (BzAsyncTexture *self);

GFile *
bz_async_texture_get_cache_into (BzAsyncTexture *self);

const char *
bz_async_texture_get_cache_into_path (BzAsyncTexture *self);

int
bz_async_texture_get_width (BzAsyncTexture *self);

int
bz_async_texture_get_height (BzAsyncTexture *self);

gboolean
bz_async_texture_get_loaded (BzAsyncTexture *self);

GdkTexture *
bz_async_texture_dup_texture (BzAsyncTexture *self);

DexFuture *
bz_async_texture_dup_future (BzAsyncTexture *self);

void
bz_async_texture_ensure (BzAsyncTexture *self);

void
bz_async_texture_cancel (BzAsyncTexture *self);

gboolean
bz_async_texture_is_loading (BzAsyncTexture *self);

G_END_DECLS
