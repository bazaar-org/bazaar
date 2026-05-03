/* bz-appstream-parser.h
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

#include "bz-entry.h"
#include <appstream.h>
#include <glib-object.h>
#include <xmlb.h>

G_BEGIN_DECLS

gboolean bz_appstream_parser_populate_entry (BzEntry     *entry,
                                             AsComponent *component,
                                             const char  *appstream_dir,
                                             const char  *remote_name,
                                             const char  *module_dir,
                                             const char  *unique_id_checksum,
                                             const char  *id,
                                             guint        kinds,
                                             GError     **error);

BzEntry *
bz_appstream_parser_entry_from_metainfo (GFile   *metainfo_file,
                                         GFile   *icon_file,
                                         GError **error);

AsComponent *
bz_parse_component_for_node (XbNode  *node,
                             GError **error);

GBytes *
bz_decompress_gz_bytes (GBytes       *compressed,
                        GCancellable *cancellable,
                        GError      **error);

G_END_DECLS
