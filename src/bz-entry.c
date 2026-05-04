/* bz-entry.c
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

/* TODO: clean this mess up */

#define G_LOG_DOMAIN  "BAZAAR::ENTRY"
#define BAZAAR_MODULE "entry"

#include <json-glib/json-glib.h>

#include "bz-app-permissions.h"
#include "bz-async-texture.h"
#include "bz-category-flags.h"
#include "bz-country-data-point.h"
#include "bz-data-point.h"
#include "bz-entry.h"
#include "bz-env.h"
#include "bz-global-net.h"
#include "bz-io.h"
#include "bz-release.h"
#include "bz-repository.h"
#include "bz-serializable.h"
#include "bz-url.h"
#include "bz-util.h"
#include "bz-verification-status.h"

G_DEFINE_FLAGS_TYPE (
    BzEntryKind,
    bz_entry_kind,
    G_DEFINE_ENUM_VALUE (BZ_ENTRY_KIND_APPLICATION, "application"),
    G_DEFINE_ENUM_VALUE (BZ_ENTRY_KIND_RUNTIME, "runtime"),
    G_DEFINE_ENUM_VALUE (BZ_ENTRY_KIND_ADDON, "addon"))

G_DEFINE_FLAGS_TYPE (
    BzControlType,
    bz_control_type,
    G_DEFINE_ENUM_VALUE (BZ_CONTROL_NONE, "none"),
    G_DEFINE_ENUM_VALUE (BZ_CONTROL_POINTING, "pointing"),
    G_DEFINE_ENUM_VALUE (BZ_CONTROL_KEYBOARD, "keyboard"),
    G_DEFINE_ENUM_VALUE (BZ_CONTROL_CONSOLE, "console"),
    G_DEFINE_ENUM_VALUE (BZ_CONTROL_TABLET, "tablet"),
    G_DEFINE_ENUM_VALUE (BZ_CONTROL_TOUCH, "touch"),
    G_DEFINE_ENUM_VALUE (BZ_CONTROL_GAMEPAD, "gamepad"),
    G_DEFINE_ENUM_VALUE (BZ_CONTROL_TV_REMOTE, "tv-remote"),
    G_DEFINE_ENUM_VALUE (BZ_CONTROL_VOICE, "voice"),
    G_DEFINE_ENUM_VALUE (BZ_CONTROL_VISION, "vision"))

G_DEFINE_ENUM_TYPE (
    BzRelationType,
    bz_relation_type,
    G_DEFINE_ENUM_VALUE (BZ_RELATION_REQUIRES, "requires"),
    G_DEFINE_ENUM_VALUE (BZ_RELATION_RECOMMENDS, "recommends"),
    G_DEFINE_ENUM_VALUE (BZ_RELATION_SUPPORTS, "supports"))

typedef struct
{
  gint     hold;
  gboolean installed;
  char    *installed_version;
  gboolean reinstallable;
  gboolean searchable;

  guint             kinds;
  GListModel       *addons;
  char             *id;
  char             *unique_id;
  char             *unique_id_checksum;
  char             *title;
  char             *eol;
  char             *description;
  char             *long_description;
  char             *remote_repo_name;
  char             *url;
  guint64           size;
  guint64           installed_size;
  GdkPaintable     *icon_paintable;
  GIcon            *mini_icon;
  GdkPaintable     *remote_repo_icon;
  char             *search_tokens;
  char             *metadata_license;
  char             *project_license;
  gboolean          is_floss;
  char             *project_group;
  char             *developer;
  char             *developer_id;
  GListModel       *developer_apps;
  GListModel       *screenshot_paintables;
  GListModel       *screenshot_captions;
  GdkPaintable     *thumbnail_paintable;
  GListModel       *share_urls;
  char             *donation_url;
  char             *forge_url;
  char             *ratings_summary;
  GListModel       *version_history;
  char             *light_accent_color;
  char             *dark_accent_color;
  gboolean          is_mobile_friendly;
  guint             required_controls;
  guint             recommended_controls;
  guint             supported_controls;
  gint              min_display_length;
  gint              max_display_length;
  AsContentRating  *content_rating;
  GListModel       *keywords;
  BzCategoryFlags   categories;
  BzAppPermissions *permissions;

  gboolean              is_flathub;
  BzVerificationStatus *verification_status;
  GListModel           *download_stats;
  GListModel           *download_stats_per_country;
  int                   recent_downloads;
  int                   total_downloads;
  int                   favorites_count;

  GHashTable *flathub_prop_queries;
} BzEntryPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (BzEntry, bz_entry, G_TYPE_OBJECT);

enum
{
  PROP_0,

  PROP_HOLDING,
  PROP_INSTALLED,
  PROP_INSTALLED_VERSION,
  PROP_REINSTALLABLE,
  PROP_SEARCHABLE,
  PROP_KINDS,
  PROP_ADDONS,
  PROP_ID,
  PROP_UNIQUE_ID,
  PROP_UNIQUE_ID_CHECKSUM,
  PROP_TITLE,
  PROP_EOL,
  PROP_DESCRIPTION,
  PROP_DOWNLOAD_STATS_PER_COUNTRY,
  PROP_LONG_DESCRIPTION,
  PROP_REMOTE_REPO_NAME,
  PROP_URL,
  PROP_SIZE,
  PROP_INSTALLED_SIZE,
  PROP_ICON_PAINTABLE,
  PROP_MINI_ICON,
  PROP_SEARCH_TOKENS,
  PROP_REMOTE_REPO_ICON,
  PROP_METADATA_LICENSE,
  PROP_PROJECT_LICENSE,
  PROP_IS_FLOSS,
  PROP_PROJECT_GROUP,
  PROP_DEVELOPER,
  PROP_DEVELOPER_ID,
  PROP_DEVELOPER_APPS,
  PROP_SCREENSHOT_PAINTABLES,
  PROP_SCREENSHOT_CAPTIONS,
  PROP_THUMBNAIL_PAINTABLE,
  PROP_SHARE_URLS,
  PROP_DONATION_URL,
  PROP_FORGE_URL,
  PROP_RATINGS_SUMMARY,
  PROP_VERSION_HISTORY,
  PROP_IS_FLATHUB,
  PROP_VERIFICATION_STATUS,
  PROP_DOWNLOAD_STATS,
  PROP_RECENT_DOWNLOADS,
  PROP_TOTAL_DOWNLOADS,
  PROP_FAVORITES_COUNT,
  PROP_LIGHT_ACCENT_COLOR,
  PROP_DARK_ACCENT_COLOR,
  PROP_IS_MOBILE_FRIENDLY,
  PROP_REQUIRED_CONTROLS,
  PROP_RECOMMENDED_CONTROLS,
  PROP_SUPPORTED_CONTROLS,
  PROP_MIN_DISPLAY_LENGTH,
  PROP_MAX_DISPLAY_LENGTH,
  PROP_CONTENT_RATING,
  PROP_KEYWORDS,
  PROP_CATEGORIES,
  PROP_PERMISSIONS,

  LAST_PROP
};
static GParamSpec *props[LAST_PROP] = { 0 };

BZ_DEFINE_DATA (
    query_flathub,
    QueryFlathub,
    {
      GWeakRef self;
      int      prop;
      char    *id;
      char    *developer;
    },
    g_weak_ref_clear (&self->self);
    BZ_RELEASE_DATA (id, g_free);
    BZ_RELEASE_DATA (developer, g_free));
static DexFuture *
query_flathub_fiber (QueryFlathubData *data);
static DexFuture *
query_flathub_then (DexFuture        *future,
                    QueryFlathubData *data);

static void
query_flathub (BzEntry *self,
               int      prop);

static void
download_stats_per_day_foreach (JsonObject  *object,
                                const gchar *member_name,
                                JsonNode    *member_node,
                                GListStore  *store);
static void
download_stats_per_country_foreach (JsonObject  *object,
                                    const gchar *member_name,
                                    JsonNode    *member_node,
                                    GListStore  *store);

static gboolean
maybe_save_paintable (BzEntryPrivate  *priv,
                      const char      *key,
                      GdkPaintable    *paintable,
                      GVariantBuilder *builder);

static GdkPaintable *
make_async_texture (GVariant *parse);

static void
clear_entry (BzEntry *self);

static void
bz_entry_dispose (GObject *object)
{
  BzEntry *self = BZ_ENTRY (object);

  clear_entry (self);

  G_OBJECT_CLASS (bz_entry_parent_class)->dispose (object);
}

static void
bz_entry_get_property (GObject    *object,
                       guint       prop_id,
                       GValue     *value,
                       GParamSpec *pspec)
{
  BzEntry        *self = BZ_ENTRY (object);
  BzEntryPrivate *priv = bz_entry_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_HOLDING:
      g_value_set_boolean (value, bz_entry_is_holding (self));
      break;
    case PROP_INSTALLED:
      g_value_set_boolean (value, priv->installed);
      break;
    case PROP_INSTALLED_VERSION:
      g_value_set_string (value, priv->installed_version);
      break;
    case PROP_REINSTALLABLE:
      g_value_set_boolean (value, priv->reinstallable);
      break;
    case PROP_SEARCHABLE:
      g_value_set_boolean (value, priv->searchable);
      break;
    case PROP_ADDONS:
      g_value_set_object (value, priv->addons);
      break;
    case PROP_KINDS:
      g_value_set_flags (value, priv->kinds);
      break;
    case PROP_ID:
      g_value_set_string (value, priv->id);
      break;
    case PROP_UNIQUE_ID:
      g_value_set_string (value, priv->unique_id);
      break;
    case PROP_UNIQUE_ID_CHECKSUM:
      g_value_set_string (value, priv->unique_id_checksum);
      break;
    case PROP_TITLE:
      g_value_set_string (value, priv->title);
      break;
    case PROP_EOL:
      g_value_set_string (value, priv->eol);
      break;
    case PROP_DESCRIPTION:
      g_value_set_string (value, priv->description);
      break;
    case PROP_LONG_DESCRIPTION:
      g_value_set_string (value, priv->long_description);
      break;
    case PROP_REMOTE_REPO_NAME:
      g_value_set_string (value, priv->remote_repo_name);
      break;
    case PROP_URL:
      g_value_set_string (value, priv->url);
      break;
    case PROP_SIZE:
      g_value_set_uint64 (value, priv->size);
      break;
    case PROP_INSTALLED_SIZE:
      g_value_set_uint64 (value, priv->installed_size);
      break;
    case PROP_ICON_PAINTABLE:
      g_value_set_object (value, priv->icon_paintable);
      break;
    case PROP_MINI_ICON:
      g_value_set_object (value, priv->mini_icon);
      break;
    case PROP_SEARCH_TOKENS:
      g_value_set_boxed (value, priv->search_tokens);
      break;
    case PROP_REMOTE_REPO_ICON:
      g_value_set_object (value, priv->remote_repo_icon);
      break;
    case PROP_METADATA_LICENSE:
      g_value_set_string (value, priv->metadata_license);
      break;
    case PROP_PROJECT_LICENSE:
      g_value_set_string (value, priv->project_license);
      break;
    case PROP_IS_FLOSS:
      g_value_set_boolean (value, priv->is_floss);
      break;
    case PROP_PROJECT_GROUP:
      g_value_set_string (value, priv->project_group);
      break;
    case PROP_DEVELOPER:
      g_value_set_string (value, priv->developer);
      break;
    case PROP_DEVELOPER_ID:
      g_value_set_string (value, priv->developer_id);
      break;
    case PROP_DEVELOPER_APPS:
      query_flathub (self, PROP_DEVELOPER_APPS);
      g_value_set_object (value, priv->developer_apps);
      break;
    case PROP_SCREENSHOT_PAINTABLES:
      g_value_set_object (value, priv->screenshot_paintables);
      break;
    case PROP_SCREENSHOT_CAPTIONS:
      g_value_set_object (value, priv->screenshot_captions);
      break;
    case PROP_THUMBNAIL_PAINTABLE:
      g_value_set_object (value, priv->thumbnail_paintable);
      break;
    case PROP_SHARE_URLS:
      g_value_set_object (value, priv->share_urls);
      break;
    case PROP_DONATION_URL:
      g_value_set_string (value, priv->donation_url);
      break;
    case PROP_FORGE_URL:
      g_value_set_string (value, priv->forge_url);
      break;
    case PROP_RATINGS_SUMMARY:
      g_value_set_string (value, priv->ratings_summary);
      break;
    case PROP_VERSION_HISTORY:
      g_value_set_object (value, priv->version_history);
      break;
    case PROP_LIGHT_ACCENT_COLOR:
      g_value_set_string (value, priv->light_accent_color);
      break;
    case PROP_DARK_ACCENT_COLOR:
      g_value_set_string (value, priv->dark_accent_color);
      break;
    case PROP_IS_MOBILE_FRIENDLY:
      g_value_set_boolean (value, priv->is_mobile_friendly);
      break;
    case PROP_REQUIRED_CONTROLS:
      g_value_set_flags (value, priv->required_controls);
      break;
    case PROP_RECOMMENDED_CONTROLS:
      g_value_set_flags (value, priv->recommended_controls);
      break;
    case PROP_SUPPORTED_CONTROLS:
      g_value_set_flags (value, priv->supported_controls);
      break;
    case PROP_MIN_DISPLAY_LENGTH:
      g_value_set_int (value, priv->min_display_length);
      break;
    case PROP_MAX_DISPLAY_LENGTH:
      g_value_set_int (value, priv->max_display_length);
      break;
    case PROP_CONTENT_RATING:
      g_value_set_object (value, priv->content_rating);
      break;
    case PROP_KEYWORDS:
      g_value_set_object (value, priv->keywords);
      break;
    case PROP_CATEGORIES:
      g_value_set_uint (value, priv->categories);
      break;
    case PROP_PERMISSIONS:
      g_value_set_object (value, priv->permissions);
      break;
    case PROP_IS_FLATHUB:
      g_value_set_boolean (value, priv->is_flathub);
      break;
    case PROP_VERIFICATION_STATUS:
      g_value_set_object (value, priv->verification_status);
      break;
    case PROP_DOWNLOAD_STATS:
      query_flathub (self, PROP_DOWNLOAD_STATS);
      g_value_set_object (value, priv->download_stats);
      break;
    case PROP_DOWNLOAD_STATS_PER_COUNTRY:
      query_flathub (self, PROP_DOWNLOAD_STATS_PER_COUNTRY);
      g_value_set_object (value, priv->download_stats_per_country);
      break;
    case PROP_RECENT_DOWNLOADS:
      query_flathub (self, PROP_RECENT_DOWNLOADS);
      g_value_set_int (value, priv->recent_downloads);
      break;
    case PROP_TOTAL_DOWNLOADS:
      query_flathub (self, PROP_TOTAL_DOWNLOADS);
      g_value_set_int (value, priv->total_downloads);
      break;
    case PROP_FAVORITES_COUNT:
      query_flathub (self, PROP_FAVORITES_COUNT);
      g_value_set_int (value, priv->favorites_count);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_entry_set_property (GObject      *object,
                       guint         prop_id,
                       const GValue *value,
                       GParamSpec   *pspec)
{
  BzEntry        *self = BZ_ENTRY (object);
  BzEntryPrivate *priv = bz_entry_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_INSTALLED:
      priv->installed = g_value_get_boolean (value);
      break;
    case PROP_INSTALLED_VERSION:
      g_clear_pointer (&priv->installed_version, g_free);
      priv->installed_version = g_value_dup_string (value);
      break;
    case PROP_REINSTALLABLE:
      priv->reinstallable = g_value_get_boolean (value);
      break;
    case PROP_SEARCHABLE:
      priv->searchable = g_value_get_boolean (value);
      break;
    case PROP_ADDONS:
      g_clear_object (&priv->addons);
      priv->addons = g_value_dup_object (value);
      break;
    case PROP_KINDS:
      priv->kinds = g_value_get_flags (value);
      break;
    case PROP_ID:
      g_clear_pointer (&priv->id, g_free);
      priv->id = g_value_dup_string (value);
      break;
    case PROP_UNIQUE_ID:
      g_clear_pointer (&priv->unique_id, g_free);
      priv->unique_id = g_value_dup_string (value);
      break;
    case PROP_UNIQUE_ID_CHECKSUM:
      g_clear_pointer (&priv->unique_id_checksum, g_free);
      priv->unique_id_checksum = g_value_dup_string (value);
      break;
    case PROP_TITLE:
      g_clear_pointer (&priv->title, g_free);
      priv->title = g_value_dup_string (value);
      break;
    case PROP_EOL:
      g_clear_pointer (&priv->eol, g_free);
      priv->eol = g_value_dup_string (value);
      break;
    case PROP_DESCRIPTION:
      g_clear_pointer (&priv->description, g_free);
      priv->description = g_value_dup_string (value);
      break;
    case PROP_LONG_DESCRIPTION:
      g_clear_pointer (&priv->long_description, g_free);
      priv->long_description = g_value_dup_string (value);
      break;
    case PROP_REMOTE_REPO_NAME:
      g_clear_pointer (&priv->remote_repo_name, g_free);
      priv->remote_repo_name = g_value_dup_string (value);
      priv->is_flathub       = g_strcmp0 (priv->remote_repo_name, "flathub") == 0;
      g_object_notify_by_pspec (object, props[PROP_IS_FLATHUB]);
      break;
    case PROP_URL:
      g_clear_pointer (&priv->url, g_free);
      priv->url = g_value_dup_string (value);
      break;
    case PROP_SIZE:
      priv->size = g_value_get_uint64 (value);
      break;
    case PROP_INSTALLED_SIZE:
      priv->installed_size = g_value_get_uint64 (value);
      break;
    case PROP_ICON_PAINTABLE:
      g_clear_object (&priv->icon_paintable);
      priv->icon_paintable = g_value_dup_object (value);
      break;
    case PROP_MINI_ICON:
      g_clear_object (&priv->mini_icon);
      priv->mini_icon = g_value_dup_object (value);
      break;
    case PROP_SEARCH_TOKENS:
      g_clear_pointer (&priv->search_tokens, g_free);
      priv->search_tokens = g_value_dup_string (value);
      break;
    case PROP_REMOTE_REPO_ICON:
      g_clear_object (&priv->remote_repo_icon);
      priv->remote_repo_icon = g_value_dup_object (value);
      break;
    case PROP_METADATA_LICENSE:
      g_clear_pointer (&priv->metadata_license, g_free);
      priv->metadata_license = g_value_dup_string (value);
      break;
    case PROP_PROJECT_LICENSE:
      g_clear_pointer (&priv->project_license, g_free);
      priv->project_license = g_value_dup_string (value);
      break;
    case PROP_IS_FLOSS:
      priv->is_floss = g_value_get_boolean (value);
      break;
    case PROP_PROJECT_GROUP:
      g_clear_pointer (&priv->project_group, g_free);
      priv->project_group = g_value_dup_string (value);
      break;
    case PROP_DEVELOPER:
      g_clear_pointer (&priv->developer, g_free);
      priv->developer = g_value_dup_string (value);
      break;
    case PROP_DEVELOPER_ID:
      g_clear_pointer (&priv->developer_id, g_free);
      priv->developer_id = g_value_dup_string (value);
      break;
    case PROP_DEVELOPER_APPS:
      g_clear_object (&priv->developer_apps);
      priv->developer_apps = g_value_dup_object (value);
      break;
    case PROP_SCREENSHOT_PAINTABLES:
      g_clear_object (&priv->screenshot_paintables);
      priv->screenshot_paintables = g_value_dup_object (value);
      break;
    case PROP_SCREENSHOT_CAPTIONS:
      g_clear_object (&priv->screenshot_captions);
      priv->screenshot_captions = g_value_dup_object (value);
      break;
    case PROP_THUMBNAIL_PAINTABLE:
      g_clear_object (&priv->thumbnail_paintable);
      priv->thumbnail_paintable = g_value_dup_object (value);
      break;
    case PROP_SHARE_URLS:
      g_clear_object (&priv->share_urls);
      priv->share_urls = g_value_dup_object (value);
      break;
    case PROP_DONATION_URL:
      g_clear_pointer (&priv->donation_url, g_free);
      priv->donation_url = g_value_dup_string (value);
      break;
    case PROP_FORGE_URL:
      g_clear_pointer (&priv->forge_url, g_free);
      priv->forge_url = g_value_dup_string (value);
      break;
    case PROP_RATINGS_SUMMARY:
      g_clear_pointer (&priv->ratings_summary, g_free);
      priv->ratings_summary = g_value_dup_string (value);
      break;
    case PROP_VERSION_HISTORY:
      g_clear_object (&priv->version_history);
      priv->version_history = g_value_dup_object (value);
      break;
    case PROP_LIGHT_ACCENT_COLOR:
      g_clear_pointer (&priv->light_accent_color, g_free);
      priv->light_accent_color = g_value_dup_string (value);
      break;
    case PROP_DARK_ACCENT_COLOR:
      g_clear_pointer (&priv->dark_accent_color, g_free);
      priv->dark_accent_color = g_value_dup_string (value);
      break;
    case PROP_IS_MOBILE_FRIENDLY:
      priv->is_mobile_friendly = g_value_get_boolean (value);
      break;
    case PROP_REQUIRED_CONTROLS:
      priv->required_controls = g_value_get_flags (value);
      break;
    case PROP_RECOMMENDED_CONTROLS:
      priv->recommended_controls = g_value_get_flags (value);
      break;
    case PROP_SUPPORTED_CONTROLS:
      priv->supported_controls = g_value_get_flags (value);
      break;
    case PROP_MIN_DISPLAY_LENGTH:
      priv->min_display_length = g_value_get_int (value);
      break;
    case PROP_MAX_DISPLAY_LENGTH:
      priv->max_display_length = g_value_get_int (value);
      break;
    case PROP_CONTENT_RATING:
      g_clear_object (&priv->content_rating);
      priv->content_rating = g_value_dup_object (value);
      break;
    case PROP_KEYWORDS:
      g_clear_object (&priv->keywords);
      priv->keywords = g_value_dup_object (value);
      break;
    case PROP_CATEGORIES:
      priv->categories = g_value_get_uint (value);
      break;
    case PROP_PERMISSIONS:
      g_clear_object (&priv->permissions);
      priv->permissions = g_value_dup_object (value);
      break;
    case PROP_IS_FLATHUB:
      priv->is_flathub = g_value_get_boolean (value);
      break;
    case PROP_VERIFICATION_STATUS:
      g_clear_object (&priv->verification_status);
      priv->verification_status = g_value_dup_object (value);
      break;
    case PROP_DOWNLOAD_STATS:
    case PROP_DOWNLOAD_STATS_PER_COUNTRY:
      {
        if (prop_id == PROP_DOWNLOAD_STATS)
          {
            g_clear_object (&priv->download_stats);
            priv->download_stats = g_value_dup_object (value);
          }
        else
          {
            g_clear_object (&priv->download_stats_per_country);
            priv->download_stats_per_country = g_value_dup_object (value);
          }
      }
      break;
    case PROP_RECENT_DOWNLOADS:
      priv->recent_downloads = g_value_get_int (value);
      break;
    case PROP_TOTAL_DOWNLOADS:
      priv->total_downloads = g_value_get_int (value);
      break;
    case PROP_FAVORITES_COUNT:
      priv->favorites_count = g_value_get_int (value);
      break;
    case PROP_HOLDING:
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_entry_class_init (BzEntryClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = bz_entry_set_property;
  object_class->get_property = bz_entry_get_property;
  object_class->dispose      = bz_entry_dispose;

  props[PROP_HOLDING] =
      g_param_spec_boolean (
          "holding",
          NULL, NULL, FALSE,
          G_PARAM_READABLE);

  props[PROP_INSTALLED] =
      g_param_spec_boolean (
          "installed",
          NULL, NULL, FALSE,
          G_PARAM_READWRITE);

  props[PROP_INSTALLED_VERSION] =
      g_param_spec_string (
          "installed-version",
          NULL, NULL, NULL,
          G_PARAM_READWRITE);

  props[PROP_REINSTALLABLE] =
      g_param_spec_boolean (
          "reinstallable",
          NULL, NULL, TRUE,
          G_PARAM_READWRITE);

  props[PROP_SEARCHABLE] =
      g_param_spec_boolean (
          "searchable",
          NULL, NULL,
          TRUE,
          G_PARAM_READWRITE);

  props[PROP_ADDONS] =
      g_param_spec_object (
          "addons",
          NULL, NULL,
          G_TYPE_LIST_MODEL,
          G_PARAM_READWRITE);

  props[PROP_KINDS] =
      g_param_spec_flags (
          "kinds",
          NULL, NULL,
          BZ_TYPE_ENTRY_KIND, 0,
          G_PARAM_READWRITE);

  props[PROP_ID] =
      g_param_spec_string (
          "id",
          NULL, NULL, NULL,
          G_PARAM_READWRITE);

  props[PROP_UNIQUE_ID] =
      g_param_spec_string (
          "unique-id",
          NULL, NULL, NULL,
          G_PARAM_READWRITE);

  props[PROP_UNIQUE_ID_CHECKSUM] =
      g_param_spec_string (
          "unique-id-checksum",
          NULL, NULL, NULL,
          G_PARAM_READWRITE);

  props[PROP_TITLE] =
      g_param_spec_string (
          "title",
          NULL, NULL, NULL,
          G_PARAM_READWRITE);

  props[PROP_EOL] =
      g_param_spec_string (
          "eol",
          NULL, NULL, NULL,
          G_PARAM_READWRITE);

  props[PROP_DESCRIPTION] =
      g_param_spec_string (
          "description",
          NULL, NULL, NULL,
          G_PARAM_READWRITE);

  props[PROP_LONG_DESCRIPTION] =
      g_param_spec_string (
          "long-description",
          NULL, NULL, NULL,
          G_PARAM_READWRITE);

  props[PROP_URL] =
      g_param_spec_string (
          "url",
          NULL, NULL, NULL,
          G_PARAM_READWRITE);

  props[PROP_REMOTE_REPO_NAME] =
      g_param_spec_string (
          "remote-repo-name",
          NULL, NULL, NULL,
          G_PARAM_READWRITE);

  props[PROP_SIZE] =
      g_param_spec_uint64 (
          "size",
          NULL, NULL,
          0, G_MAXUINT64, 0,
          G_PARAM_READWRITE);

  props[PROP_INSTALLED_SIZE] =
      g_param_spec_uint64 (
          "installed-size",
          NULL, NULL,
          0, G_MAXUINT64, 0,
          G_PARAM_READWRITE);

  props[PROP_ICON_PAINTABLE] =
      g_param_spec_object (
          "icon-paintable",
          NULL, NULL,
          GDK_TYPE_PAINTABLE,
          G_PARAM_READWRITE);

  props[PROP_MINI_ICON] =
      g_param_spec_object (
          "mini-icon",
          NULL, NULL,
          G_TYPE_ICON,
          G_PARAM_READWRITE);

  props[PROP_SEARCH_TOKENS] =
      g_param_spec_string (
          "search-tokens",
          NULL, NULL, NULL,
          G_PARAM_READWRITE);

  props[PROP_REMOTE_REPO_ICON] =
      g_param_spec_object (
          "remote-repo-icon",
          NULL, NULL,
          GDK_TYPE_PAINTABLE,
          G_PARAM_READWRITE);

  props[PROP_METADATA_LICENSE] =
      g_param_spec_string (
          "metadata-license",
          NULL, NULL, NULL,
          G_PARAM_READWRITE);

  props[PROP_PROJECT_LICENSE] =
      g_param_spec_string (
          "project-license",
          NULL, NULL, NULL,
          G_PARAM_READWRITE);

  props[PROP_IS_FLOSS] =
      g_param_spec_boolean (
          "is-floss",
          NULL, NULL, FALSE,
          G_PARAM_READWRITE);

  props[PROP_PROJECT_GROUP] =
      g_param_spec_string (
          "project-group",
          NULL, NULL, NULL,
          G_PARAM_READWRITE);

  props[PROP_DEVELOPER] =
      g_param_spec_string (
          "developer",
          NULL, NULL, NULL,
          G_PARAM_READWRITE);

  props[PROP_DEVELOPER_ID] =
      g_param_spec_string (
          "developer-id",
          NULL, NULL, NULL,
          G_PARAM_READWRITE);

  props[PROP_DEVELOPER_APPS] =
      g_param_spec_object (
          "developer-apps",
          NULL, NULL,
          G_TYPE_LIST_MODEL,
          G_PARAM_READWRITE);

  props[PROP_SCREENSHOT_PAINTABLES] =
      g_param_spec_object (
          "screenshot-paintables",
          NULL, NULL,
          G_TYPE_LIST_MODEL,
          G_PARAM_READWRITE);

  props[PROP_SCREENSHOT_CAPTIONS] =
      g_param_spec_object (
          "screenshot-captions",
          NULL, NULL,
          G_TYPE_LIST_MODEL,
          G_PARAM_READWRITE);

  props[PROP_THUMBNAIL_PAINTABLE] =
      g_param_spec_object (
          "thumbnail-paintable",
          NULL, NULL,
          GDK_TYPE_PAINTABLE,
          G_PARAM_READWRITE);

  props[PROP_SHARE_URLS] =
      g_param_spec_object (
          "share-urls",
          NULL, NULL,
          G_TYPE_LIST_MODEL,
          G_PARAM_READWRITE);

  props[PROP_DONATION_URL] =
      g_param_spec_string (
          "donation-url",
          NULL, NULL, NULL,
          G_PARAM_READWRITE);

  props[PROP_FORGE_URL] =
      g_param_spec_string (
          "forge-url",
          NULL, NULL, NULL,
          G_PARAM_READWRITE);

  props[PROP_RATINGS_SUMMARY] =
      g_param_spec_string (
          "ratings-summary",
          NULL, NULL, NULL,
          G_PARAM_READWRITE);

  props[PROP_VERSION_HISTORY] =
      g_param_spec_object (
          "version-history",
          NULL, NULL,
          G_TYPE_LIST_MODEL,
          G_PARAM_READWRITE);

  props[PROP_LIGHT_ACCENT_COLOR] =
      g_param_spec_string (
          "light-accent-color",
          NULL, NULL, NULL,
          G_PARAM_READWRITE);

  props[PROP_DARK_ACCENT_COLOR] =
      g_param_spec_string (
          "dark-accent-color",
          NULL, NULL, NULL,
          G_PARAM_READWRITE);

  props[PROP_IS_MOBILE_FRIENDLY] =
      g_param_spec_boolean (
          "is-mobile-friendly",
          NULL, NULL,
          FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  props[PROP_REQUIRED_CONTROLS] =
      g_param_spec_flags (
          "required-controls",
          NULL, NULL,
          BZ_TYPE_CONTROL_TYPE,
          BZ_CONTROL_NONE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  props[PROP_RECOMMENDED_CONTROLS] =
      g_param_spec_flags (
          "recommended-controls",
          NULL, NULL,
          BZ_TYPE_CONTROL_TYPE,
          BZ_CONTROL_NONE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  props[PROP_SUPPORTED_CONTROLS] =
      g_param_spec_flags (
          "supported-controls",
          NULL, NULL,
          BZ_TYPE_CONTROL_TYPE,
          BZ_CONTROL_NONE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  props[PROP_MIN_DISPLAY_LENGTH] =
      g_param_spec_int (
          "min-display-length",
          NULL, NULL,
          0, G_MAXINT,
          0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  props[PROP_MAX_DISPLAY_LENGTH] =
      g_param_spec_int (
          "max-display-length",
          NULL, NULL,
          0, G_MAXINT,
          0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  props[PROP_CONTENT_RATING] =
      g_param_spec_object (
          "content-rating",
          NULL, NULL,
          AS_TYPE_CONTENT_RATING,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  props[PROP_KEYWORDS] =
      g_param_spec_object (
          "keywords",
          NULL, NULL,
          G_TYPE_LIST_MODEL,
          G_PARAM_READWRITE);

  props[PROP_CATEGORIES] =
      g_param_spec_uint (
          "categories",
          NULL, NULL,
          0, G_MAXUINT,
          BZ_CATEGORY_FLAGS_NONE,
          G_PARAM_READWRITE);

  props[PROP_PERMISSIONS] =
      g_param_spec_object (
          "permissions",
          NULL, NULL,
          BZ_TYPE_APP_PERMISSIONS,
          G_PARAM_READWRITE);

  props[PROP_IS_FLATHUB] =
      g_param_spec_boolean (
          "is-flathub",
          NULL, NULL, FALSE,
          G_PARAM_READWRITE);

  props[PROP_VERIFICATION_STATUS] =
      g_param_spec_object (
          "verification-status",
          NULL, NULL,
          BZ_TYPE_VERIFICATION_STATUS,
          G_PARAM_READWRITE);

  props[PROP_DOWNLOAD_STATS] =
      g_param_spec_object (
          "download-stats",
          NULL, NULL,
          G_TYPE_LIST_MODEL,
          G_PARAM_READWRITE);

  props[PROP_DOWNLOAD_STATS_PER_COUNTRY] =
      g_param_spec_object (
          "download-stats-per-country",
          NULL, NULL,
          G_TYPE_LIST_MODEL,
          G_PARAM_READWRITE);

  props[PROP_RECENT_DOWNLOADS] =
      g_param_spec_int (
          "recent-downloads",
          NULL, NULL,
          0, G_MAXINT, 0,
          G_PARAM_READWRITE);

  props[PROP_TOTAL_DOWNLOADS] =
      g_param_spec_int (
          "total-downloads",
          NULL, NULL,
          0, G_MAXINT, 0,
          G_PARAM_READWRITE);

  props[PROP_FAVORITES_COUNT] =
      g_param_spec_int (
          "favorites-count",
          NULL, NULL,
          -1, G_MAXINT, -1,
          G_PARAM_READWRITE);

  g_object_class_install_properties (object_class, LAST_PROP, props);
}

static void
bz_entry_init (BzEntry *self)
{
  BzEntryPrivate *priv = bz_entry_get_instance_private (self);

  priv->hold            = 0;
  priv->reinstallable   = TRUE;
  priv->searchable      = TRUE;
  priv->favorites_count = -1;
}

static void
bz_entry_real_serialize (BzSerializable  *serializable,
                         GVariantBuilder *builder)
{
  BzEntry        *self = BZ_ENTRY (serializable);
  BzEntryPrivate *priv = bz_entry_get_instance_private (self);

  g_variant_builder_add (builder, "{sv}", "installed", g_variant_new_boolean (priv->installed));
  if (priv->installed_version != NULL)
    g_variant_builder_add (builder, "{sv}", "installed-version", g_variant_new_string (priv->installed_version));
  g_variant_builder_add (builder, "{sv}", "kinds", g_variant_new_uint32 (priv->kinds));
  g_variant_builder_add (builder, "{sv}", "reinstallable", g_variant_new_boolean (priv->reinstallable));
  g_variant_builder_add (builder, "{sv}", "searchable", g_variant_new_boolean (priv->searchable));
  if (priv->addons != NULL)
    {
      guint n_items = 0;

      n_items = g_list_model_get_n_items (priv->addons);
      if (n_items > 0)
        {
          g_autoptr (GVariantBuilder) sub_builder = NULL;

          sub_builder = g_variant_builder_new (G_VARIANT_TYPE ("as"));
          for (guint i = 0; i < n_items; i++)
            {
              g_autoptr (GtkStringObject) string = NULL;

              string = g_list_model_get_item (priv->addons, i);
              g_variant_builder_add (sub_builder, "s", gtk_string_object_get_string (string));
            }

          g_variant_builder_add (builder, "{sv}", "addons", g_variant_builder_end (sub_builder));
        }
    }
  if (priv->id != NULL)
    g_variant_builder_add (builder, "{sv}", "id", g_variant_new_string (priv->id));
  if (priv->unique_id != NULL)
    g_variant_builder_add (builder, "{sv}", "unique-id", g_variant_new_string (priv->unique_id));
  if (priv->unique_id_checksum != NULL)
    g_variant_builder_add (builder, "{sv}", "unique-id-checksum", g_variant_new_string (priv->unique_id_checksum));
  if (priv->title != NULL)
    g_variant_builder_add (builder, "{sv}", "title", g_variant_new_string (priv->title));
  if (priv->eol != NULL)
    g_variant_builder_add (builder, "{sv}", "eol", g_variant_new_string (priv->eol));
  if (priv->description != NULL)
    g_variant_builder_add (builder, "{sv}", "description", g_variant_new_string (priv->description));
  if (priv->long_description != NULL)
    g_variant_builder_add (builder, "{sv}", "long-description", g_variant_new_string (priv->long_description));
  if (priv->remote_repo_name != NULL)
    g_variant_builder_add (builder, "{sv}", "remote-repo-name", g_variant_new_string (priv->remote_repo_name));
  if (priv->url != NULL)
    g_variant_builder_add (builder, "{sv}", "url", g_variant_new_string (priv->url));
  if (priv->size > 0)
    g_variant_builder_add (builder, "{sv}", "size", g_variant_new_uint64 (priv->size));
  if (priv->installed_size > 0)
    g_variant_builder_add (builder, "{sv}", "installed-size", g_variant_new_uint64 (priv->installed_size));
  if (priv->icon_paintable != NULL)
    maybe_save_paintable (priv, "icon-paintable", priv->icon_paintable, builder);
  if (priv->mini_icon != NULL)
    {
      g_autoptr (GVariant) serialized = NULL;

      serialized = g_icon_serialize (priv->mini_icon);
      g_variant_builder_add (builder, "{sv}", "mini-icon", serialized);
    }
  if (priv->remote_repo_icon != NULL)
    maybe_save_paintable (priv, "remote-repo-icon", priv->remote_repo_icon, builder);
  if (priv->search_tokens != NULL)
    g_variant_builder_add (builder, "{sv}", "search-tokens", g_variant_new_string (priv->search_tokens));
  if (priv->metadata_license != NULL)
    g_variant_builder_add (builder, "{sv}", "metadata-license", g_variant_new_string (priv->metadata_license));
  if (priv->project_license != NULL)
    g_variant_builder_add (builder, "{sv}", "project-license", g_variant_new_string (priv->project_license));
  g_variant_builder_add (builder, "{sv}", "is-floss", g_variant_new_boolean (priv->is_floss));
  if (priv->project_group != NULL)
    g_variant_builder_add (builder, "{sv}", "project-group", g_variant_new_string (priv->project_group));
  if (priv->developer != NULL)
    g_variant_builder_add (builder, "{sv}", "developer", g_variant_new_string (priv->developer));
  if (priv->developer_id != NULL)
    g_variant_builder_add (builder, "{sv}", "developer-id", g_variant_new_string (priv->developer_id));
  if (priv->screenshot_paintables != NULL)
    {
      guint n_items = 0;

      n_items = g_list_model_get_n_items (priv->screenshot_paintables);
      if (n_items > 0)
        {
          g_autoptr (GVariantBuilder) sub_builder = NULL;

          sub_builder = g_variant_builder_new (G_VARIANT_TYPE ("a{sv}"));
          for (guint i = 0; i < n_items; i++)
            {
              g_autoptr (GdkPaintable) paintable = NULL;
              g_autofree char *key               = NULL;

              paintable = g_list_model_get_item (priv->screenshot_paintables, i);
              key       = g_strdup_printf ("screenshot_%d.png", i);

              maybe_save_paintable (priv, key, paintable, sub_builder);
            }

          g_variant_builder_add (builder, "{sv}", "screenshot-paintables", g_variant_builder_end (sub_builder));
        }
    }
  if (priv->screenshot_captions != NULL)
    {
      guint n_items = 0;

      n_items = g_list_model_get_n_items (priv->screenshot_captions);
      if (n_items > 0)
        {
          g_autoptr (GVariantBuilder) sub_builder = NULL;

          sub_builder = g_variant_builder_new (G_VARIANT_TYPE ("as"));
          for (guint i = 0; i < n_items; i++)
            {
              g_autoptr (GtkStringObject) string = NULL;

              string = g_list_model_get_item (priv->screenshot_captions, i);
              g_variant_builder_add (sub_builder, "s", gtk_string_object_get_string (string));
            }

          g_variant_builder_add (builder, "{sv}", "screenshot-captions", g_variant_builder_end (sub_builder));
        }
    }
  if (priv->thumbnail_paintable != NULL)
    maybe_save_paintable (priv, "thumbnail-paintable", priv->thumbnail_paintable, builder);
  if (priv->share_urls != NULL)
    {
      guint n_items = 0;

      n_items = g_list_model_get_n_items (priv->share_urls);
      if (n_items > 0)
        {
          g_autoptr (GVariantBuilder) sub_builder = NULL;

          sub_builder = g_variant_builder_new (G_VARIANT_TYPE ("a(sss)"));
          for (guint i = 0; i < n_items; i++)
            {
              g_autoptr (BzUrl) url = NULL;
              const char *name      = NULL;
              const char *url_str   = NULL;
              const char *icon_name = NULL;

              url       = g_list_model_get_item (priv->share_urls, i);
              name      = bz_url_get_name (url);
              url_str   = bz_url_get_url (url);
              icon_name = bz_url_get_icon_name (url);
              g_variant_builder_add (sub_builder, "(sss)", name, url_str, icon_name ? icon_name : "");
            }
          g_variant_builder_add (builder, "{sv}", "share-urls", g_variant_builder_end (sub_builder));
        }
    }
  if (priv->donation_url != NULL)
    g_variant_builder_add (builder, "{sv}", "donation-url", g_variant_new_string (priv->donation_url));
  if (priv->forge_url != NULL)
    g_variant_builder_add (builder, "{sv}", "forge-url", g_variant_new_string (priv->forge_url));
  if (priv->version_history != NULL)
    {
      guint n_items = 0;

      n_items = g_list_model_get_n_items (priv->version_history);
      if (n_items > 0)
        {
          g_autoptr (GVariantBuilder) sub_builder = NULL;

          sub_builder = g_variant_builder_new (G_VARIANT_TYPE ("a(mstmsms)"));
          for (guint i = 0; i < n_items; i++)
            {
              g_autoptr (BzRelease) release = NULL;
              guint64     timestamp         = 0;
              const char *url               = NULL;
              const char *version           = NULL;
              const char *description       = NULL;

              release     = g_list_model_get_item (priv->version_history, i);
              timestamp   = bz_release_get_timestamp (release);
              url         = bz_release_get_url (release);
              version     = bz_release_get_version (release);
              description = bz_release_get_description (release);

              g_variant_builder_add (
                  sub_builder,
                  "(mstmsms)",
                  description,
                  timestamp,
                  url,
                  version);
            }

          g_variant_builder_add (builder, "{sv}", "version-history", g_variant_builder_end (sub_builder));
        }
    }
  if (priv->light_accent_color != NULL)
    g_variant_builder_add (builder, "{sv}", "light-accent-color", g_variant_new_string (priv->light_accent_color));
  if (priv->dark_accent_color != NULL)
    g_variant_builder_add (builder, "{sv}", "dark-accent-color", g_variant_new_string (priv->dark_accent_color));
  g_variant_builder_add (builder, "{sv}", "is-mobile-friendly", g_variant_new_boolean (priv->is_mobile_friendly));
  if (priv->required_controls != BZ_CONTROL_NONE)
    g_variant_builder_add (builder, "{sv}", "required-controls", g_variant_new_uint32 (priv->required_controls));
  if (priv->recommended_controls != BZ_CONTROL_NONE)
    g_variant_builder_add (builder, "{sv}", "recommended-controls", g_variant_new_uint32 (priv->recommended_controls));
  if (priv->supported_controls != BZ_CONTROL_NONE)
    g_variant_builder_add (builder, "{sv}", "supported-controls", g_variant_new_uint32 (priv->supported_controls));
  if (priv->min_display_length > 0)
    g_variant_builder_add (builder, "{sv}", "min-display-length", g_variant_new_int32 (priv->min_display_length));
  if (priv->max_display_length > 0)
    g_variant_builder_add (builder, "{sv}", "max-display-length", g_variant_new_int32 (priv->max_display_length));
  if (priv->content_rating != NULL)
    {
      const gchar *kind                       = as_content_rating_get_kind (priv->content_rating);
      g_autoptr (GVariantBuilder) sub_builder = NULL;
      g_autofree const gchar **rating_ids     = NULL;

      sub_builder = g_variant_builder_new (G_VARIANT_TYPE ("a(ss)"));
      rating_ids  = as_content_rating_get_all_rating_ids ();

      for (gsize i = 0; rating_ids[i] != NULL; i++)
        {
          AsContentRatingValue value     = as_content_rating_get_value (priv->content_rating, rating_ids[i]);
          const gchar         *value_str = as_content_rating_value_to_string (value);

          if (value != AS_CONTENT_RATING_VALUE_UNKNOWN)
            g_variant_builder_add (sub_builder, "(ss)", rating_ids[i], value_str);
        }

      g_variant_builder_add (builder, "{sv}", "content-rating-kind", g_variant_new_string (kind ? kind : "oars-1.1"));
      g_variant_builder_add (builder, "{sv}", "content-rating-values", g_variant_builder_end (sub_builder));
    }
  if (priv->keywords != NULL)
    {
      guint n_items = 0;

      n_items = g_list_model_get_n_items (priv->keywords);
      if (n_items > 0)
        {
          g_autoptr (GVariantBuilder) sub_builder = NULL;

          sub_builder = g_variant_builder_new (G_VARIANT_TYPE ("as"));
          for (guint i = 0; i < n_items; i++)
            {
              g_autoptr (GtkStringObject) string = NULL;

              string = g_list_model_get_item (priv->keywords, i);
              g_variant_builder_add (sub_builder, "s", gtk_string_object_get_string (string));
            }

          g_variant_builder_add (builder, "{sv}", "keywords", g_variant_builder_end (sub_builder));
        }
    }
  if (priv->categories != BZ_CATEGORY_FLAGS_NONE)
    g_variant_builder_add (builder, "{sv}", "categories",
                           g_variant_new_uint32 (priv->categories));
  if (priv->verification_status != NULL)
    {
      gboolean         verified              = FALSE;
      g_autofree char *method                = NULL;
      g_autofree char *website               = NULL;
      g_autofree char *login_name            = NULL;
      g_autofree char *login_provider        = NULL;
      g_autofree char *timestamp             = NULL;
      gboolean         login_is_organization = FALSE;

      g_object_get (priv->verification_status,
                    "verified", &verified,
                    "method", &method,
                    "website", &website,
                    "login-name", &login_name,
                    "login-provider", &login_provider,
                    "timestamp", &timestamp,
                    "login-is-organization", &login_is_organization,
                    NULL);

      g_variant_builder_add (builder, "{sv}", "verification-verified", g_variant_new_boolean (verified));
      if (method != NULL)
        g_variant_builder_add (builder, "{sv}", "verification-method", g_variant_new_string (method));
      if (website != NULL)
        g_variant_builder_add (builder, "{sv}", "verification-website", g_variant_new_string (website));
      if (login_name != NULL)
        g_variant_builder_add (builder, "{sv}", "verification-login-name", g_variant_new_string (login_name));
      if (login_provider != NULL)
        g_variant_builder_add (builder, "{sv}", "verification-login-provider", g_variant_new_string (login_provider));
      if (timestamp != NULL)
        g_variant_builder_add (builder, "{sv}", "verification-timestamp", g_variant_new_string (timestamp));
      g_variant_builder_add (builder, "{sv}", "verification-login-is-organization", g_variant_new_boolean (login_is_organization));
    }

  if (priv->permissions != NULL)
    {
      bz_app_permissions_serialize (priv->permissions, builder);
    }

  g_variant_builder_add (builder, "{sv}", "is-flathub", g_variant_new_boolean (priv->is_flathub));
  if (priv->is_flathub)
    {
      if (priv->flathub_prop_queries != NULL)
        {
          if (g_hash_table_contains (priv->flathub_prop_queries, GINT_TO_POINTER (PROP_DOWNLOAD_STATS)) &&
              priv->download_stats != NULL)
            {
              guint n_items = 0;

              n_items = g_list_model_get_n_items (priv->download_stats);
              if (n_items > 0)
                {
                  g_autoptr (GVariantBuilder) sub_builder = NULL;

                  sub_builder = g_variant_builder_new (G_VARIANT_TYPE ("a(ddms)"));
                  for (guint i = 0; i < n_items; i++)
                    {
                      g_autoptr (BzDataPoint) point = NULL;
                      double      independent       = 0.0;
                      double      dependent         = 0.0;
                      const char *label             = NULL;

                      point       = g_list_model_get_item (priv->download_stats, i);
                      independent = bz_data_point_get_independent (point);
                      dependent   = bz_data_point_get_dependent (point);
                      label       = bz_data_point_get_label (point);

                      g_variant_builder_add (sub_builder, "(ddms)", independent, dependent, label);
                    }
                  g_variant_builder_add (builder, "{sv}", "download-stats", g_variant_builder_end (sub_builder));
                }
            }
          if (g_hash_table_contains (priv->flathub_prop_queries, GINT_TO_POINTER (PROP_RECENT_DOWNLOADS)))
            g_variant_builder_add (builder, "{sv}", "recent-downloads", g_variant_new_int32 (priv->recent_downloads));
          if (g_hash_table_contains (priv->flathub_prop_queries, GINT_TO_POINTER (PROP_FAVORITES_COUNT)))
            g_variant_builder_add (builder, "{sv}", "favorites-count", g_variant_new_int32 (priv->favorites_count));
        }
    }
}

static gboolean
bz_entry_real_deserialize (BzSerializable *serializable,
                           GVariant       *import,
                           GError        **error)
{
  BzEntry        *self          = BZ_ENTRY (serializable);
  BzEntryPrivate *priv          = bz_entry_get_instance_private (self);
  g_autoptr (GVariantIter) iter = NULL;

  clear_entry (self);

  iter = g_variant_iter_new (import);
  for (;;)
    {
      g_autofree char *key       = NULL;
      g_autoptr (GVariant) value = NULL;

      if (!g_variant_iter_next (iter, "{sv}", &key, &value))
        break;

      if (g_strcmp0 (key, "installed") == 0)
        priv->installed = g_variant_get_boolean (value);
      else if (g_strcmp0 (key, "installed-version") == 0)
        priv->installed_version = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (key, "kinds") == 0)
        priv->kinds = g_variant_get_uint32 (value);
      else if (g_strcmp0 (key, "reinstallable") == 0)
        priv->reinstallable = g_variant_get_boolean (value);
      else if (g_strcmp0 (key, "searchable") == 0)
        priv->searchable = g_variant_get_boolean (value);
      else if (g_strcmp0 (key, "addons") == 0)
        {
          g_autoptr (GListStore) store        = NULL;
          g_autoptr (GVariantIter) addon_iter = NULL;

          store = g_list_store_new (GTK_TYPE_STRING_OBJECT);

          addon_iter = g_variant_iter_new (value);
          for (;;)
            {
              g_autofree char *unique_id         = NULL;
              g_autoptr (GtkStringObject) string = NULL;

              if (!g_variant_iter_next (addon_iter, "s", &unique_id))
                break;
              string = gtk_string_object_new (unique_id);
              g_list_store_append (store, string);
            }

          priv->addons = G_LIST_MODEL (g_steal_pointer (&store));
        }
      else if (g_strcmp0 (key, "id") == 0)
        priv->id = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (key, "unique-id") == 0)
        priv->unique_id = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (key, "unique-id-checksum") == 0)
        priv->unique_id_checksum = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (key, "title") == 0)
        priv->title = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (key, "eol") == 0)
        priv->eol = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (key, "description") == 0)
        priv->description = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (key, "long-description") == 0)
        priv->long_description = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (key, "remote-repo-name") == 0)
        priv->remote_repo_name = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (key, "url") == 0)
        priv->url = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (key, "size") == 0)
        priv->size = g_variant_get_uint64 (value);
      else if (g_strcmp0 (key, "installed-size") == 0)
        priv->installed_size = g_variant_get_uint64 (value);
      else if (g_strcmp0 (key, "icon-paintable") == 0)
        priv->icon_paintable = make_async_texture (value);
      else if (g_strcmp0 (key, "mini-icon") == 0)
        priv->mini_icon = g_icon_deserialize (value);
      else if (g_strcmp0 (key, "remote-repo-icon") == 0)
        priv->remote_repo_icon = make_async_texture (value);
      else if (g_strcmp0 (key, "search-tokens") == 0)
        priv->search_tokens = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (key, "metadata-license") == 0)
        priv->metadata_license = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (key, "project-license") == 0)
        priv->project_license = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (key, "is-floss") == 0)
        priv->is_floss = g_variant_get_boolean (value);
      else if (g_strcmp0 (key, "developer") == 0)
        priv->developer = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (key, "developer-id") == 0)
        priv->developer_id = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (key, "screenshot-paintables") == 0)
        {
          g_autoptr (GListStore) store             = NULL;
          g_autoptr (GVariantIter) screenshot_iter = NULL;

          store = g_list_store_new (BZ_TYPE_ASYNC_TEXTURE);

          screenshot_iter = g_variant_iter_new (value);
          for (;;)
            {
              g_autofree char *basename        = NULL;
              g_autoptr (GVariant) screenshot  = NULL;
              g_autoptr (GdkPaintable) texture = NULL;

              if (!g_variant_iter_next (screenshot_iter, "{sv}", &basename, &screenshot))
                break;
              texture = make_async_texture (screenshot);
              g_list_store_append (store, texture);
            }

          priv->screenshot_paintables = G_LIST_MODEL (g_steal_pointer (&store));
        }
      else if (g_strcmp0 (key, "screenshot-captions") == 0)
        {
          g_autoptr (GListStore) store          = NULL;
          g_autoptr (GVariantIter) caption_iter = NULL;

          store = g_list_store_new (GTK_TYPE_STRING_OBJECT);

          caption_iter = g_variant_iter_new (value);
          for (;;)
            {
              g_autofree char *caption           = NULL;
              g_autoptr (GtkStringObject) string = NULL;

              if (!g_variant_iter_next (caption_iter, "s", &caption))
                break;
              string = gtk_string_object_new (caption);
              g_list_store_append (store, string);
            }

          priv->screenshot_captions = G_LIST_MODEL (g_steal_pointer (&store));
        }
      else if (g_strcmp0 (key, "thumbnail-paintable") == 0)
        priv->thumbnail_paintable = make_async_texture (value);
      else if (g_strcmp0 (key, "share-urls") == 0)
        {
          g_autoptr (GListStore) store      = NULL;
          g_autoptr (GVariantIter) url_iter = NULL;

          store = g_list_store_new (BZ_TYPE_URL);

          url_iter = g_variant_iter_new (value);
          for (;;)
            {
              g_autofree char *name      = NULL;
              g_autofree char *url_str   = NULL;
              g_autoptr (BzUrl) url      = NULL;
              g_autofree char *icon_name = NULL;

              if (!g_variant_iter_next (url_iter, "(sss)", &name, &url_str, &icon_name))
                break;
              url = bz_url_new ();
              bz_url_set_name (url, name);
              bz_url_set_url (url, url_str);
              bz_url_set_icon_name (url, icon_name);
              g_list_store_append (store, url);
            }

          priv->share_urls = G_LIST_MODEL (g_steal_pointer (&store));
        }
      else if (g_strcmp0 (key, "donation-url") == 0)
        priv->donation_url = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (key, "forge-url") == 0)
        priv->forge_url = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (key, "version-history") == 0)
        {
          g_autoptr (GListStore) store          = NULL;
          g_autoptr (GVariantIter) version_iter = NULL;

          store = g_list_store_new (BZ_TYPE_RELEASE);

          version_iter = g_variant_iter_new (value);
          for (;;)
            {
              guint64          timestamp    = 0;
              g_autofree char *url          = NULL;
              g_autofree char *description  = NULL;
              g_autofree char *version      = NULL;
              g_autoptr (BzRelease) release = NULL;

              if (!g_variant_iter_next (version_iter, "(mstmsms)", &description, &timestamp, &url, &version))
                break;

              release = bz_release_new ();
              bz_release_set_timestamp (release, timestamp);
              bz_release_set_url (release, url);
              bz_release_set_version (release, version);
              bz_release_set_description (release, description);
              g_list_store_append (store, release);
            }

          priv->version_history = G_LIST_MODEL (g_steal_pointer (&store));
        }
      else if (g_strcmp0 (key, "light-accent-color") == 0)
        priv->light_accent_color = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (key, "dark-accent-color") == 0)
        priv->dark_accent_color = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (key, "is-mobile-friendly") == 0)
        priv->is_mobile_friendly = g_variant_get_boolean (value);
      else if (g_strcmp0 (key, "required-controls") == 0 && g_variant_is_of_type (value, G_VARIANT_TYPE_UINT32))
        priv->required_controls = g_variant_get_uint32 (value);
      else if (g_strcmp0 (key, "recommended-controls") == 0 && g_variant_is_of_type (value, G_VARIANT_TYPE_UINT32))
        priv->recommended_controls = g_variant_get_uint32 (value);
      else if (g_strcmp0 (key, "supported-controls") == 0 && g_variant_is_of_type (value, G_VARIANT_TYPE_UINT32))
        priv->supported_controls = g_variant_get_uint32 (value);
      else if (g_strcmp0 (key, "min-display-length") == 0)
        priv->min_display_length = g_variant_get_int32 (value);
      else if (g_strcmp0 (key, "max-display-length") == 0)
        priv->max_display_length = g_variant_get_int32 (value);
      else if (g_strcmp0 (key, "content-rating-kind") == 0)
        {
          g_autofree gchar *kind = NULL;

          kind = g_variant_dup_string (value, NULL);

          if (priv->content_rating == NULL)
            priv->content_rating = as_content_rating_new ();

          as_content_rating_set_kind (priv->content_rating, kind);
        }
      else if (g_strcmp0 (key, "content-rating-values") == 0)
        {
          g_autoptr (GVariantIter) rating_iter = NULL;

          if (priv->content_rating == NULL)
            priv->content_rating = as_content_rating_new ();

          rating_iter = g_variant_iter_new (value);
          for (;;)
            {
              g_autofree gchar    *rating_id        = NULL;
              g_autofree gchar    *rating_value_str = NULL;
              AsContentRatingValue rating_value;

              if (!g_variant_iter_next (rating_iter, "(ss)", &rating_id, &rating_value_str))
                break;

              rating_value = as_content_rating_value_from_string (rating_value_str);
              if (rating_value != AS_CONTENT_RATING_VALUE_UNKNOWN)
                as_content_rating_set_value (priv->content_rating, rating_id, rating_value);
            }
        }
      else if (g_strcmp0 (key, "keywords") == 0)
        {
          g_autoptr (GListStore) store           = NULL;
          g_autoptr (GVariantIter) keywords_iter = NULL;

          store = g_list_store_new (GTK_TYPE_STRING_OBJECT);

          keywords_iter = g_variant_iter_new (value);
          for (;;)
            {
              g_autofree char *keyword           = NULL;
              g_autoptr (GtkStringObject) string = NULL;

              if (!g_variant_iter_next (keywords_iter, "s", &keyword))
                break;
              string = gtk_string_object_new (keyword);
              g_list_store_append (store, string);
            }

          priv->keywords = G_LIST_MODEL (g_steal_pointer (&store));
        }
      else if (g_strcmp0 (key, "categories") == 0)
        priv->categories = g_variant_get_uint32 (value);
      else if (g_strcmp0 (key, "verification-verified") == 0)
        {
          if (priv->verification_status == NULL)
            priv->verification_status = bz_verification_status_new ();
          g_object_set (priv->verification_status, "verified", g_variant_get_boolean (value), NULL);
        }
      else if (g_strcmp0 (key, "verification-method") == 0)
        {
          if (priv->verification_status == NULL)
            priv->verification_status = bz_verification_status_new ();
          g_object_set (priv->verification_status, "method", g_variant_get_string (value, NULL), NULL);
        }
      else if (g_strcmp0 (key, "verification-website") == 0)
        {
          if (priv->verification_status == NULL)
            priv->verification_status = bz_verification_status_new ();
          g_object_set (priv->verification_status, "website", g_variant_get_string (value, NULL), NULL);
        }
      else if (g_strcmp0 (key, "verification-login-name") == 0)
        {
          if (priv->verification_status == NULL)
            priv->verification_status = bz_verification_status_new ();
          g_object_set (priv->verification_status, "login-name", g_variant_get_string (value, NULL), NULL);
        }
      else if (g_strcmp0 (key, "verification-login-provider") == 0)
        {
          if (priv->verification_status == NULL)
            priv->verification_status = bz_verification_status_new ();
          g_object_set (priv->verification_status, "login-provider", g_variant_get_string (value, NULL), NULL);
        }
      else if (g_strcmp0 (key, "verification-timestamp") == 0)
        {
          if (priv->verification_status == NULL)
            priv->verification_status = bz_verification_status_new ();
          g_object_set (priv->verification_status, "timestamp", g_variant_get_string (value, NULL), NULL);
        }
      else if (g_strcmp0 (key, "verification-login-is-organization") == 0)
        {
          if (priv->verification_status == NULL)
            priv->verification_status = bz_verification_status_new ();
          g_object_set (priv->verification_status, "login-is-organization", g_variant_get_boolean (value), NULL);
        }
      else if (g_strcmp0 (key, "is-flathub") == 0)
        priv->is_flathub = g_variant_get_boolean (value);
      else if (g_str_has_prefix (key, "permissions-"))
        {
          continue;
        }
    }

  if (priv->permissions == NULL)
    priv->permissions = bz_app_permissions_new ();

  if (!bz_app_permissions_deserialize (priv->permissions, import, error))
    {
      g_warning ("Failed to deserialize app permissions");
    }

  return TRUE;
}

void
bz_entry_hold (BzEntry *self)
{
  BzEntryPrivate *priv = NULL;

  g_return_if_fail (BZ_IS_ENTRY (self));
  priv = bz_entry_get_instance_private (self);

  if (++priv->hold == 1)
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP_HOLDING]);
}

void
bz_entry_release (BzEntry *self)
{
  BzEntryPrivate *priv = NULL;

  g_return_if_fail (BZ_IS_ENTRY (self));
  priv = bz_entry_get_instance_private (self);

  if (--priv->hold == 0)
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP_HOLDING]);
}

gboolean
bz_entry_is_holding (BzEntry *self)
{
  BzEntryPrivate *priv = NULL;

  g_return_val_if_fail (BZ_IS_ENTRY (self), FALSE);
  priv = bz_entry_get_instance_private (self);

  return priv->hold > 0;
}

gboolean
bz_entry_is_installed (BzEntry *self)
{
  BzEntryPrivate *priv = NULL;

  g_return_val_if_fail (BZ_IS_ENTRY (self), FALSE);
  priv = bz_entry_get_instance_private (self);

  return priv->installed;
}

void
bz_entry_set_installed (BzEntry *self,
                        gboolean installed)
{
  BzEntryPrivate *priv = NULL;

  g_return_if_fail (BZ_IS_ENTRY (self));
  priv = bz_entry_get_instance_private (self);

  priv->installed = installed;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_INSTALLED]);
}

const char *
bz_entry_get_installed_version (BzEntry *self)
{
  BzEntryPrivate *priv = NULL;

  g_return_val_if_fail (BZ_IS_ENTRY (self), NULL);
  priv = bz_entry_get_instance_private (self);

  return priv->installed_version;
}

void
bz_entry_set_installed_version (BzEntry    *self,
                                const char *version)
{
  BzEntryPrivate *priv = NULL;

  g_return_if_fail (BZ_IS_ENTRY (self));
  priv = bz_entry_get_instance_private (self);

  g_clear_pointer (&priv->installed_version, g_free);
  priv->installed_version = g_strdup (version);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_INSTALLED_VERSION]);
}

gboolean
bz_entry_is_of_kinds (BzEntry *self,
                      guint    kinds)
{
  BzEntryPrivate *priv = NULL;

  g_return_val_if_fail (BZ_IS_ENTRY (self), FALSE);
  priv = bz_entry_get_instance_private (self);

  return (priv->kinds & kinds) == kinds;
}

gboolean
bz_entry_is_reinstallable (BzEntry *self)
{
  BzEntryPrivate *priv = NULL;

  g_return_val_if_fail (BZ_IS_ENTRY (self), TRUE);
  priv = bz_entry_get_instance_private (self);

  return priv->reinstallable;
}

gboolean
bz_entry_is_searchable (BzEntry *self)
{
  BzEntryPrivate *priv = NULL;

  g_return_val_if_fail (BZ_IS_ENTRY (self), TRUE);
  priv = bz_entry_get_instance_private (self);

  return priv->searchable;
}

void
bz_entry_append_addon (BzEntry    *self,
                       const char *id)
{
  BzEntryPrivate *priv               = NULL;
  g_autoptr (GtkStringObject) string = NULL;

  g_return_if_fail (BZ_IS_ENTRY (self));
  g_return_if_fail (id != NULL);
  priv = bz_entry_get_instance_private (self);

  string = gtk_string_object_new (id);
  if (priv->addons == NULL)
    {
      priv->addons = (GListModel *) g_list_store_new (GTK_TYPE_STRING_OBJECT);
      g_list_store_append (G_LIST_STORE (priv->addons), string);
      g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ADDONS]);
    }
  else
    g_list_store_append (G_LIST_STORE (priv->addons), string);
}

GListModel *
bz_entry_get_addons (BzEntry *self)
{
  BzEntryPrivate *priv = NULL;

  g_return_val_if_fail (BZ_IS_ENTRY (self), NULL);
  priv = bz_entry_get_instance_private (self);

  return priv->addons;
}

const char *
bz_entry_get_id (BzEntry *self)
{
  BzEntryPrivate *priv = NULL;

  g_return_val_if_fail (BZ_IS_ENTRY (self), NULL);
  priv = bz_entry_get_instance_private (self);

  return priv->id;
}

const char *
bz_entry_get_unique_id (BzEntry *self)
{
  BzEntryPrivate *priv = NULL;

  g_return_val_if_fail (BZ_IS_ENTRY (self), NULL);
  priv = bz_entry_get_instance_private (self);

  return priv->unique_id;
}

const char *
bz_entry_get_unique_id_checksum (BzEntry *self)
{
  BzEntryPrivate *priv = NULL;

  g_return_val_if_fail (BZ_IS_ENTRY (self), NULL);
  priv = bz_entry_get_instance_private (self);

  return priv->unique_id_checksum;
}

const char *
bz_entry_get_title (BzEntry *self)
{
  BzEntryPrivate *priv = NULL;

  g_return_val_if_fail (BZ_IS_ENTRY (self), NULL);
  priv = bz_entry_get_instance_private (self);

  return priv->title;
}

const char *
bz_entry_get_developer (BzEntry *self)
{
  BzEntryPrivate *priv = NULL;

  g_return_val_if_fail (BZ_IS_ENTRY (self), NULL);
  priv = bz_entry_get_instance_private (self);

  return priv->developer;
}

gboolean
bz_entry_is_verified (BzEntry *self)
{
  BzEntryPrivate *priv     = NULL;
  gboolean        verified = FALSE;

  g_return_val_if_fail (BZ_IS_ENTRY (self), FALSE);
  priv = bz_entry_get_instance_private (self);

  if (priv->verification_status != NULL)
    g_object_get (priv->verification_status, "verified", &verified, NULL);

  return verified;
}

const char *
bz_entry_get_eol (BzEntry *self)
{
  BzEntryPrivate *priv = NULL;

  g_return_val_if_fail (BZ_IS_ENTRY (self), 0);
  priv = bz_entry_get_instance_private (self);

  return priv->eol;
}

const char *
bz_entry_get_description (BzEntry *self)
{
  BzEntryPrivate *priv = NULL;

  g_return_val_if_fail (BZ_IS_ENTRY (self), NULL);
  priv = bz_entry_get_instance_private (self);

  return priv->description;
}

const char *
bz_entry_get_long_description (BzEntry *self)
{
  BzEntryPrivate *priv = NULL;

  g_return_val_if_fail (BZ_IS_ENTRY (self), NULL);
  priv = bz_entry_get_instance_private (self);

  return priv->long_description;
}

const char *
bz_entry_get_remote_repo_name (BzEntry *self)
{
  BzEntryPrivate *priv = NULL;

  g_return_val_if_fail (BZ_IS_ENTRY (self), NULL);
  priv = bz_entry_get_instance_private (self);

  return priv->remote_repo_name;
}

guint64
bz_entry_get_size (BzEntry *self)
{
  BzEntryPrivate *priv = NULL;

  g_return_val_if_fail (BZ_IS_ENTRY (self), 0);
  priv = bz_entry_get_instance_private (self);

  return priv->size;
}

guint64
bz_entry_get_installed_size (BzEntry *self)
{
  BzEntryPrivate *priv = NULL;

  g_return_val_if_fail (BZ_IS_ENTRY (self), 0);
  priv = bz_entry_get_instance_private (self);

  return priv->installed_size;
}

GdkPaintable *
bz_entry_get_icon_paintable (BzEntry *self)
{
  BzEntryPrivate *priv = NULL;

  g_return_val_if_fail (BZ_IS_ENTRY (self), NULL);
  priv = bz_entry_get_instance_private (self);

  return priv->icon_paintable;
}

GListModel *
bz_entry_get_screenshot_paintables (BzEntry *self)
{
  BzEntryPrivate *priv = NULL;

  g_return_val_if_fail (BZ_IS_ENTRY (self), NULL);
  priv = bz_entry_get_instance_private (self);

  return priv->screenshot_paintables;
}

GIcon *
bz_entry_get_mini_icon (BzEntry *self)
{
  BzEntryPrivate *priv = NULL;

  g_return_val_if_fail (BZ_IS_ENTRY (self), NULL);
  priv = bz_entry_get_instance_private (self);

  return priv->mini_icon;
}

const char *
bz_entry_get_search_tokens (BzEntry *self)
{
  BzEntryPrivate *priv = NULL;

  g_return_val_if_fail (BZ_IS_ENTRY (self), NULL);
  priv = bz_entry_get_instance_private (self);

  return priv->search_tokens;
}

GListModel *
bz_entry_get_share_urls (BzEntry *self)
{
  BzEntryPrivate *priv = NULL;

  g_return_val_if_fail (BZ_IS_ENTRY (self), NULL);
  priv = bz_entry_get_instance_private (self);

  return priv->share_urls;
}

const char *
bz_entry_get_url (BzEntry *self)
{
  BzEntryPrivate *priv = NULL;

  g_return_val_if_fail (BZ_IS_ENTRY (self), NULL);
  priv = bz_entry_get_instance_private (self);

  return priv->url;
}

const char *
bz_entry_get_donation_url (BzEntry *self)
{
  BzEntryPrivate *priv = NULL;

  g_return_val_if_fail (BZ_IS_ENTRY (self), NULL);
  priv = bz_entry_get_instance_private (self);

  return priv->donation_url;
}

const char *
bz_entry_get_forge_url (BzEntry *self)
{
  BzEntryPrivate *priv = NULL;

  g_return_val_if_fail (BZ_IS_ENTRY (self), NULL);
  priv = bz_entry_get_instance_private (self);

  return priv->forge_url;
}

BzRepository *
bz_entry_get_repository (BzEntry    *self,
                         GListModel *repos)
{
  BzEntryPrivate *priv    = NULL;
  guint           n_repos = 0;
  g_auto (GStrv) parts    = NULL;
  const char *scope       = NULL;
  const char *repo_name   = NULL;
  gboolean    is_user     = FALSE;

  priv = bz_entry_get_instance_private (self);

  if (priv->unique_id == NULL)
    return NULL;

  parts = g_strsplit (priv->unique_id, "::", -1);
  if (g_strv_length (parts) < 3)
    return NULL;

  scope     = parts[0];
  repo_name = parts[1];

  is_user = g_strcmp0 (scope, "FLATPAK-USER") == 0;

  n_repos = g_list_model_get_n_items (repos);
  for (guint i = 0; i < n_repos; i++)
    {
      g_autoptr (BzRepository) repo = g_list_model_get_item (repos, i);
      const char *name              = bz_repository_get_name (repo);
      gboolean    repo_is_user      = bz_repository_get_is_user (repo);

      if (repo_is_user == is_user &&
          g_strcmp0 (name, repo_name) == 0)
        return g_object_ref (repo);
    }

  return NULL;
}

gboolean
bz_entry_get_is_foss (BzEntry *self)
{
  BzEntryPrivate *priv = NULL;

  g_return_val_if_fail (BZ_IS_ENTRY (self), FALSE);
  priv = bz_entry_get_instance_private (self);

  return priv->is_floss;
}

const char *
bz_entry_get_light_accent_color (BzEntry *self)
{
  BzEntryPrivate *priv = NULL;

  g_return_val_if_fail (BZ_IS_ENTRY (self), NULL);
  priv = bz_entry_get_instance_private (self);

  return priv->light_accent_color;
}

const char *
bz_entry_get_dark_accent_color (BzEntry *self)
{
  BzEntryPrivate *priv = NULL;

  g_return_val_if_fail (BZ_IS_ENTRY (self), NULL);
  priv = bz_entry_get_instance_private (self);

  return priv->dark_accent_color;
}

gboolean
bz_entry_get_is_mobile_friendly (BzEntry *self)
{
  BzEntryPrivate *priv = bz_entry_get_instance_private (self);

  g_return_val_if_fail (BZ_IS_ENTRY (self), FALSE);

  return priv->is_mobile_friendly;
}

guint
bz_entry_get_required_controls (BzEntry *self)
{
  BzEntryPrivate *priv = bz_entry_get_instance_private (self);

  g_return_val_if_fail (BZ_IS_ENTRY (self), BZ_CONTROL_NONE);

  return priv->required_controls;
}

guint
bz_entry_get_recommended_controls (BzEntry *self)
{
  BzEntryPrivate *priv = bz_entry_get_instance_private (self);

  g_return_val_if_fail (BZ_IS_ENTRY (self), BZ_CONTROL_NONE);

  return priv->recommended_controls;
}

guint
bz_entry_get_supported_controls (BzEntry *self)
{
  BzEntryPrivate *priv = bz_entry_get_instance_private (self);

  g_return_val_if_fail (BZ_IS_ENTRY (self), BZ_CONTROL_NONE);

  return priv->supported_controls;
}

gboolean
bz_entry_has_control (BzEntry       *self,
                      BzControlType  control,
                      BzRelationType relation)
{
  BzEntryPrivate *priv = bz_entry_get_instance_private (self);

  g_return_val_if_fail (BZ_IS_ENTRY (self), FALSE);

  switch (relation)
    {
    case BZ_RELATION_REQUIRES:
      return (priv->required_controls & control) != 0;
    case BZ_RELATION_RECOMMENDS:
      return (priv->recommended_controls & control) != 0;
    case BZ_RELATION_SUPPORTS:
      return (priv->supported_controls & control) != 0;
    default:
      return FALSE;
    }
}

gint
bz_entry_get_min_display_length (BzEntry *self)
{
  BzEntryPrivate *priv = bz_entry_get_instance_private (self);

  g_return_val_if_fail (BZ_IS_ENTRY (self), 0);

  return priv->min_display_length;
}

gint
bz_entry_get_max_display_length (BzEntry *self)
{
  BzEntryPrivate *priv = bz_entry_get_instance_private (self);

  g_return_val_if_fail (BZ_IS_ENTRY (self), 0);

  return priv->max_display_length;
}

gboolean
bz_entry_supports_form_factor (BzEntry *self,
                               guint    available_controls,
                               gint     display_length)
{
  BzEntryPrivate *priv = bz_entry_get_instance_private (self);

  g_return_val_if_fail (BZ_IS_ENTRY (self), FALSE);

  if (priv->required_controls != BZ_CONTROL_NONE)
    {
      if ((priv->required_controls & available_controls) != priv->required_controls)
        return FALSE;
    }

  if (priv->min_display_length > 0 && display_length < priv->min_display_length)
    return FALSE;

  if (priv->max_display_length > 0 && display_length > priv->max_display_length)
    return FALSE;

  return TRUE;
}

AsContentRating *
bz_entry_get_content_rating (BzEntry *self)
{
  BzEntryPrivate *priv = bz_entry_get_instance_private (self);

  g_return_val_if_fail (BZ_IS_ENTRY (self), NULL);

  return priv->content_rating;
}

BzCategoryFlags
bz_entry_get_category_flags (BzEntry *self)
{
  BzEntryPrivate *priv = NULL;

  g_return_val_if_fail (BZ_IS_ENTRY (self), BZ_CATEGORY_FLAGS_NONE);
  priv = bz_entry_get_instance_private (self);

  return priv->categories;
}

gboolean
bz_entry_get_is_flathub (BzEntry *self)
{
  BzEntryPrivate *priv = NULL;

  g_return_val_if_fail (BZ_IS_ENTRY (self), FALSE);
  priv = bz_entry_get_instance_private (self);

  return priv->is_flathub;
}

gint
bz_entry_calc_usefulness (BzEntry *self)
{
  BzEntryPrivate *priv  = NULL;
  gint            score = 0;

  g_return_val_if_fail (BZ_IS_ENTRY (self), FALSE);
  priv = bz_entry_get_instance_private (self);

  score += priv->is_flathub ? 1000 : 0;

  score += priv->title != NULL ? 5 : 0;
  score += priv->description != NULL ? 1 : 0;
  score += priv->long_description != NULL ? 5 : 0;
  score += priv->url != NULL ? 1 : 0;
  score += priv->size > 0 ? 1 : 0;
  score += priv->icon_paintable != NULL ? 15 : 0;
  score += priv->remote_repo_icon != NULL ? 1 : 0;
  score += priv->metadata_license != NULL ? 1 : 0;
  score += priv->project_license != NULL ? 1 : 0;
  score += priv->project_group != NULL ? 1 : 0;
  score += priv->developer != NULL ? 1 : 0;
  score += priv->developer_id != NULL ? 1 : 0;
  score += priv->screenshot_paintables != NULL ? 5 : 0;
  score += priv->share_urls != NULL ? 5 : 0;

  score -= priv->eol != NULL ? 500 : 0;

  return score;
}

void
bz_entry_serialize (BzEntry         *self,
                    GVariantBuilder *builder)
{
  g_return_if_fail (BZ_IS_ENTRY (self));
  g_return_if_fail (builder != NULL);

  return bz_entry_real_serialize (BZ_SERIALIZABLE (self), builder);
}

gboolean
bz_entry_deserialize (BzEntry  *self,
                      GVariant *import,
                      GError  **error)
{
  g_return_val_if_fail (BZ_IS_ENTRY (self), FALSE);
  g_return_val_if_fail (import != NULL, FALSE);

  return bz_entry_real_deserialize (BZ_SERIALIZABLE (self), import, error);
}

GIcon *
bz_load_mini_icon_sync (const char *unique_id_checksum,
                        const char *path)
{
  guint            icon_size          = 0;
  g_autofree char *main_cache         = NULL;
  g_autofree char *mini_icon_basename = NULL;
  g_autofree char *mini_icon_path     = NULL;
  g_autoptr (GBytes) bytes            = NULL;
  cairo_surface_t *surface_in         = NULL;
  int              width              = 0;
  int              height             = 0;
  cairo_surface_t *surface_out        = NULL;
  cairo_t         *cairo              = NULL;
  g_autoptr (GFile) parent_file       = NULL;
  g_autoptr (GFile) mini_icon_file    = NULL;
  g_autoptr (GIcon) mini_icon         = NULL;

  icon_size = bz_get_desktop_search_provider_icon_size ();

  main_cache         = bz_dup_module_dir ();
  mini_icon_basename = g_strdup_printf ("%s-%ux%u", unique_id_checksum, icon_size, icon_size);
  mini_icon_path     = g_build_filename (main_cache, mini_icon_basename, NULL);

  if (g_file_test (mini_icon_path, G_FILE_TEST_EXISTS))
    /* Assume the icon left behind by last writer */
    goto done;

  surface_in = cairo_image_surface_create_from_png (path);
  width      = cairo_image_surface_get_width (surface_in);
  height     = cairo_image_surface_get_height (surface_in);

  surface_out = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, icon_size, icon_size);
  cairo       = cairo_create (surface_out);

  cairo_scale (cairo,
               (double) icon_size / (double) width,
               (double) icon_size / (double) height);
  cairo_set_source_surface (cairo, surface_in, 0, 0);
  cairo_paint (cairo);
  cairo_restore (cairo);

  parent_file = g_file_new_for_path (main_cache);
  g_file_make_directory_with_parents (parent_file, NULL, NULL);

  cairo_surface_flush (surface_out);
  cairo_surface_write_to_png (surface_out, mini_icon_path);
  cairo_destroy (cairo);
  cairo_surface_destroy (surface_in);
  cairo_surface_destroy (surface_out);

done:
  mini_icon_file = g_file_new_for_path (mini_icon_path);
  mini_icon      = g_file_icon_new (mini_icon_file);
  return g_steal_pointer (&mini_icon);
}

static void
query_flathub (BzEntry *self,
               int      prop)
{
  BzEntryPrivate *priv              = NULL;
  g_autoptr (QueryFlathubData) data = NULL;
  g_autoptr (DexFuture) future      = NULL;
  gboolean is_download_stat         = FALSE;

  priv = bz_entry_get_instance_private (self);

  is_download_stat = (prop == PROP_DOWNLOAD_STATS ||
                      prop == PROP_DOWNLOAD_STATS_PER_COUNTRY ||
                      prop == PROP_RECENT_DOWNLOADS ||
                      prop == PROP_TOTAL_DOWNLOADS);

  if (!is_download_stat && !priv->is_flathub)
    return;
  if (priv->id == NULL)
    return;

  if (priv->flathub_prop_queries == NULL)
    priv->flathub_prop_queries = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, dex_unref);
  else if (g_hash_table_contains (priv->flathub_prop_queries, GINT_TO_POINTER (prop)))
    return;

  data = query_flathub_data_new ();
  g_weak_ref_init (&data->self, self);
  data->prop      = prop;
  data->id        = g_strdup (priv->id);
  data->developer = g_strdup (priv->developer);

  future = dex_scheduler_spawn (
      bz_get_io_scheduler (),
      bz_get_dex_stack_size (),
      (DexFiberFunc) query_flathub_fiber,
      query_flathub_data_ref (data), query_flathub_data_unref);
  future = dex_future_then (
      future, (DexFutureCallback) query_flathub_then,
      query_flathub_data_ref (data), query_flathub_data_unref);
  g_hash_table_replace (
      priv->flathub_prop_queries,
      GINT_TO_POINTER (prop),
      g_steal_pointer (&future));
}

static gint
compare_dates (BzDataPoint *a,
               BzDataPoint *b)
{
  double date_a = bz_data_point_get_independent (a);
  double date_b = bz_data_point_get_independent (b);

  return (date_a > date_b) - (date_a < date_b);
}

static DexFuture *
query_flathub_fiber (QueryFlathubData *data)
{
  int   prop                     = data->prop;
  char *id                       = data->id;
  char *developer                = data->developer;
  g_autoptr (GError) local_error = NULL;
  g_autofree char *request       = NULL;
  g_autoptr (JsonNode) node      = NULL;

  switch (prop)
    {
    case PROP_DOWNLOAD_STATS:
    case PROP_DOWNLOAD_STATS_PER_COUNTRY:
    case PROP_RECENT_DOWNLOADS:
    case PROP_TOTAL_DOWNLOADS:
      request = g_strdup_printf ("/stats/%s?all=false&days=175", id);
      break;
    case PROP_DEVELOPER_APPS:
      request = g_strdup_printf ("/collection/developer/%s", developer);
      break;
    case PROP_FAVORITES_COUNT:
      request = g_strdup_printf ("/favorites/%s/count", id);
      break;
    default:
      g_assert_not_reached ();
      return NULL;
    }

  node = dex_await_boxed (bz_query_flathub_v2_json (request), &local_error);
  if (node == NULL)
    {
      if (!g_error_matches (local_error, DEX_ERROR, DEX_ERROR_FIBER_CANCELLED))
        g_warning ("Could not retrieve property %s for %s from flathub: %s",
                   props[prop]->name, id, local_error->message);
      return dex_future_new_for_error (g_steal_pointer (&local_error));
    }

  switch (prop)
    {
    case PROP_DOWNLOAD_STATS:
      {
        JsonObject *root             = NULL;
        JsonObject *per_day          = NULL;
        g_autoptr (GListStore) store = NULL;

        if (!JSON_NODE_HOLDS_OBJECT (node))
          {
            g_debug ("No data for property %s for %s from flathub",
                     props[prop]->name, id);
            return dex_future_new_for_error (
                g_error_new (G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                             "Unexpected JSON response format"));
          }

        root    = json_node_get_object (node);
        per_day = json_object_get_object_member (root, "installs_per_day");
        store   = g_list_store_new (BZ_TYPE_DATA_POINT);

        json_object_foreach_member (
            per_day,
            (JsonObjectForeach) download_stats_per_day_foreach,
            store);

        g_list_store_sort (store, (GCompareDataFunc) compare_dates, NULL);

        return dex_future_new_for_object (store);
      }
      break;

    case PROP_DOWNLOAD_STATS_PER_COUNTRY:
      {
        JsonObject *per_country      = NULL;
        g_autoptr (GListStore) store = NULL;

        per_country = json_object_get_object_member (
            json_node_get_object (node),
            "installs_per_country");

        store = g_list_store_new (BZ_TYPE_COUNTRY_DATA_POINT);

        json_object_foreach_member (
            per_country,
            (JsonObjectForeach) download_stats_per_country_foreach,
            store);

        return dex_future_new_for_object (store);
      }
      break;

    case PROP_RECENT_DOWNLOADS:
      {
        int recent_downloads = 0;

        if (json_object_has_member (json_node_get_object (node), "installs_last_month"))
          recent_downloads = json_object_get_int_member (json_node_get_object (node), "installs_last_month");

        return dex_future_new_for_int (recent_downloads);
      }
      break;
    case PROP_TOTAL_DOWNLOADS:
      {
        int total_downloads = 0;

        if (json_object_has_member (json_node_get_object (node), "installs_total"))
          total_downloads = json_object_get_int_member (json_node_get_object (node), "installs_total");

        return dex_future_new_for_int (total_downloads);
      }
      break;

    case PROP_DEVELOPER_APPS:
      {
        JsonObject *response_obj          = NULL;
        JsonArray  *apps_array            = NULL;
        g_autoptr (GtkStringList) app_ids = NULL;

        response_obj = json_node_get_object (node);
        apps_array   = json_object_get_array_member (response_obj, "hits");

        app_ids = gtk_string_list_new (NULL);
        for (guint i = 0; i < json_array_get_length (apps_array); i++)
          {
            JsonObject *app_obj = json_array_get_object_element (apps_array, i);
            const char *app_id  = json_object_get_string_member (app_obj, "app_id");

            if (app_id != NULL)
              gtk_string_list_append (app_ids, app_id);
          }

        return dex_future_new_for_object (app_ids);
      }
      break;
    case PROP_FAVORITES_COUNT:
      {
        int favorites_count = 0;
        if (json_object_has_member (json_node_get_object (node), "favorites_count"))
          favorites_count = json_object_get_int_member (json_node_get_object (node), "favorites_count");

        return dex_future_new_for_int (favorites_count);
      }
      break;

    default:
      g_assert_not_reached ();
      return NULL;
    }
}

static DexFuture *
query_flathub_then (DexFuture        *future,
                    QueryFlathubData *data)
{
  g_autoptr (BzEntry) self = NULL;
  int           prop       = data->prop;
  const GValue *value      = NULL;

  self = g_weak_ref_get (&data->self);
  if (self == NULL)
    return NULL;

  value = dex_future_get_value (future, NULL);
  g_object_set_property (G_OBJECT (self), props[prop]->name, value);

  return NULL;
}

static void
download_stats_per_day_foreach (JsonObject  *object,
                                const gchar *member_name,
                                JsonNode    *member_node,
                                GListStore  *store)
{
  double independent               = 0;
  double dependent                 = 0;
  g_autoptr (BzDataPoint) point    = NULL;
  g_autoptr (GDateTime) date       = NULL;
  g_autofree char *formatted_label = NULL;
  g_autofree char *iso_with_tz     = NULL;

  dependent = json_node_get_int (member_node);

  iso_with_tz = g_strdup_printf ("%sT00:00:00Z", member_name);
  date        = g_date_time_new_from_iso8601 (iso_with_tz, NULL);

  formatted_label = g_date_time_format (date, "%-d %b");
  independent     = (double) g_date_time_to_unix (date);

  point = g_object_new (
      BZ_TYPE_DATA_POINT,
      "independent", independent,
      "dependent", dependent,
      "label", formatted_label,
      NULL);
  g_list_store_append (store, point);
}

static void
download_stats_per_country_foreach (JsonObject  *object,
                                    const gchar *member_name,
                                    JsonNode    *member_node,
                                    GListStore  *store)
{
  guint downloads                      = 0;
  g_autoptr (BzCountryDataPoint) point = NULL;

  downloads = json_node_get_int (member_node);

  point = g_object_new (
      BZ_TYPE_COUNTRY_DATA_POINT,
      "country-code", member_name,
      "downloads", downloads,
      NULL);

  g_list_store_append (store, point);
}

static gboolean
maybe_save_paintable (BzEntryPrivate  *priv,
                      const char      *key,
                      GdkPaintable    *paintable,
                      GVariantBuilder *builder)
{
  g_autoptr (GError) local_error = NULL;
  const char *source_uri         = NULL;
  const char *cache_into_path    = NULL;
  g_autoptr (GdkTexture) texture = NULL;
  g_autoptr (GFile) save_file    = NULL;
  gboolean result                = FALSE;

  if (!BZ_IS_ASYNC_TEXTURE (paintable))
    {
      return FALSE;
    }

  source_uri      = bz_async_texture_get_source_uri (BZ_ASYNC_TEXTURE (paintable));
  cache_into_path = bz_async_texture_get_cache_into_path (BZ_ASYNC_TEXTURE (paintable));
  if (cache_into_path == NULL)
    goto done;

  if (bz_async_texture_get_loaded (BZ_ASYNC_TEXTURE (paintable)))
    texture = bz_async_texture_dup_texture (BZ_ASYNC_TEXTURE (paintable));
  else
    goto done;

  save_file = g_file_new_for_path (cache_into_path);
  if (!g_file_query_exists (save_file, NULL))
    {
      g_autoptr (GFile) parent_file        = NULL;
      g_autoptr (GBytes) png_bytes         = NULL;
      g_autoptr (GFileOutputStream) output = NULL;
      gssize bytes_written                 = 0;

      parent_file = g_file_get_parent (save_file);
      result      = g_file_make_directory_with_parents (
          parent_file, NULL, &local_error);
      if (!result)
        {
          if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_EXISTS))
            g_clear_pointer (&local_error, g_error_free);
          else
            {
              g_warning ("Couldn't serialize texture to %s: %s\n",
                         cache_into_path, local_error->message);
              goto done;
            }
        }

      png_bytes = gdk_texture_save_to_png_bytes (texture);
      if (png_bytes == NULL)
        goto done;

      output = g_file_replace (
          save_file,
          NULL,
          FALSE,
          G_FILE_CREATE_REPLACE_DESTINATION,
          NULL,
          &local_error);
      if (output == NULL)
        {
          g_warning ("Couldn't serialize texture to %s: %s\n",
                     cache_into_path, local_error->message);
          goto done;
        }

      bytes_written = g_output_stream_write_bytes (
          G_OUTPUT_STREAM (output), png_bytes, NULL, &local_error);
      if (bytes_written < 0)
        {
          g_warning ("Couldn't serialize texture to %s: %s\n",
                     cache_into_path, local_error->message);
          goto done;
        }

      result = g_output_stream_close (G_OUTPUT_STREAM (output), NULL, &local_error);
      if (!result)
        {
          g_warning ("Couldn't serialize texture to %s: %s\n",
                     cache_into_path, local_error->message);
          goto done;
        }
    }

done:
  g_variant_builder_add (builder, "{sv}", key, g_variant_new ("(sms)", source_uri, cache_into_path));
  return TRUE;
}

static GdkPaintable *
make_async_texture (GVariant *parse)
{
  g_autofree char *source            = NULL;
  g_autofree char *cache_into        = NULL;
  g_autoptr (GFile) source_file      = NULL;
  g_autoptr (GFile) cache_into_file  = NULL;
  g_autoptr (BzAsyncTexture) texture = NULL;

  g_variant_get (parse, "(sms)", &source, &cache_into);
  source_file = g_file_new_for_uri (source);
  if (cache_into != NULL)
    cache_into_file = g_file_new_for_path (cache_into);

  texture = bz_async_texture_new_lazy (source_file, cache_into_file);
  return GDK_PAINTABLE (g_steal_pointer (&texture));
}

static void
clear_entry (BzEntry *self)
{
  BzEntryPrivate *priv = bz_entry_get_instance_private (self);

  g_clear_pointer (&priv->flathub_prop_queries, g_hash_table_unref);
  g_clear_object (&priv->addons);
  g_clear_pointer (&priv->id, g_free);
  g_clear_pointer (&priv->unique_id, g_free);
  g_clear_pointer (&priv->unique_id_checksum, g_free);
  g_clear_pointer (&priv->installed_version, g_free);
  g_clear_pointer (&priv->title, g_free);
  g_clear_pointer (&priv->eol, g_free);
  g_clear_pointer (&priv->description, g_free);
  g_clear_pointer (&priv->long_description, g_free);
  g_clear_pointer (&priv->remote_repo_name, g_free);
  g_clear_pointer (&priv->url, g_free);
  g_clear_object (&priv->icon_paintable);
  g_clear_object (&priv->mini_icon);
  g_clear_object (&priv->remote_repo_icon);
  g_clear_pointer (&priv->search_tokens, g_free);
  g_clear_pointer (&priv->metadata_license, g_free);
  g_clear_pointer (&priv->project_license, g_free);
  g_clear_pointer (&priv->project_group, g_free);
  g_clear_pointer (&priv->developer, g_free);
  g_clear_pointer (&priv->developer_id, g_free);
  g_clear_object (&priv->developer_apps);
  g_clear_object (&priv->screenshot_paintables);
  g_clear_object (&priv->screenshot_captions);
  g_clear_object (&priv->thumbnail_paintable);
  g_clear_object (&priv->share_urls);
  g_clear_pointer (&priv->donation_url, g_free);
  g_clear_pointer (&priv->forge_url, g_free);
  g_clear_pointer (&priv->ratings_summary, g_free);
  g_clear_object (&priv->version_history);
  g_clear_pointer (&priv->light_accent_color, g_free);
  g_clear_pointer (&priv->dark_accent_color, g_free);
  g_clear_object (&priv->verification_status);
  g_clear_object (&priv->download_stats);
  g_clear_object (&priv->download_stats_per_country);
  g_clear_object (&priv->content_rating);
  g_clear_object (&priv->keywords);
  g_clear_object (&priv->permissions);
}
