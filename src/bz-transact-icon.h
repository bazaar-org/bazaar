/* bz-transact-icon.h
 *
 * Copyright 2026 Eva M
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

#include "bz-transact-icon-info.h"

G_BEGIN_DECLS

#define BZ_TYPE_TRANSACT_ICON (bz_transact_icon_get_type ())
G_DECLARE_FINAL_TYPE (BzTransactIcon, bz_transact_icon, BZ, TRANSACT_ICON, AdwBin)

BzTransactIcon *
bz_transact_icon_new (void);

BzTransactIconInfo *
bz_transact_icon_get_info (BzTransactIcon *self);

void
bz_transact_icon_set_info (BzTransactIcon     *self,
                           BzTransactIconInfo *info);

G_END_DECLS

/* End of bz-transact-icon.h */
