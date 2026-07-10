/* bz-user-data-page.c
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

#include "bz-application-map-factory.h"
#include "env.h"
#include "io.h"
#include "bz-user-data-page.h"
#include "bz-user-data-tile.h"
#include "util.h"

struct _BzUserDataPage
{
  AdwNavigationPage parent_instance;

  BzStateInfo *state;
  GListModel  *model;

  /* Template widgets */
  AdwViewStack *stack;
};

G_DEFINE_FINAL_TYPE (BzUserDataPage, bz_user_data_page, ADW_TYPE_NAVIGATION_PAGE)

enum
{
  PROP_0,

  PROP_STATE,
  PROP_MODEL,

  LAST_PROP
};
static GParamSpec *props[LAST_PROP] = { 0 };

static DexFuture *
fetch_user_data_fiber (GWeakRef *wr);

static void
items_changed (BzUserDataPage *self,
               guint           position,
               guint           removed,
               guint           added,
               GListModel     *model);

static void
set_page (BzUserDataPage *self);

static void
bz_user_data_page_dispose (GObject *object)
{
  BzUserDataPage *self = BZ_USER_DATA_PAGE (object);

  if (self->model != NULL)
    g_signal_handlers_disconnect_by_func (self->model, items_changed, self);
  g_clear_object (&self->model);
  g_clear_object (&self->state);

  G_OBJECT_CLASS (bz_user_data_page_parent_class)->dispose (object);
}

static void
bz_user_data_page_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  BzUserDataPage *self = BZ_USER_DATA_PAGE (object);

  switch (prop_id)
    {
    case PROP_STATE:
      g_value_set_object (value, self->state);
      break;
    case PROP_MODEL:
      g_value_set_object (value, self->model);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_user_data_page_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  BzUserDataPage *self = BZ_USER_DATA_PAGE (object);

  switch (prop_id)
    {
    case PROP_STATE:
      self->state = g_value_dup_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_user_data_page_constructed (GObject *object)
{
  BzUserDataPage *self         = BZ_USER_DATA_PAGE (object);
  g_autoptr (DexFuture) future = NULL;

  G_OBJECT_CLASS (bz_user_data_page_parent_class)->constructed (object);

  future = dex_scheduler_spawn (
      dex_scheduler_get_default (),
      bz_get_dex_stack_size (),
      (DexFiberFunc) fetch_user_data_fiber,
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

static void
bz_user_data_page_class_init (BzUserDataPageClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose      = bz_user_data_page_dispose;
  object_class->constructed  = bz_user_data_page_constructed;
  object_class->get_property = bz_user_data_page_get_property;
  object_class->set_property = bz_user_data_page_set_property;

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

  g_object_class_install_properties (object_class, LAST_PROP, props);

  g_type_ensure (BZ_TYPE_USER_DATA_TILE);

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/kolunmi/Bazaar/bz-user-data-page.ui");
  gtk_widget_class_bind_template_child (widget_class, BzUserDataPage, stack);
  gtk_widget_class_bind_template_callback (widget_class, is_zero);
}

static void
bz_user_data_page_init (BzUserDataPage *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

GtkWidget *
bz_user_data_page_new (BzStateInfo *state)
{
  return g_object_new (BZ_TYPE_USER_DATA_PAGE,
                       "state", state,
                       NULL);
}

static void
items_changed (BzUserDataPage *self,
               guint           position,
               guint           removed,
               guint           added,
               GListModel     *model)
{
  set_page (self);
}

static void
set_page (BzUserDataPage *self)
{
  if (self->model != NULL &&
      g_list_model_get_n_items (G_LIST_MODEL (self->model)) > 0)
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
fetch_user_data_fiber (GWeakRef *wr)
{
  g_autoptr (BzUserDataPage) self      = NULL;
  g_autoptr (GHashTable) ids_hash      = NULL;
  g_autoptr (GError) local_error       = NULL;
  GHashTableIter iter                  = { 0 };
  g_autoptr (GtkStringList) id_list    = NULL;
  BzApplicationMapFactory *factory     = NULL;
  g_autoptr (GListStore) sorted_store  = NULL;
  GListModel *installed_groups         = NULL;
  g_autoptr (GHashTable) installed_ids = NULL;
  const char *own_id                  = NULL;
  guint n_items                        = 0;

  self = g_weak_ref_get (wr);
  if (self == NULL)
    return dex_future_new_reject (G_IO_ERROR, G_IO_ERROR_CANCELLED, "Page destroyed");

  own_id = g_application_get_application_id (g_application_get_default ());

  installed_groups = bz_state_info_get_all_installed_entry_groups (self->state);
  installed_ids    = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  if (installed_groups != NULL)
    {
      guint n_installed = g_list_model_get_n_items (installed_groups);
      for (guint i = 0; i < n_installed; i++)
        {
          g_autoptr (BzEntryGroup) group = NULL;
          const char *id                 = NULL;

          group = g_list_model_get_item (installed_groups, i);
          id    = bz_entry_group_get_id (group);
          if (id != NULL)
            g_hash_table_replace (installed_ids, g_strdup (id), NULL);
        }
    }

  ids_hash = dex_await_boxed (
      bz_get_user_data_ids_dex (),
      &local_error);

  if (ids_hash == NULL)
    {
      g_warning ("Failed to enumerate user data directories: %s",
                 local_error->message);
      return dex_future_new_for_error (g_steal_pointer (&local_error));
    }

  id_list = gtk_string_list_new (NULL);
  g_hash_table_iter_init (&iter, ids_hash);
  for (;;)
    {
      char *app_id = NULL;

      if (!g_hash_table_iter_next (&iter, (gpointer *) &app_id, NULL))
        break;

      if (!g_hash_table_contains (installed_ids, app_id) &&
          g_strcmp0 (app_id, own_id) != 0)
        gtk_string_list_append (id_list, app_id);
    }

  factory      = bz_state_info_get_application_factory (self->state);
  sorted_store = g_list_store_new (BZ_TYPE_ENTRY_GROUP);
  n_items      = g_list_model_get_n_items (G_LIST_MODEL (id_list));

  for (guint i = 0; i < n_items; i++)
    {
      GtkStringObject *str_obj       = NULL;
      g_autofree char *id_copy       = NULL;
      g_autoptr (BzEntryGroup) group = NULL;

      str_obj = g_list_model_get_item (G_LIST_MODEL (id_list), i);
      id_copy = g_strdup (gtk_string_object_get_string (str_obj));

      group = bz_application_map_factory_convert_one (factory, str_obj);
      if (group == NULL)
        group = bz_entry_group_new_manual (id_copy, id_copy, NULL);

      g_list_store_append (sorted_store, group);
    }

  g_list_store_sort (sorted_store, (GCompareDataFunc) compare_entry_groups_by_title, NULL);

  if (self->model != NULL)
    g_signal_handlers_disconnect_by_func (self->model, items_changed, self);

  g_clear_object (&self->model);
  self->model = G_LIST_MODEL (g_steal_pointer (&sorted_store));

  g_signal_connect_swapped (self->model, "items-changed", G_CALLBACK (items_changed), self);
  set_page (self);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_MODEL]);

  return dex_future_new_true ();
}
