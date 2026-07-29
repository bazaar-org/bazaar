/* bz-user-data-page.h
 *
 * Copyright 2025 Adam Masciola, Alexander Vanhee
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

#include "bz-state-info.h"
#include <adwaita.h>

G_BEGIN_DECLS

#define BZ_TYPE_USER_DATA_PAGE (bz_user_data_page_get_type ())

G_DECLARE_FINAL_TYPE (BzUserDataPage, bz_user_data_page, BZ, USER_DATA_PAGE, AdwNavigationPage)

GtkWidget *
bz_user_data_page_new (BzStateInfo *state);

void
bz_user_data_page_remove_group (BzUserDataPage *self,
                                BzEntryGroup    *group);

G_END_DECLS
