/* bz-entry.h
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

#include <appstream.h>
#include <gtk/gtk.h>
#include <libdex.h>

#include "bz-category-flags.h"
#include "bz-repository.h"

G_BEGIN_DECLS

typedef enum
{
  BZ_ENTRY_KIND_APPLICATION = 1 << 0,
  BZ_ENTRY_KIND_RUNTIME     = 1 << 1,
  BZ_ENTRY_KIND_ADDON       = 1 << 2,
} BzEntryKind;

GType bz_entry_kind_get_type (void);
#define BZ_TYPE_ENTRY_KIND (bz_entry_kind_get_type ())

typedef enum
{
  BZ_CONTROL_NONE      = 0,
  BZ_CONTROL_POINTING  = 1 << 0,
  BZ_CONTROL_KEYBOARD  = 1 << 1,
  BZ_CONTROL_CONSOLE   = 1 << 2,
  BZ_CONTROL_TABLET    = 1 << 3,
  BZ_CONTROL_TOUCH     = 1 << 4,
  BZ_CONTROL_GAMEPAD   = 1 << 5,
  BZ_CONTROL_TV_REMOTE = 1 << 6,
  BZ_CONTROL_VOICE     = 1 << 7,
  BZ_CONTROL_VISION    = 1 << 8,
} BzControlType;

GType bz_control_type_get_type (void);
#define BZ_TYPE_CONTROL_TYPE (bz_control_type_get_type ())

typedef enum
{
  BZ_RELATION_REQUIRES,
  BZ_RELATION_RECOMMENDS,
  BZ_RELATION_SUPPORTS,
} BzRelationType;

GType bz_relation_type_get_type (void);
#define BZ_TYPE_RELATION_TYPE (bz_relation_type_get_type ())

#define BZ_TYPE_ENTRY (bz_entry_get_type ())
G_DECLARE_DERIVABLE_TYPE (BzEntry, bz_entry, BZ, ENTRY, GObject)

struct _BzEntryClass
{
  GObjectClass parent_class;
};

void
bz_entry_hold (BzEntry *self);

void
bz_entry_release (BzEntry *self);

gboolean
bz_entry_is_holding (BzEntry *self);

gboolean
bz_entry_is_installed (BzEntry *self);

const char *
bz_entry_get_installed_version (BzEntry *self);

void
bz_entry_set_installed_version (BzEntry    *self,
                                const char *version);

void
bz_entry_set_installed (BzEntry *self,
                        gboolean installed);

gboolean
bz_entry_is_reinstallable (BzEntry *self);

gboolean
bz_entry_is_searchable (BzEntry *self);

gboolean
bz_entry_is_of_kinds (BzEntry *self,
                      guint    kinds);

void
bz_entry_append_addon (BzEntry    *self,
                       const char *id);

GListModel *
bz_entry_get_addons (BzEntry *self);

const char *
bz_entry_get_id (BzEntry *self);

const char *
bz_entry_get_unique_id (BzEntry *self);

const char *
bz_entry_get_unique_id_checksum (BzEntry *self);

const char *
bz_entry_get_title (BzEntry *self);

const char *
bz_entry_get_developer (BzEntry *self);

GList *
bz_entry_get_developer_apps (BzEntry *self);

const char *
bz_entry_get_eol (BzEntry *self);

const char *
bz_entry_get_description (BzEntry *self);

gboolean bz_entry_is_verified (BzEntry *self);

const char *
bz_entry_get_long_description (BzEntry *self);

const char *
bz_entry_get_remote_repo_name (BzEntry *self);

guint64
bz_entry_get_size (BzEntry *self);

guint64
bz_entry_get_installed_size (BzEntry *self);

GdkPaintable *
bz_entry_get_icon_paintable (BzEntry *self);

GListModel *
bz_entry_get_screenshot_paintables (BzEntry *self);

GIcon *
bz_entry_get_mini_icon (BzEntry *self);

const char *
bz_entry_get_search_tokens (BzEntry *self);

GListModel *
bz_entry_get_share_urls (BzEntry *self);

const char *
bz_entry_get_url (BzEntry *self);

const char *
bz_entry_get_donation_url (BzEntry *self);

const char *
bz_entry_get_forge_url (BzEntry *self);

BzRepository *
bz_entry_get_repository (BzEntry    *self,
                         GListModel *repos);

gboolean
bz_entry_get_is_foss (BzEntry *self);

const char *
bz_entry_get_light_accent_color (BzEntry *self);

const char *
bz_entry_get_dark_accent_color (BzEntry *self);

gboolean
bz_entry_get_is_flathub (BzEntry *self);

gboolean
bz_entry_get_is_mobile_friendly (BzEntry *self);

guint
bz_entry_get_required_controls (BzEntry *self);

guint
bz_entry_get_recommended_controls (BzEntry *self);

guint
bz_entry_get_supported_controls (BzEntry *self);

gboolean
bz_entry_has_control (BzEntry       *self,
                      BzControlType  control,
                      BzRelationType relation);

gint
bz_entry_get_min_display_length (BzEntry *self);

gint
bz_entry_get_max_display_length (BzEntry *self);

gboolean
bz_entry_supports_form_factor (BzEntry *self,
                               guint    available_controls,
                               gint     display_length);

gint
bz_entry_get_age_rating (BzEntry *self);

AsContentRating *
bz_entry_get_content_rating (BzEntry *self);

BzCategoryFlags
bz_entry_get_category_flags (BzEntry *self);

gint
bz_entry_calc_usefulness (BzEntry *self);

void
bz_entry_serialize (BzEntry         *self,
                    GVariantBuilder *builder);

gboolean
bz_entry_deserialize (BzEntry  *self,
                      GVariant *import,
                      GError  **error);

GIcon *
bz_load_mini_icon_sync (const char *unique_id_checksum,
                        const char *path);

G_END_DECLS
