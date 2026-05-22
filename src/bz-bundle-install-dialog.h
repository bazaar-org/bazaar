/* bz-bundle-install-dialog.h
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

#include "bz-entry.h"
#include "bz-flatpak-repo.h"
#include "bz-state-info.h"
#include <adwaita.h>

G_BEGIN_DECLS

#define BZ_TYPE_BUNDLE_INSTALL_DIALOG (bz_bundle_install_dialog_get_type ())
G_DECLARE_FINAL_TYPE (BzBundleInstallDialog, bz_bundle_install_dialog, BZ, BUNDLE_INSTALL_DIALOG, AdwBreakpointBin)

BzBundleInstallDialog *
bz_bundle_install_dialog_new (void);

BzStateInfo *
bz_bundle_install_dialog_get_state (BzBundleInstallDialog *self);

BzEntry *
bz_bundle_install_dialog_get_entry (BzBundleInstallDialog *self);

BzFlatpakRepo *
bz_bundle_install_dialog_get_runtime_repo (BzBundleInstallDialog *self);

void
bz_bundle_install_dialog_set_state (BzBundleInstallDialog *self,
                                    BzStateInfo           *state);

void
bz_bundle_install_dialog_set_entry (BzBundleInstallDialog *self,
                                    BzEntry               *entry);
void
bz_bundle_install_dialog_set_runtime_repo (BzBundleInstallDialog *self,
                                           BzFlatpakRepo         *runtime_repo);

G_END_DECLS

/* End of bz-bundle-install-dialog.h */
