/* bz-gnome-extension-entry.h
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

#include <appstream.h>

#include "bz-entry.h"

G_BEGIN_DECLS

#define BZ_TYPE_GNOME_EXTENSION_ENTRY (bz_gnome_extension_entry_get_type ())
G_DECLARE_FINAL_TYPE (BzGnomeExtensionEntry, bz_gnome_extension_entry, BZ, GNOME_EXTENSION_ENTRY, BzEntry)

BzGnomeExtensionEntry *
bz_gnome_extension_entry_new_from_component (AsComponent *component,
                                             GError     **error);

const char *
bz_gnome_extension_entry_get_uuid (BzGnomeExtensionEntry *self);

int
bz_gnome_extension_entry_get_pk (BzGnomeExtensionEntry *self);

guint64
bz_gnome_extension_entry_get_downloads (BzGnomeExtensionEntry *self);

char *
bz_gnome_extension_entry_uuid_to_id (const char *uuid);
G_END_DECLS
