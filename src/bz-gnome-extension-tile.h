/* bz-gnome-extension-tile.h
 *
 * Copyright 2026 Alexander Vanhee
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

#include <adwaita.h>

#include "bz-entry-group.h"
#include "bz-gnome-extension-info.h"
#include "bz-list-tile.h"

G_BEGIN_DECLS

#define BZ_TYPE_GNOME_EXTENSION_TILE (bz_gnome_extension_tile_get_type ())
G_DECLARE_FINAL_TYPE (BzGnomeExtensionTile, bz_gnome_extension_tile, BZ, GNOME_EXTENSION_TILE, BzListTile)

GtkWidget *
bz_gnome_extension_tile_new (void);

void
bz_gnome_extension_tile_set_info (BzGnomeExtensionTile *self,
                                  BzGnomeExtensionInfo *info);

BzGnomeExtensionInfo *
bz_gnome_extension_tile_get_info (BzGnomeExtensionTile *self);

BzEntryGroup *
bz_gnome_extension_tile_get_group (BzGnomeExtensionTile *self);

G_END_DECLS
