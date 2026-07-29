/* bz-app-tile.h
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

#include "bz-entry-group.h"

G_BEGIN_DECLS

#define BZ_TYPE_APP_TILE (bz_app_tile_get_type ())
G_DECLARE_FINAL_TYPE (BzAppTile, bz_app_tile, BZ, APP_TILE, GtkButton)

GtkWidget *
bz_app_tile_new (void);

BzEntryGroup *
bz_app_tile_get_group (BzAppTile *self);

void
bz_app_tile_set_group (BzAppTile    *self,
                       BzEntryGroup *group);

GdkPaintable *
bz_app_tile_get_icon_override (BzAppTile *self);

void
bz_app_tile_set_icon_override (BzAppTile    *self,
                               GdkPaintable *icon_override);

G_END_DECLS

/* End of bz-app-tile.h */
