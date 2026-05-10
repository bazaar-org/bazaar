/* bz-safety-dialog.h
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

#include "bz-entry.h"
#include <adwaita.h>

G_BEGIN_DECLS

G_BEGIN_DECLS
#define BZ_TYPE_SAFETY_DIALOG (bz_safety_dialog_get_type ())

G_DECLARE_FINAL_TYPE (BzSafetyDialog, bz_safety_dialog, BZ, SAFETY_DIALOG, AdwBin)

AdwDialog *
bz_safety_dialog_new (BzEntry *entry);

AdwNavigationPage *
bz_safety_dialog_page_new (BzEntry *entry);

G_END_DECLS
