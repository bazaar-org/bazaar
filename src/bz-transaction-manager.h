/* bz-transaction-manager.h
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

#include "bz-backend.h"
#include "bz-main-config.h"
#include "bz-transaction.h"

G_BEGIN_DECLS

#define BZ_TRANSACTION_MGR_ERROR (bz_transaction_mgr_error_quark ())
GQuark bz_transaction_mgr_error_quark (void);

typedef enum
{
  BZ_TRANSACTION_MGR_ERROR_CANCELLED_BY_HOOK = 0,
} BzTransaction_MgrError;

#define BZ_TYPE_TRANSACTION_MANAGER (bz_transaction_manager_get_type ())
G_DECLARE_FINAL_TYPE (BzTransactionManager, bz_transaction_manager, BZ, TRANSACTION_MANAGER, GObject)

BzTransactionManager *
bz_transaction_manager_new (void);

void
bz_transaction_manager_set_config (BzTransactionManager *self,
                                   BzMainConfig         *config);

BzMainConfig *
bz_transaction_manager_get_config (BzTransactionManager *self);

void
bz_transaction_manager_set_backend (BzTransactionManager *self,
                                    BzBackend            *backend);

BzBackend *
bz_transaction_manager_get_backend (BzTransactionManager *self);

void
bz_transaction_manager_set_gnome_ext_backend (BzTransactionManager *self,
                                              BzBackend            *backend);
BzBackend *
bz_transaction_manager_get_gnome_ext_backend (BzTransactionManager *self);

void
bz_transaction_manager_set_paused (BzTransactionManager *self,
                                   gboolean              paused);

gboolean
bz_transaction_manager_get_paused (BzTransactionManager *self);

gboolean
bz_transaction_manager_get_active (BzTransactionManager *self);

gboolean
bz_transaction_manager_get_pending (BzTransactionManager *self);

gboolean
bz_transaction_manager_get_has_transactions (BzTransactionManager *self);

GListModel *
bz_transaction_manager_get_all_trackers (BzTransactionManager *self);

G_GNUC_WARN_UNUSED_RESULT
DexFuture *
bz_transaction_manager_add (BzTransactionManager *self,
                            BzTransaction        *transaction);

void
bz_transaction_manager_cancel_current (BzTransactionManager *self);

void
bz_transaction_manager_clear_finished (BzTransactionManager *self);

G_END_DECLS
