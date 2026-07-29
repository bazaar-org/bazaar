/* search-index-write.h
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

#include <gio/gio.h>

G_BEGIN_DECLS

/*
 * HEADER:
 * char[4] "BZSI"
 * uint32  version
 * uint32  entry count
 *
 * CONTENT:
 * id
 * title
 * developer
 * description (cut off)
 * search_tokens
 * icon_path
 *
 * with strings as uint32 length + uint8[] bytes
 *
 */

gboolean
bz_write_search_index (GListModel *groups,
                       const char *out_path,
                       GError    **error);

G_END_DECLS
