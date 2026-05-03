/* bz-gnome-extension-instance.h
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
#include "bz-backend.h"
#include <glib-object.h>

G_BEGIN_DECLS

#define BZ_TYPE_GNOME_EXTENSION_INSTANCE (bz_gnome_extension_instance_get_type ())

G_DECLARE_FINAL_TYPE (BzGnomeExtensionInstance, bz_gnome_extension_instance, BZ, GNOME_EXTENSION_INSTANCE, GObject)

DexFuture *
bz_gnome_extension_instance_new (void);

char *
bz_gnome_extension_instance_dup_shell_version (BzGnomeExtensionInstance *self);

void
bz_gnome_extension_instance_check_for_updates (BzGnomeExtensionInstance *self);

G_END_DECLS
