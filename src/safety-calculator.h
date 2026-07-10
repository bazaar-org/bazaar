/* safety-calculator.h
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

#include "bz-context-row.h"
#include "bz-entry.h"
#include "bz-safety-row.h"
#include <gio/gio.h>

G_BEGIN_DECLS

typedef enum
{
  BZ_HIGH_RISK_GROUP_NONE = 0,
  BZ_HIGH_RISK_GROUP_X11  = 1 << 0,
  BZ_HIGH_RISK_GROUP_DISK = 1 << 1,
} BzHighRiskGroup;

#define BZ_TYPE_HIGH_RISK_GROUP (bz_high_risk_group_get_type ())

GListModel  *
bz_safety_calculator_analyze_entry (BzEntry *entry);

BzImportance
bz_safety_calculator_calculate_rating (BzEntry *entry);

char *
bz_safety_calculator_get_top_icon (BzEntry *entry,
                                   int      index);

BzHighRiskGroup
bz_safety_calculator_get_high_risk_groups (BzEntry *entry);

G_END_DECLS
