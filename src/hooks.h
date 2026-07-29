/* hooks.h
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

#include "bz-entry-group.h"
#include "bz-hook.h"

G_BEGIN_DECLS

DexFuture *
bz_execute_hook (BzHook               *hook,
                 BzHookTransactionType ts_type,
                 const char           *ts_appid,
                 BzEntryGroup         *group);

DexFuture *
bz_run_hook_emission (GListModel           *hooks,
                      BzHookSignal          signal,
                      BzHookTransactionType ts_type,
                      const char           *ts_appid,
                      BzEntryGroup         *group);

G_END_DECLS
