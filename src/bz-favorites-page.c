/* bz-favorites-page.c
 *
 * Copyright 2025 Adam Masciola, Alexander Vanhee
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

#include "config.h"

#include <glib/gi18n.h>
#include <json-glib/json-glib.h>

#include "bz-application-map-factory.h"
#include "env.h"
#include "error.h"
#include "bz-favorites-page.h"
#include "bz-favorites-tile.h"
#include "global-net.h"
#include "io.h"
#include "util.h"

struct _BzFavoritesPage
{
  AdwNavigationPage parent_instance;

  BzStateInfo *state;
  GListModel  *model;
  GListModel  *favorites;
  gboolean     show_sidebar;

  AdwViewStack *stack;
};

G_DEFINE_FINAL_TYPE (BzFavoritesPage, bz_favorites_page, ADW_TYPE_NAVIGATION_PAGE)

enum
{
  PROP_0,

  PROP_STATE,
  PROP_MODEL,
  PROP_FAVORITES,
  PROP_SHOW_SIDEBAR,

  LAST_PROP
};
static GParamSpec *props[LAST_PROP] = { 0 };

static DexFuture *
fetch_favorites_fiber (GWeakRef *wr);

static void
favorites_changed (BzFavoritesPage *self,
                   guint            position,
                   guint            removed,
                   guint            added,
                   GListModel      *model);

static void
set_page (BzFavoritesPage *self);

static void
bz_favorites_page_dispose (GObject *object)
{
  BzFavoritesPage *self = BZ_FAVORITES_PAGE (object);

  if (self->favorites != NULL)
    g_signal_handlers_disconnect_by_func (self->favorites, favorites_changed, self);
  g_clear_object (&self->favorites);
  g_clear_object (&self->model);
  g_clear_object (&self->state);

  G_OBJECT_CLASS (bz_favorites_page_parent_class)->dispose (object);
}

static void
bz_favorites_page_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  BzFavoritesPage *self = BZ_FAVORITES_PAGE (object);

  switch (prop_id)
    {
    case PROP_STATE:
      g_value_set_object (value, self->state);
      break;
    case PROP_MODEL:
      g_value_set_object (value, self->model);
      break;
    case PROP_FAVORITES:
      g_value_set_object (value, self->favorites);
      break;
    case PROP_SHOW_SIDEBAR:
      g_value_set_boolean (value, self->show_sidebar);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_favorites_page_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  BzFavoritesPage *self = BZ_FAVORITES_PAGE (object);

  switch (prop_id)
    {
    case PROP_STATE:
      self->state = g_value_dup_object (value);
      break;
    case PROP_SHOW_SIDEBAR:
      self->show_sidebar = g_value_get_boolean (value);
      g_object_notify_by_pspec (G_OBJECT (self), props[PROP_SHOW_SIDEBAR]);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_favorites_page_constructed (GObject *object)
{
  BzFavoritesPage *self        = BZ_FAVORITES_PAGE (object);
  g_autoptr (DexFuture) future = NULL;

  G_OBJECT_CLASS (bz_favorites_page_parent_class)->constructed (object);

  future = dex_scheduler_spawn (
      dex_scheduler_get_default (),
      bz_get_dex_stack_size (),
      (DexFiberFunc) fetch_favorites_fiber,
      bz_track_weak (self),
      bz_weak_release);
  dex_future_disown (g_steal_pointer (&future));
}

static gboolean
is_zero (gpointer object,
         int      value)
{
  return value == 0;
}

static gboolean
invert_boolean (gpointer object,
                gboolean value)
{
  return !value;
}

static gboolean
is_empty (gpointer    object,
          GListModel *model)
{
  if (model == NULL)
    return TRUE;
  return g_list_model_get_n_items (model) == 0;
}

static gboolean
is_favorited (GListModel   *favorites,
              BzEntryGroup *group)
{
  if (favorites == NULL || group == NULL)
    return FALSE;

  return g_list_store_find (G_LIST_STORE (favorites), group, NULL);
}

static void
tile_activated_cb (BzFavoritesTile *tile)
{
  BzEntryGroup *group = NULL;

  g_assert (BZ_IS_FAVORITES_TILE (tile));

  group = bz_favorites_tile_get_group (tile);
  if (group == NULL)
    return;

  gtk_widget_activate_action (GTK_WIDGET (tile), "window.show-group", "s",
                              bz_entry_group_get_id (group));
}

static void
unfavorite_cb (BzFavoritesTile *tile,
               BzEntryGroup    *group,
               gpointer         user_data)
{
  BzFavoritesPage *self     = NULL;
  guint            position = 0;

  g_assert (BZ_IS_FAVORITES_TILE (tile));
  g_assert (BZ_IS_ENTRY_GROUP (group));

  self = BZ_FAVORITES_PAGE (gtk_widget_get_ancestor (GTK_WIDGET (tile), BZ_TYPE_FAVORITES_PAGE));
  if (self == NULL)
    return;

  if (self->favorites == NULL)
    return;

  if (g_list_store_find (G_LIST_STORE (self->favorites), group, &position))
    {
      g_list_store_remove (G_LIST_STORE (self->favorites), position);
      g_object_notify_by_pspec (G_OBJECT (self), props[PROP_FAVORITES]);
    }
}

static void
install_all_cb (BzFavoritesPage *self,
                GtkButton       *button)
{
  GVariantBuilder builder = G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE ("as"));
  guint n_items           = 0;

  g_return_if_fail (BZ_IS_FAVORITES_PAGE (self));

  if (self->model == NULL || self->favorites == NULL)
    return;

  n_items = g_list_model_get_n_items (self->favorites);

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr (BzEntryGroup) group = NULL;

      group = g_list_model_get_item (self->favorites, i);

      if (!is_favorited (self->favorites, group))
        continue;

      g_variant_builder_add (&builder, "s", bz_entry_group_get_id (group));
    }

  gtk_widget_activate_action (GTK_WIDGET (self), "window.bulk-install", "as",
                              &builder);
}

static void
bz_favorites_page_class_init (BzFavoritesPageClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose      = bz_favorites_page_dispose;
  object_class->constructed  = bz_favorites_page_constructed;
  object_class->get_property = bz_favorites_page_get_property;
  object_class->set_property = bz_favorites_page_set_property;

  props[PROP_STATE] =
      g_param_spec_object (
          "state",
          NULL, NULL,
          BZ_TYPE_STATE_INFO,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  props[PROP_MODEL] =
      g_param_spec_object (
          "model",
          NULL, NULL,
          G_TYPE_LIST_MODEL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  props[PROP_FAVORITES] =
      g_param_spec_object (
          "favorites",
          NULL, NULL,
          G_TYPE_LIST_MODEL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  props[PROP_SHOW_SIDEBAR] =
      g_param_spec_boolean (
          "show-sidebar",
          NULL, NULL,
          FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  g_type_ensure (BZ_TYPE_FAVORITES_TILE);

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/kolunmi/Bazaar/bz-favorites-page.ui");
  gtk_widget_class_bind_template_child (widget_class, BzFavoritesPage, stack);
  gtk_widget_class_bind_template_callback (widget_class, is_zero);
  gtk_widget_class_bind_template_callback (widget_class, invert_boolean);
  gtk_widget_class_bind_template_callback (widget_class, is_empty);
  gtk_widget_class_bind_template_callback (widget_class, tile_activated_cb);
  gtk_widget_class_bind_template_callback (widget_class, unfavorite_cb);
  gtk_widget_class_bind_template_callback (widget_class, install_all_cb);
}

static void
bz_favorites_page_init (BzFavoritesPage *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

GtkWidget *
bz_favorites_page_new (BzStateInfo *state)
{
  return g_object_new (BZ_TYPE_FAVORITES_PAGE,
                       "state", state,
                       NULL);
}

static void
favorites_changed (BzFavoritesPage *self,
                   guint            position,
                   guint            removed,
                   guint            added,
                   GListModel      *model)
{
  set_page (self);
}

static void
set_page (BzFavoritesPage *self)
{
  if (self->favorites != NULL &&
      g_list_model_get_n_items (self->favorites) > 0)
    adw_view_stack_set_visible_child_name (self->stack, "content");
  else
    adw_view_stack_set_visible_child_name (self->stack, "empty");
}

static int
compare_entry_groups_by_title (BzEntryGroup *group_a,
                               BzEntryGroup *group_b)
{
  const char *title_a = bz_entry_group_get_title (group_a);
  const char *title_b = bz_entry_group_get_title (group_b);
  return g_utf8_collate (title_a, title_b);
}

static DexFuture *
fetch_favorites_fiber (GWeakRef *wr)
{
  g_autoptr (BzFavoritesPage) self       = NULL;
  g_autoptr (GError) local_error         = NULL;
  g_autoptr (GtkStringList) id_list      = NULL;
  g_autoptr (GListModel) model           = NULL;
  g_autoptr (GListStore) sorted_store    = NULL;
  g_autoptr (GListStore) favorites_store = NULL;
  g_autoptr (JsonNode) node              = NULL;
  BzApplicationMapFactory *factory       = NULL;
  BzAuthState             *auth_state    = NULL;
  const char              *token         = NULL;
  JsonArray               *array         = NULL;
  guint                    n_favorites   = 0;
  guint                    n_items       = 0;

  self = g_weak_ref_get (wr);
  if (self == NULL)
    return dex_future_new_reject (G_IO_ERROR, G_IO_ERROR_CANCELLED, "Page destroyed");

  id_list = gtk_string_list_new (NULL);

  auth_state = bz_state_info_get_auth_state (self->state);
  token      = bz_auth_state_get_token (auth_state);

  if (token != NULL && bz_auth_state_is_authenticated (auth_state))
    {
      node = dex_await_boxed (
          bz_query_flathub_v2_json_authenticated ("/favorites", token),
          &local_error);

      if (node == NULL)
        {
          g_warning ("Failed to fetch favorites from Flathub: %s", local_error->message);
          goto done;
        }

      if (!JSON_NODE_HOLDS_ARRAY (node))
        {
          g_warning ("Unexpected response format from Flathub favorites API");
          goto done;
        }

      array       = json_node_get_array (node);
      n_favorites = json_array_get_length (array);

      for (guint i = 0; i < n_favorites; i++)
        {
          JsonObject *favorite_obj = NULL;
          const char *app_id       = NULL;

          favorite_obj = json_array_get_object_element (array, i);
          app_id       = json_object_get_string_member (favorite_obj, "app_id");

          if (app_id != NULL)
            gtk_string_list_append (id_list, app_id);
        }
    }

done:
  factory = bz_state_info_get_application_factory (self->state);
  model   = bz_application_map_factory_generate (factory, G_LIST_MODEL (id_list));

  sorted_store    = g_list_store_new (BZ_TYPE_ENTRY_GROUP);
  favorites_store = g_list_store_new (BZ_TYPE_ENTRY_GROUP);
  n_items         = g_list_model_get_n_items (model);
  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr (BzEntryGroup) group = g_list_model_get_item (model, i);
      g_list_store_append (sorted_store, group);
      g_list_store_append (favorites_store, group);
    }
  g_list_store_sort (sorted_store, (GCompareDataFunc) compare_entry_groups_by_title, NULL);

  g_clear_object (&self->model);
  self->model = G_LIST_MODEL (g_steal_pointer (&sorted_store));

  if (self->favorites != NULL)
    g_signal_handlers_disconnect_by_func (self->favorites, favorites_changed, self);

  g_clear_object (&self->favorites);
  self->favorites = G_LIST_MODEL (g_steal_pointer (&favorites_store));

  g_signal_connect_swapped (self->favorites, "items-changed", G_CALLBACK (favorites_changed), self);
  set_page (self);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_MODEL]);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_FAVORITES]);

  return dex_future_new_true ();
}
