/*
 * bz-search-bar.h
 *
 * Copyright 2026 Zelda Ahmed <zoeyahmed10@proton.me>
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <adwaita.h>

G_BEGIN_DECLS

#define BZ_TYPE_SEARCH_BAR (bz_search_bar_get_type ())

G_DECLARE_FINAL_TYPE (BzSearchBar, bz_search_bar, BZ, SEARCH_BAR, GtkWidget)

BzSearchBar *
bz_search_bar_new (void);

gboolean
bz_search_bar_get_busy (BzSearchBar *self);

void
bz_search_bar_set_busy (BzSearchBar *self,
                        gboolean     busy);

GtkWidget *
bz_search_bar_get_end_widget (BzSearchBar *self);

void
bz_search_bar_set_end_widget (BzSearchBar *self,
                              GtkWidget   *widget);

const gchar *
bz_search_bar_get_placeholder (BzSearchBar *self);

void
bz_search_bar_set_placeholder (BzSearchBar *self,
                               const gchar *text);

G_END_DECLS
