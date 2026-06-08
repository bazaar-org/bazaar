/* bz-search-filter-popover.h
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
#include "bz-category-flags.h"
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define BZ_TYPE_SEARCH_FILTER_POPOVER (bz_search_filter_popover_get_type ())

G_DECLARE_FINAL_TYPE (BzSearchFilterPopover, bz_search_filter_popover, BZ, SEARCH_FILTER_POPOVER, GtkPopover)

GtkWidget *
bz_search_filter_popover_new (void);

BzCategoryFlags
bz_search_filter_popover_get_selected_categories (BzSearchFilterPopover *self);

gboolean
bz_search_filter_popover_get_only_verified (BzSearchFilterPopover *self);

gboolean
bz_search_filter_popover_get_only_free (BzSearchFilterPopover *self);

gboolean
bz_search_filter_popover_get_only_non_eol (BzSearchFilterPopover *self);

gboolean
bz_search_filter_popover_get_only_mobile (BzSearchFilterPopover *self);

void
bz_search_filter_popover_clear (BzSearchFilterPopover *self);

G_END_DECLS
