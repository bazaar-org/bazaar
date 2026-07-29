/* bz-flathub-state.c
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

#define G_LOG_DOMAIN                 "BAZAAR::FLATHUB"
#define COLLECTION_FETCH_SIZE        192
#define CATEGORY_FETCH_SIZE          48
#define QUALITY_MODERATION_PAGE_SIZE 300
#define KEYWORD_SEARCH_PAGE_SIZE     48
#define ADWAITA_URL                  "https://arewelibadwaitayet.com"

#include <json-glib/json-glib.h>
#include <libdex.h>

#include "env.h"
#include "bz-flathub-category.h"
#include "bz-flathub-state.h"
#include "global-net.h"
#include "io.h"
#include "bz-serializable.h"
#include "util.h"

struct _BzFlathubState
{
  GObject parent_instance;

  char                    *for_day;
  BzApplicationMapFactory *map_factory;
  char                    *app_of_the_day;
  GtkStringList           *apps_of_the_week;
  GListStore              *categories;

  DexFuture *initializing;
};

typedef enum
{
  QUALITY_MODE_NONE,
  QUALITY_MODE_FIRST,
  QUALITY_MODE_RANDOM
} QualityMode;

static void
serializable_iface_init (BzSerializableInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (
    BzFlathubState,
    bz_flathub_state,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (BZ_TYPE_SERIALIZABLE, serializable_iface_init))

static GListModel *bz_flathub_state_dup_apps_of_the_day_week (BzFlathubState *self);

enum
{
  PROP_0,

  PROP_FOR_DAY,
  PROP_MAP_FACTORY,
  PROP_APP_OF_THE_DAY,
  PROP_APP_OF_THE_DAY_GROUP,
  PROP_APPS_OF_THE_WEEK,
  PROP_APPS_OF_THE_DAY_WEEK,
  PROP_CATEGORIES,

  LAST_PROP
};
static GParamSpec *props[LAST_PROP] = { 0 };

static DexFuture *
initialize_fiber (GWeakRef *wr);
static DexFuture *
initialize_finally (DexFuture *future,
                    GWeakRef  *wr);

static void
notify_all (BzFlathubState *self);

static void
clear (BzFlathubState *self);

static void
bz_flathub_state_dispose (GObject *object)
{
  BzFlathubState *self = BZ_FLATHUB_STATE (object);

  dex_clear (&self->initializing);
  g_clear_pointer (&self->map_factory, g_object_unref);
  clear (self);

  G_OBJECT_CLASS (bz_flathub_state_parent_class)->dispose (object);
}

static void
bz_flathub_state_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  BzFlathubState *self = BZ_FLATHUB_STATE (object);

  switch (prop_id)
    {
    case PROP_FOR_DAY:
      g_value_set_string (value, bz_flathub_state_get_for_day (self));
      break;
    case PROP_MAP_FACTORY:
      g_value_set_object (value, bz_flathub_state_get_map_factory (self));
      break;
    case PROP_APP_OF_THE_DAY:
      g_value_set_string (value, bz_flathub_state_get_app_of_the_day (self));
      break;
    case PROP_APP_OF_THE_DAY_GROUP:
      g_value_take_object (value, bz_flathub_state_dup_app_of_the_day_group (self));
      break;
    case PROP_APPS_OF_THE_WEEK:
      g_value_take_object (value, bz_flathub_state_dup_apps_of_the_week (self));
      break;
    case PROP_APPS_OF_THE_DAY_WEEK:
      g_value_take_object (value, bz_flathub_state_dup_apps_of_the_day_week (self));
      break;
    case PROP_CATEGORIES:
      g_value_set_object (value, bz_flathub_state_get_categories (self));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_flathub_state_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  BzFlathubState *self = BZ_FLATHUB_STATE (object);

  switch (prop_id)
    {
    case PROP_FOR_DAY:
      dex_future_disown (bz_flathub_state_set_for_day (self, g_value_get_string (value)));
      break;
    case PROP_MAP_FACTORY:
      bz_flathub_state_set_map_factory (self, g_value_get_object (value));
      break;
    case PROP_APP_OF_THE_DAY:
    case PROP_APP_OF_THE_DAY_GROUP:
    case PROP_APPS_OF_THE_WEEK:
    case PROP_APPS_OF_THE_DAY_WEEK:
    case PROP_CATEGORIES:
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_flathub_state_class_init (BzFlathubStateClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = bz_flathub_state_set_property;
  object_class->get_property = bz_flathub_state_get_property;
  object_class->dispose      = bz_flathub_state_dispose;

  props[PROP_FOR_DAY] =
      g_param_spec_string (
          "for-day",
          NULL, NULL, NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_MAP_FACTORY] =
      g_param_spec_object (
          "map-factory",
          NULL, NULL,
          BZ_TYPE_APPLICATION_MAP_FACTORY,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_APP_OF_THE_DAY] =
      g_param_spec_string (
          "app-of-the-day",
          NULL, NULL, NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_APP_OF_THE_DAY_GROUP] =
      g_param_spec_object (
          "app-of-the-day-group",
          NULL, NULL,
          BZ_TYPE_ENTRY_GROUP,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_APPS_OF_THE_WEEK] =
      g_param_spec_object (
          "apps-of-the-week",
          NULL, NULL,
          G_TYPE_LIST_MODEL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_APPS_OF_THE_DAY_WEEK] =
      g_param_spec_object (
          "apps-of-the-day-week",
          NULL, NULL,
          G_TYPE_LIST_MODEL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_CATEGORIES] =
      g_param_spec_object (
          "categories",
          NULL, NULL,
          G_TYPE_LIST_MODEL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, LAST_PROP, props);
}

static void
bz_flathub_state_init (BzFlathubState *self)
{
}

static void
bz_flathub_state_real_serialize (BzSerializable  *serializable,
                                 GVariantBuilder *builder)
{
  BzFlathubState *self = BZ_FLATHUB_STATE (serializable);

  if (self->initializing != NULL &&
      dex_future_is_pending (self->initializing))
    return;

  if (self->for_day != NULL)
    g_variant_builder_add (builder, "{sv}", "for-day", g_variant_new_string (self->for_day));
  if (self->app_of_the_day != NULL)
    g_variant_builder_add (builder, "{sv}", "app-of-the-day", g_variant_new_string (self->app_of_the_day));
  if (self->apps_of_the_week != NULL)
    {
      guint n_items = 0;

      n_items = g_list_model_get_n_items (G_LIST_MODEL (self->apps_of_the_week));
      if (n_items > 0)
        {
          g_autoptr (GVariantBuilder) sub_builder = NULL;

          sub_builder = g_variant_builder_new (G_VARIANT_TYPE ("as"));
          for (guint i = 0; i < n_items; i++)
            {
              const char *string = NULL;

              string = gtk_string_list_get_string (self->apps_of_the_week, i);
              g_variant_builder_add (sub_builder, "s", string);
            }

          g_variant_builder_add (builder, "{sv}", "apps-of-the-week", g_variant_builder_end (sub_builder));
        }
    }
  if (self->categories != NULL)
    {
      guint n_items = 0;

      n_items = g_list_model_get_n_items (G_LIST_MODEL (self->categories));
      if (n_items > 0)
        {
          g_autoptr (GVariantBuilder) sub_builder = NULL;

          sub_builder = g_variant_builder_new (G_VARIANT_TYPE ("av"));
          for (guint i = 0; i < n_items; i++)
            {
              g_autoptr (BzFlathubCategory) category       = NULL;
              g_autoptr (GVariantBuilder) category_builder = NULL;

              category         = g_list_model_get_item (G_LIST_MODEL (self->categories), i);
              category_builder = g_variant_builder_new (G_VARIANT_TYPE_VARDICT);

              bz_serializable_serialize (BZ_SERIALIZABLE (category), category_builder);
              g_variant_builder_add (sub_builder, "v", g_variant_builder_end (category_builder));
            }

          g_variant_builder_add (builder, "{sv}", "categories", g_variant_builder_end (sub_builder));
        }
    }
}

static gboolean
bz_flathub_state_real_deserialize (BzSerializable *serializable,
                                   GVariant       *import,
                                   GError        **error)
{
  BzFlathubState *self          = BZ_FLATHUB_STATE (serializable);
  gboolean        result        = FALSE;
  g_autoptr (GVariantIter) iter = NULL;

  if (self->initializing != NULL &&
      !dex_future_is_pending (self->initializing))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_BUSY,
                   "Cannot perform serialization operations while initializing!");
      return FALSE;
    }

  clear (self);

  iter = g_variant_iter_new (import);
  for (;;)
    {
      g_autofree char *key       = NULL;
      g_autoptr (GVariant) value = NULL;

      /* TODO automate this, this is awful */
      if (!g_variant_iter_next (iter, "{sv}", &key, &value))
        break;

      if (g_strcmp0 (key, "for-day") == 0)
        self->for_day = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (key, "app-of-the-day") == 0)
        self->app_of_the_day = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (key, "apps-of-the-week") == 0)
        {
          g_autoptr (GtkStringList) list     = NULL;
          g_autoptr (GVariantIter) list_iter = NULL;

          list = gtk_string_list_new (NULL);

          list_iter = g_variant_iter_new (value);
          for (;;)
            {
              g_autofree char *id = NULL;

              if (!g_variant_iter_next (list_iter, "s", &id))
                break;
              gtk_string_list_append (list, id);
            }

          self->apps_of_the_week = g_steal_pointer (&list);
        }
      else if (g_strcmp0 (key, "categories") == 0)
        {
          g_autoptr (GListStore) categories        = NULL;
          g_autoptr (GVariantIter) categories_iter = NULL;

          categories = g_list_store_new (BZ_TYPE_FLATHUB_CATEGORY);

          categories_iter = g_variant_iter_new (value);
          for (;;)
            {
              g_autoptr (GVariant) category_import   = NULL;
              g_autoptr (BzFlathubCategory) category = NULL;

              if (!g_variant_iter_next (categories_iter, "v", &category_import))
                break;

              category = bz_flathub_category_new ();
              result   = bz_serializable_deserialize (
                  BZ_SERIALIZABLE (category), category_import, error);
              if (!result)
                return FALSE;

              g_object_bind_property (self, "map-factory", category, "map-factory", G_BINDING_SYNC_CREATE);
              g_list_store_append (categories, category);
            }

          self->categories = g_steal_pointer (&categories);
        }
    }

  notify_all (self);
  return TRUE;
}

static void
serializable_iface_init (BzSerializableInterface *iface)
{
  iface->serialize   = bz_flathub_state_real_serialize;
  iface->deserialize = bz_flathub_state_real_deserialize;
}

BzFlathubState *
bz_flathub_state_new (void)
{
  return g_object_new (BZ_TYPE_FLATHUB_STATE, NULL);
}

const char *
bz_flathub_state_get_for_day (BzFlathubState *self)
{
  g_return_val_if_fail (BZ_IS_FLATHUB_STATE (self), NULL);
  return self->for_day;
}

BzApplicationMapFactory *
bz_flathub_state_get_map_factory (BzFlathubState *self)
{
  g_return_val_if_fail (BZ_IS_FLATHUB_STATE (self), NULL);
  return self->map_factory;
}

const char *
bz_flathub_state_get_app_of_the_day (BzFlathubState *self)
{
  g_return_val_if_fail (BZ_IS_FLATHUB_STATE (self), NULL);
  if (self->initializing != NULL &&
      dex_future_is_pending (self->initializing))
    return NULL;
  return self->app_of_the_day;
}

BzEntryGroup *
bz_flathub_state_dup_app_of_the_day_group (BzFlathubState *self)
{
  g_autoptr (GtkStringObject) string = NULL;

  g_return_val_if_fail (BZ_IS_FLATHUB_STATE (self), NULL);
  if (self->initializing != NULL &&
      dex_future_is_pending (self->initializing))
    return NULL;
  g_return_val_if_fail (self->map_factory != NULL, NULL);

  string = gtk_string_object_new (self->app_of_the_day);
  return bz_application_map_factory_convert_one (self->map_factory, string);
}

GListModel *
bz_flathub_state_dup_apps_of_the_week (BzFlathubState *self)
{
  g_return_val_if_fail (BZ_IS_FLATHUB_STATE (self), NULL);
  if (self->initializing != NULL &&
      dex_future_is_pending (self->initializing))
    return NULL;

  if (self->apps_of_the_week != NULL)
    {
      if (self->map_factory != NULL)
        return bz_application_map_factory_generate (
            self->map_factory, G_LIST_MODEL (self->apps_of_the_week));
      else
        return G_LIST_MODEL (g_object_ref (self->apps_of_the_week));
    }
  else
    return NULL;
}

GListModel *
bz_flathub_state_dup_apps_of_the_day_week (BzFlathubState *self)
{
  g_autoptr (GtkStringList) combined_list = NULL;

  g_return_val_if_fail (BZ_IS_FLATHUB_STATE (self), NULL);
  if (self->initializing != NULL &&
      dex_future_is_pending (self->initializing))
    return NULL;

  combined_list = gtk_string_list_new (NULL);

  if (self->app_of_the_day != NULL)
    gtk_string_list_append (combined_list, self->app_of_the_day);

  if (self->apps_of_the_week != NULL)
    {
      guint n_items = g_list_model_get_n_items (G_LIST_MODEL (self->apps_of_the_week));
      for (guint i = 0; i < n_items; i++)
        {
          const char *app_id = gtk_string_list_get_string (self->apps_of_the_week, i);
          gtk_string_list_append (combined_list, app_id);
        }
    }

  if (self->map_factory != NULL)
    return bz_application_map_factory_generate (self->map_factory, G_LIST_MODEL (combined_list));
  else
    return G_LIST_MODEL (g_object_ref (combined_list));
}

GListModel *
bz_flathub_state_get_categories (BzFlathubState *self)
{
  g_return_val_if_fail (BZ_IS_FLATHUB_STATE (self), NULL);
  if (self->initializing != NULL &&
      dex_future_is_pending (self->initializing))
    return NULL;
  return G_LIST_MODEL (self->categories);
}

DexFuture *
bz_flathub_state_set_for_day (BzFlathubState *self,
                              const char     *for_day)
{
  dex_return_error_if_fail (BZ_IS_FLATHUB_STATE (self));

  dex_clear (&self->initializing);
  clear (self);
  if (for_day != NULL)
    {
      g_autoptr (DexFuture) future = NULL;

      self->for_day          = g_strdup (for_day);
      self->apps_of_the_week = gtk_string_list_new (NULL);
      self->categories       = g_list_store_new (BZ_TYPE_FLATHUB_CATEGORY);

      future = dex_scheduler_spawn (
          bz_get_io_scheduler (),
          bz_get_dex_stack_size (),
          (DexFiberFunc) initialize_fiber,
          bz_track_weak (self), bz_weak_release);
      future = dex_future_finally (
          future,
          (DexFutureCallback) initialize_finally,
          bz_track_weak (self), bz_weak_release);
      self->initializing = g_steal_pointer (&future);
      return dex_ref (self->initializing);
    }
  else
    {
      notify_all (self);
      return dex_future_new_false ();
    }
}

DexFuture *
bz_flathub_state_update_to_today (BzFlathubState *self)
{
  g_autoptr (GDateTime) datetime = NULL;
  g_autofree gchar *for_day      = NULL;

  dex_return_error_if_fail (BZ_IS_FLATHUB_STATE (self));

  datetime = g_date_time_new_now_utc ();
  for_day  = g_date_time_format (datetime, "%F");

  g_debug ("Syncing with flathub for day: %s", for_day);
  return bz_flathub_state_set_for_day (self, for_day);
}

void
bz_flathub_state_set_map_factory (BzFlathubState          *self,
                                  BzApplicationMapFactory *map_factory)
{
  g_return_if_fail (BZ_IS_FLATHUB_STATE (self));
  g_return_if_fail (map_factory == NULL || BZ_IS_APPLICATION_MAP_FACTORY (map_factory));

  g_clear_object (&self->map_factory);
  if (map_factory != NULL)
    self->map_factory = g_object_ref (map_factory);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_MAP_FACTORY]);
}

static gboolean
is_kde_plasma (void)
{
  const char *desktop = g_getenv ("XDG_CURRENT_DESKTOP");

  if (desktop == NULL)
    return FALSE;

  return g_str_equal (desktop, "KDE") || g_strstr_len (desktop, -1, "KDE") != NULL;
}

static void
add_category (BzFlathubState *self,
              const char     *name,
              JsonNode       *node,
              GHashTable     *quality_set,
              gboolean        is_json_object,
              QualityMode     quality_mode,
              gboolean        is_spotlight)
{
  JsonObject    *object                   = NULL;
  JsonObjectIter iter                     = { 0 };
  JsonArray     *hits_array               = NULL;
  const char    *key                      = NULL;
  const char    *app                      = NULL;
  g_autoptr (BzFlathubCategory) category  = NULL;
  g_autoptr (GtkStringList) store         = NULL;
  g_autoptr (GtkStringList) quality_store = NULL;
  g_autoptr (GPtrArray) quality_apps      = NULL;
  guint app_count                         = 0;
  guint quality_count                     = 0;
  guint random_index                      = 0;
  guint i                                 = 0;
  int   total_entries                     = 0;

  category      = bz_flathub_category_new ();
  store         = gtk_string_list_new (NULL);
  quality_store = gtk_string_list_new (NULL);

  bz_flathub_category_set_name (category, name);
  bz_flathub_category_set_is_spotlight (category, is_spotlight);
  bz_flathub_category_set_applications (category, G_LIST_MODEL (store));

  object = json_node_get_object (node);

  if (is_json_object)
    {
      if (quality_mode == QUALITY_MODE_RANDOM)
        quality_apps = g_ptr_array_new_with_free_func (g_free);

      json_object_iter_init (&iter, object);
      while (json_object_iter_next (&iter, &key, NULL))
        {
          gtk_string_list_append (store, key);

          if (g_hash_table_contains (quality_set, key))
            {
              if (quality_mode == QUALITY_MODE_RANDOM)
                g_ptr_array_add (quality_apps, g_strdup (key));
              else if (quality_mode == QUALITY_MODE_FIRST)
                gtk_string_list_append (quality_store, key);
            }
          app_count++;
        }
      total_entries = json_object_get_size (object);
    }
  else
    {
      if (quality_mode == QUALITY_MODE_RANDOM)
        quality_apps = g_ptr_array_new_with_free_func (g_free);

      hits_array = json_object_get_array_member (object, "hits");
      app_count  = json_array_get_length (hits_array);

      for (i = 0; i < app_count; i++)
        {
          JsonObject *element = NULL;
          const char *app_id  = NULL;

          element = json_array_get_object_element (hits_array, i);
          app_id  = json_object_get_string_member (element, "app_id");
          gtk_string_list_append (store, app_id);

          if (g_hash_table_contains (quality_set, app_id))
            {
              if (quality_mode == QUALITY_MODE_RANDOM)
                g_ptr_array_add (quality_apps, g_strdup (app_id));
              else if (quality_mode == QUALITY_MODE_FIRST)
                gtk_string_list_append (quality_store, app_id);
            }
        }
      total_entries = json_object_get_int_member (object, "totalHits");
    }

  if (quality_mode == QUALITY_MODE_RANDOM && quality_apps != NULL)
    {
      quality_count = MIN (7, quality_apps->len);
      for (i = 0; i < quality_count; i++)
        {
          random_index = g_random_int_range (0, quality_apps->len);
          app          = g_ptr_array_index (quality_apps, random_index);
          gtk_string_list_append (quality_store, app);
          g_ptr_array_remove_index_fast (quality_apps, random_index);
        }
    }

  bz_flathub_category_set_total_entries (category, total_entries);
  bz_flathub_category_set_quality_applications (category, G_LIST_MODEL (quality_store));
  g_list_store_append (self->categories, category);
}

static DexFuture *
initialize_fiber (GWeakRef *wr)
{
  g_autoptr (BzFlathubState) self    = NULL;
  g_autoptr (GError) local_error     = NULL;
  gboolean result                    = FALSE;
  gboolean is_kde                    = is_kde_plasma ();
  g_autoptr (GHashTable) quality_set = NULL;

  g_autoptr (DexFuture) aotd_f       = NULL;
  g_autoptr (DexFuture) aotw_f       = NULL;
  g_autoptr (DexFuture) categories_f = NULL;
  g_autoptr (DexFuture) updated_f    = NULL;
  g_autoptr (DexFuture) added_f      = NULL;
  g_autoptr (DexFuture) popular_f    = NULL;
  g_autoptr (DexFuture) trending_f   = NULL;
  g_autoptr (DexFuture) mobile_f     = NULL;
  g_autoptr (DexFuture) passing_f    = NULL;
  g_autoptr (DexFuture) adwaita_f    = NULL;
  g_autoptr (DexFuture) toolkit_f    = NULL;
  g_autoptr (DexFuture) emulators_f  = NULL;
  g_autoptr (DexFuture) launchers_f  = NULL;
  g_autoptr (DexFuture) game_tools_f = NULL;
  g_autoptr (DexFuture) game_only_f  = NULL;

  bz_weak_get_or_return_reject (self, wr);

  quality_set = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

#define ADD_REQUEST(_var, ...)                                                         \
  G_STMT_START                                                                         \
  {                                                                                    \
    g_autofree char *_request = NULL;                                                  \
                                                                                       \
    _request = g_strdup_printf (__VA_ARGS__);                                          \
    (_var)   = bz_query_flathub_v2_json_take (g_steal_pointer (&_request));            \
    if (!dex_await (dex_ref ((_var)), &local_error))                                   \
      {                                                                                \
        g_warning ("Failed to complete request to flathub: %s", local_error->message); \
        return dex_future_new_for_error (g_steal_pointer (&local_error));              \
      }                                                                                \
  }                                                                                    \
  G_STMT_END

  if (is_kde)
    ADD_REQUEST (toolkit_f, "/collection/developer/kde?locale=en");
  else
    {
      adwaita_f = bz_https_query_json (ADWAITA_URL "/api/apps");
      if (!dex_await (dex_ref (adwaita_f), &local_error))
        {
          g_warning ("Failed to complete request to arewelibadwaitayet: %s", local_error->message);
          g_clear_error (&local_error);
          adwaita_f = NULL;
        }
    }

  ADD_REQUEST (passing_f, "/quality-moderation/passing-apps?page=1&page_size=%d", QUALITY_MODERATION_PAGE_SIZE);
  ADD_REQUEST (aotd_f, "/app-picks/app-of-the-day/%s", self->for_day);
  ADD_REQUEST (aotw_f, "/app-picks/apps-of-the-week/%s", self->for_day);
  ADD_REQUEST (categories_f, "/collection/category");
  ADD_REQUEST (updated_f, "/collection/recently-updated?page=0&per_page=%d", COLLECTION_FETCH_SIZE);
  ADD_REQUEST (added_f, "/collection/recently-added?page=0&per_page=%d", COLLECTION_FETCH_SIZE);
  ADD_REQUEST (popular_f, "/collection/popular?page=0&per_page=%d", COLLECTION_FETCH_SIZE);
  ADD_REQUEST (trending_f, "/collection/trending?page=0&per_page=%d", COLLECTION_FETCH_SIZE);
  ADD_REQUEST (mobile_f, "/collection/mobile?page=0&per_page=%d", CATEGORY_FETCH_SIZE);
  ADD_REQUEST (game_only_f, "/collection/category/game?exclude_subcategories=emulator&exclude_subcategories=packageManager&exclude_subcategories=utility&exclude_subcategories=network&exclude_subcategories=gameTool&exclude_subcategories=launcherStore&sort_by=trending&page=0&per_page=%d", CATEGORY_FETCH_SIZE);
  ADD_REQUEST (emulators_f, "/collection/category/game/subcategories?subcategory=emulator&sort_by=trending&page=0&per_page=%d", CATEGORY_FETCH_SIZE);
  ADD_REQUEST (launchers_f, "/collection/category/game/subcategories?subcategory=packageManager&subcategory=launcherStore&sort_by=trending&page=0&per_page=%d", CATEGORY_FETCH_SIZE);
  ADD_REQUEST (game_tools_f, "/collection/category/game/subcategories?subcategory=utility&subcategory=network&subcategory=gameTool&sort_by=trending&page=0&per_page=%d", CATEGORY_FETCH_SIZE);

#undef ADD_REQUEST

#define GET_BOXED(_future) g_value_get_boxed (dex_future_get_value ((_future), NULL))

  {
    JsonObject *object = NULL;
    JsonArray  *array  = NULL;
    guint       length = 0;

    object = json_node_get_object (GET_BOXED (passing_f));
    array  = json_object_get_array_member (object, "apps");
    length = json_array_get_length (array);

    for (guint i = 0; i < length; i++)
      {
        const char *app_id = NULL;

        app_id = json_array_get_string_element (array, i);
        g_hash_table_replace (quality_set, g_strdup (app_id), NULL);
      }
  }
  {
    JsonObject *object = NULL;

    object               = json_node_get_object (GET_BOXED (aotd_f));
    self->app_of_the_day = g_strdup (json_object_get_string_member (object, "app_id"));
  }
  {
    JsonObject *object = NULL;
    JsonArray  *array  = NULL;
    guint       length = 0;

    object = json_node_get_object (GET_BOXED (aotw_f));
    array  = json_object_get_array_member (object, "apps");
    length = json_array_get_length (array);

    for (guint i = 0; i < length; i++)
      {
        JsonObject *element = NULL;

        element = json_array_get_object_element (array, i);
        gtk_string_list_append (
            self->apps_of_the_week,
            json_object_get_string_member (element, "app_id"));
      }
  }

  add_category (self, "trending", GET_BOXED (trending_f), quality_set, FALSE, QUALITY_MODE_NONE, TRUE);
  add_category (self, "popular", GET_BOXED (popular_f), quality_set, FALSE, QUALITY_MODE_NONE, TRUE);
  add_category (self, "recently-added", GET_BOXED (added_f), quality_set, FALSE, QUALITY_MODE_NONE, TRUE);
  add_category (self, "recently-updated", GET_BOXED (updated_f), quality_set, FALSE, QUALITY_MODE_NONE, TRUE);
  add_category (self, "mobile", GET_BOXED (mobile_f), quality_set, FALSE, QUALITY_MODE_NONE, TRUE);
  add_category (self, "game-only", GET_BOXED (game_only_f), quality_set, FALSE, QUALITY_MODE_NONE, FALSE);
  add_category (self, "emulators", GET_BOXED (emulators_f), quality_set, FALSE, QUALITY_MODE_NONE, FALSE);
  add_category (self, "launchers", GET_BOXED (launchers_f), quality_set, FALSE, QUALITY_MODE_NONE, FALSE);
  add_category (self, "game-tools", GET_BOXED (game_tools_f), quality_set, FALSE, QUALITY_MODE_NONE, FALSE);

  {
    static const char *categories[] = {
      "audiovideo", "development", "education", "game", "graphics",
      "network", "office", "science", "system", "utility"
    };
    g_autoptr (GPtrArray) category_futures = NULL;

    category_futures = g_ptr_array_new_with_free_func (dex_unref);

    for (guint i = 0; i < G_N_ELEMENTS (categories); i++)
      {
        g_autofree char *request     = NULL;
        g_autoptr (DexFuture) future = NULL;

        request = g_strdup_printf (
            "/collection/category/%s?page=0&per_page=%d",
            categories[i], CATEGORY_FETCH_SIZE);

        future = bz_query_flathub_v2_json_take (g_steal_pointer (&request));
        result = dex_await (dex_ref (future), &local_error);
        if (!result)
          {
            g_warning ("Failed to complete request to flathub: %s", local_error->message);
            return dex_future_new_for_error (g_steal_pointer (&local_error));
          }
        g_ptr_array_add (category_futures, dex_ref (future));
      }

    for (guint i = 0; i < G_N_ELEMENTS (categories); i++)
      {
        DexFuture *future = NULL;
        JsonNode  *node   = NULL;

        future = g_ptr_array_index (category_futures, i);
        node   = GET_BOXED (future);

        add_category (self, categories[i], node, quality_set, FALSE, QUALITY_MODE_FIRST, FALSE);
      }
  }

  if (is_kde)
    add_category (self, "kde", GET_BOXED (toolkit_f), quality_set, FALSE, QUALITY_MODE_RANDOM, FALSE);
  else if (adwaita_f != NULL)
    add_category (self, "adwaita", GET_BOXED (adwaita_f), quality_set, TRUE, QUALITY_MODE_RANDOM, FALSE);

  return dex_future_new_true ();
}

static DexFuture *
initialize_finally (DexFuture *future,
                    GWeakRef  *wr)
{
  g_autoptr (BzFlathubState) self = NULL;

  bz_weak_get_or_return_reject (self, wr);

  if (dex_future_is_resolved (future))
    {
      guint n_categories = 0;

      n_categories = g_list_model_get_n_items (G_LIST_MODEL (self->categories));
      for (guint i = 0; i < n_categories; i++)
        {
          g_autoptr (BzFlathubCategory) category = NULL;

          category = g_list_model_get_item (G_LIST_MODEL (self->categories), i);
          g_object_bind_property (self, "map-factory", category, "map-factory", G_BINDING_SYNC_CREATE);
        }

      g_debug ("Done syncing flathub state; notifying property listeners...");
      notify_all (self);
    }
  else
    clear (self);

  return dex_ref (future);
}

static DexFuture *
search_collection_fiber (char *route)
{
  g_autoptr (GError) local_error    = NULL;
  g_autoptr (GtkStringList) results = NULL;
  g_autoptr (JsonNode) node         = NULL;
  g_autofree char *request          = NULL;
  JsonObject      *object           = NULL;
  JsonArray       *array            = NULL;
  guint            length           = 0;

  request = g_strdup_printf ("%s&page=1&per_page=%d&locale=en",
                             route, KEYWORD_SEARCH_PAGE_SIZE);

  node = dex_await_boxed (
      bz_query_flathub_v2_json_take (
          g_steal_pointer (&request)),
      &local_error);
  if (node == NULL)
    return dex_future_new_for_error (g_steal_pointer (&local_error));

  results = gtk_string_list_new (NULL);

  object = json_node_get_object (node);
  array  = json_object_get_array_member (object, "hits");
  length = json_array_get_length (array);

  for (guint i = 0; i < length; i++)
    {
      JsonObject *element = NULL;
      const char *app_id  = NULL;

      element = json_array_get_object_element (array, i);
      app_id  = json_object_get_string_member (element, "app_id");
      gtk_string_list_append (results, app_id);
    }
  return dex_future_new_take_object (g_steal_pointer (&results));
}

static DexFuture *
search_collection_finally (DexFuture *future,
                           GWeakRef  *wr)
{
  g_autoptr (BzFlathubState) self = NULL;
  const GValue *value             = NULL;
  GListModel   *model             = NULL;

  bz_weak_get_or_return_reject (self, wr);

  value = dex_future_get_value (future, NULL);
  if (value == NULL)
    return dex_ref (future);

  model = g_value_get_object (value);

  if (self->map_factory != NULL)
    return dex_future_new_take_object (
        bz_application_map_factory_generate (self->map_factory, model));

  return dex_ref (future);
}

DexFuture *
bz_flathub_state_search_collection (BzFlathubState *self,
                                    const char     *route)
{
  g_autoptr (DexFuture) future = NULL;

  dex_return_error_if_fail (BZ_IS_FLATHUB_STATE (self));
  dex_return_error_if_fail (route != NULL);

  future = dex_scheduler_spawn (
      bz_get_io_scheduler (),
      bz_get_dex_stack_size (),
      (DexFiberFunc) search_collection_fiber,
      g_strdup (route),
      g_free);
  future = dex_future_finally (
      future,
      (DexFutureCallback) search_collection_finally,
      bz_track_weak (self),
      bz_weak_release);
  return g_steal_pointer (&future);
}

static void
notify_all (BzFlathubState *self)
{
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_FOR_DAY]);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_APP_OF_THE_DAY]);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_APP_OF_THE_DAY_GROUP]);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_APPS_OF_THE_WEEK]);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_APPS_OF_THE_DAY_WEEK]);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_CATEGORIES]);
}

static void
clear (BzFlathubState *self)
{
  g_clear_pointer (&self->for_day, g_free);
  g_clear_pointer (&self->app_of_the_day, g_free);
  g_clear_pointer (&self->apps_of_the_week, g_object_unref);
  g_clear_pointer (&self->categories, g_object_unref);
}

/* End of bz-flathub-state.c */
