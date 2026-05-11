/* bz-entry-group.h
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

#include "bz-application-map-factory.h"
#include "bz-entry.h"
#include "bz-result.h"

G_BEGIN_DECLS

#define BZ_TYPE_ENTRY_GROUP (bz_entry_group_get_type ())
G_DECLARE_FINAL_TYPE (BzEntryGroup, bz_entry_group, BZ, ENTRY_GROUP, GObject)

BzEntryGroup *
bz_entry_group_new (BzApplicationMapFactory *factory);

BzEntryGroup *
bz_entry_group_new_for_single_entry (BzEntry *entry);

/* Only necessary if reading props from another thread, writing is always
   prohibited */
GMutexLocker *
bz_entry_group_lock (BzEntryGroup *self);

GListModel *
bz_entry_group_get_model (BzEntryGroup *self);

GListModel *
bz_entry_group_get_installed_versions (BzEntryGroup *self);

const char *
bz_entry_group_get_id (BzEntryGroup *self);

const char *
bz_entry_group_get_title (BzEntryGroup *self);

const char *
bz_entry_group_get_developer (BzEntryGroup *self);

const char *
bz_entry_group_get_description (BzEntryGroup *self);

GIcon *
bz_entry_group_get_mini_icon (BzEntryGroup *self);

gboolean
bz_entry_group_get_is_floss (BzEntryGroup *self);

const char *
bz_entry_group_get_light_accent_color (BzEntryGroup *self);

const char *
bz_entry_group_get_dark_accent_color (BzEntryGroup *self);

gboolean
bz_entry_group_get_is_flathub (BzEntryGroup *self);

gboolean
bz_entry_group_get_is_verified (BzEntryGroup *self);

const char *
bz_entry_group_get_search_tokens (BzEntryGroup *self);

const char *
bz_entry_group_get_eol (BzEntryGroup *self);

guint64
bz_entry_group_get_installed_size (BzEntryGroup *self);

GListModel *
bz_entry_group_get_addon_group_ids (BzEntryGroup *self);

void
bz_entry_group_append_addon_group_id (BzEntryGroup *self,
                                      const char   *id);

int
bz_entry_group_get_n_addons (BzEntryGroup *self);

const char *
bz_entry_group_get_donation_url (BzEntryGroup *self);

gboolean
bz_entry_group_has_category (BzEntryGroup *self,
                             const char   *name);

BzCategoryFlags
bz_entry_group_get_categories (BzEntryGroup *self);

int
bz_entry_group_get_content_age_rating (BzEntryGroup *self);

BzResult *
bz_entry_group_dup_ui_entry (BzEntryGroup *self);

char *
bz_entry_group_dup_ui_entry_id (BzEntryGroup *self);

int
bz_entry_group_get_installable (BzEntryGroup *self);

int
bz_entry_group_get_updatable (BzEntryGroup *self);

int
bz_entry_group_get_removable (BzEntryGroup *self);

int
bz_entry_group_get_installable_and_available (BzEntryGroup *self);

int
bz_entry_group_get_updatable_and_available (BzEntryGroup *self);

int
bz_entry_group_get_removable_and_available (BzEntryGroup *self);

gboolean
bz_entry_group_is_searchable (BzEntryGroup *self);

gboolean
bz_entry_group_is_addon (BzEntryGroup *self);

guint64
bz_entry_group_get_user_data_size (BzEntryGroup *self);

guint64
bz_entry_group_get_cache_size (BzEntryGroup *self);

void
bz_entry_group_reap_user_data (BzEntryGroup *self);

void
bz_entry_group_reap_user_cache (BzEntryGroup *self);

void
bz_entry_group_add (BzEntryGroup *self,
                    BzEntry      *entry,
                    BzEntry      *runtime,
                    gboolean      ignore_eol);

void
bz_entry_group_connect_living (BzEntryGroup *self,
                               BzEntry      *entry);

DexFuture *
bz_entry_group_dup_all_into_store (BzEntryGroup *self);

G_END_DECLS
