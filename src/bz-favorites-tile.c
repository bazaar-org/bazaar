/* bz-favorites-tile.c
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

#include "bz-entry-group-util.h"
#include "bz-entry-group.h"
#include "bz-env.h"
#include "bz-error.h"
#include "bz-favorites-page.h"
#include "bz-favorites-tile.h"
#include "bz-global-net.h"
#include "bz-state-info.h"
#include "bz-window.h"

struct _BzFavoritesTile
{
  BzListTile parent_instance;

  BzEntryGroup *group;

  GtkLabel   *title_label;
  GtkLabel   *description_label;
  GtkButton  *install_remove_button;
  GtkButton  *support_button;
  GtkButton  *unfavorite_button;
  GtkStack   *unfavorite_stack;
};

G_DEFINE_FINAL_TYPE (BzFavoritesTile, bz_favorites_tile, BZ_TYPE_LIST_TILE)

enum
{
  PROP_0,
  PROP_GROUP,
  LAST_PROP
};

static GParamSpec *props[LAST_PROP] = { 0 };

enum
{
  SIGNAL_UNFAVORITED,
  LAST_SIGNAL,
};

static guint signals[LAST_SIGNAL];

static void
install_remove_cb (BzFavoritesTile *self,
                   GtkButton       *button);

static void
support_cb (BzFavoritesTile *self,
            GtkButton       *button);

static DexFuture *
unfavorite_fiber (BzFavoritesTile *tile);

static void
unfavorite_cb (BzFavoritesTile *self,
               GtkButton       *button);

static void
bz_favorites_tile_dispose (GObject *object)
{
  BzFavoritesTile *self = BZ_FAVORITES_TILE (object);

  g_clear_object (&self->group);

  G_OBJECT_CLASS (bz_favorites_tile_parent_class)->dispose (object);
}

static void
bz_favorites_tile_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  BzFavoritesTile *self = BZ_FAVORITES_TILE (object);

  switch (prop_id)
    {
    case PROP_GROUP:
      g_value_set_object (value, bz_favorites_tile_get_group (self));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_favorites_tile_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  BzFavoritesTile *self = BZ_FAVORITES_TILE (object);

  switch (prop_id)
    {
    case PROP_GROUP:
      bz_favorites_tile_set_group (self, g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static gboolean
invert_boolean (gpointer object,
                gboolean value)
{
  return !value;
}

static gboolean
is_null (gpointer object,
         GObject *value)
{
  return value == NULL;
}

static gboolean
is_zero (gpointer object,
         int      value)
{
  return value == 0;
}

static char *
get_install_remove_tooltip (gpointer object,
                            int      removable)
{
  if (removable > 0)
    return g_strdup (C_("Install Controls", "Uninstall"));
  else
    return g_strdup (C_("Install Controls", "Install"));
}

static char *
get_install_remove_icon (gpointer object,
                         int      removable)
{
  if (removable > 0)
    return g_strdup ("user-trash-symbolic");
  else
    return g_strdup ("document-save-symbolic");
}

static gboolean
switch_bool (gpointer object,
             gboolean condition,
             gboolean true_value,
             gboolean false_value)
{
  return condition ? true_value : false_value;
}

static gboolean
logical_and (gpointer object,
             gboolean value1,
             gboolean value2)
{
  return value1 && value2;
}

static void
bz_favorites_tile_class_init (BzFavoritesTileClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose      = bz_favorites_tile_dispose;
  object_class->get_property = bz_favorites_tile_get_property;
  object_class->set_property = bz_favorites_tile_set_property;

  props[PROP_GROUP] =
      g_param_spec_object (
          "group",
          NULL, NULL,
          BZ_TYPE_ENTRY_GROUP,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  signals[SIGNAL_UNFAVORITED] =
      g_signal_new (
          "unfavorited",
          G_OBJECT_CLASS_TYPE (klass),
          G_SIGNAL_RUN_FIRST,
          0,
          NULL, NULL,
          NULL,
          G_TYPE_NONE, 1,
          BZ_TYPE_ENTRY_GROUP);

  g_type_ensure (BZ_TYPE_LIST_TILE);
  g_type_ensure (BZ_TYPE_ENTRY_GROUP);

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/kolunmi/Bazaar/bz-favorites-tile.ui");
  gtk_widget_class_bind_template_child (widget_class, BzFavoritesTile, title_label);
  gtk_widget_class_bind_template_child (widget_class, BzFavoritesTile, description_label);
  gtk_widget_class_bind_template_child (widget_class, BzFavoritesTile, install_remove_button);
  gtk_widget_class_bind_template_child (widget_class, BzFavoritesTile, support_button);
  gtk_widget_class_bind_template_child (widget_class, BzFavoritesTile, unfavorite_button);
  gtk_widget_class_bind_template_child (widget_class, BzFavoritesTile, unfavorite_stack);
  gtk_widget_class_bind_template_callback (widget_class, invert_boolean);
  gtk_widget_class_bind_template_callback (widget_class, is_null);
  gtk_widget_class_bind_template_callback (widget_class, is_zero);
  gtk_widget_class_bind_template_callback (widget_class, switch_bool);
  gtk_widget_class_bind_template_callback (widget_class, logical_and);
  gtk_widget_class_bind_template_callback (widget_class, get_install_remove_tooltip);
  gtk_widget_class_bind_template_callback (widget_class, get_install_remove_icon);
  gtk_widget_class_bind_template_callback (widget_class, install_remove_cb);
  gtk_widget_class_bind_template_callback (widget_class, unfavorite_cb);
  gtk_widget_class_bind_template_callback (widget_class, support_cb);

  gtk_widget_class_set_accessible_role (widget_class, GTK_ACCESSIBLE_ROLE_BUTTON);
}

static void
bz_favorites_tile_init (BzFavoritesTile *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

GtkWidget *
bz_favorites_tile_new (void)
{
  return g_object_new (BZ_TYPE_FAVORITES_TILE, NULL);
}

void
bz_favorites_tile_set_group (BzFavoritesTile *self,
                             BzEntryGroup    *group)
{
  g_return_if_fail (BZ_IS_FAVORITES_TILE (self));
  g_return_if_fail (group == NULL || BZ_IS_ENTRY_GROUP (group));

  g_clear_object (&self->group);
  if (group != NULL)
    self->group = g_object_ref (group);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_GROUP]);
}

BzEntryGroup *
bz_favorites_tile_get_group (BzFavoritesTile *self)
{
  g_return_val_if_fail (BZ_IS_FAVORITES_TILE (self), NULL);
  return self->group;
}

static void
install_remove_cb (BzFavoritesTile *self,
                   GtkButton       *button)
{
  int removable = 0;

  if (self->group == NULL)
    return;

  removable = bz_entry_group_get_removable (self->group);

  if (removable > 0)
    gtk_widget_activate_action (GTK_WIDGET (self), "window.remove-group", "(sb)",
                                bz_entry_group_get_id (self->group), FALSE);
  else
    gtk_widget_activate_action (GTK_WIDGET (self), "window.install-group", "(sb)",
                                bz_entry_group_get_id (self->group), TRUE);
}

static void
support_cb (BzFavoritesTile *self,
            GtkButton       *button)
{
  const char *url = NULL;

  if (self->group == NULL)
    return;

  url = bz_entry_group_get_donation_url (self->group);
  if (url == NULL)
    return;

  g_app_info_launch_default_for_uri (url, NULL, NULL);
}

static DexFuture *
unfavorite_fiber (BzFavoritesTile *tile)
{
  g_autoptr (GError) local_error = NULL;
  g_autoptr (BzStateInfo) state  = NULL;
  g_autofree char *request       = NULL;
  BzFavoritesPage *page          = NULL;
  BzAuthState     *auth_state    = NULL;
  const char      *token         = NULL;
  const char      *app_id        = NULL;
  GtkWidget       *revealer      = NULL;
  GtkWidget       *row           = NULL;

  revealer = gtk_widget_get_parent (GTK_WIDGET (tile));
  row      = gtk_widget_get_parent (GTK_WIDGET (revealer));

  page = BZ_FAVORITES_PAGE (gtk_widget_get_ancestor (GTK_WIDGET (tile), BZ_TYPE_FAVORITES_PAGE));
  if (page == NULL)
    return NULL;

  g_object_get (page, "state", &state, NULL);
  if (state == NULL)
    return NULL;

  auth_state = bz_state_info_get_auth_state (state);
  token      = bz_auth_state_get_token (auth_state);

  if (token == NULL || !bz_auth_state_is_authenticated (auth_state))
    return NULL;

  app_id  = bz_entry_group_get_id (tile->group);
  request = g_strdup_printf ("/favorites/%s/remove", app_id);

  dex_await (
      bz_query_flathub_v2_json_authenticated_delete (request, token),
      &local_error);

  if (local_error != NULL)
    {
      GtkWidget *window = NULL;

      gtk_stack_set_visible_child_name (tile->unfavorite_stack, "button");
      window = gtk_widget_get_ancestor (GTK_WIDGET (tile), GTK_TYPE_WINDOW);
      if (window != NULL)
        bz_show_error_for_widget (window, _("Failed to remove favorite"), local_error->message);
    }
  else
    {
      gtk_widget_set_overflow (revealer, GTK_OVERFLOW_HIDDEN);
      gtk_revealer_set_reveal_child (GTK_REVEALER (revealer), FALSE);
      gtk_widget_add_css_class (row, "hidden");
      g_signal_emit (tile, signals[SIGNAL_UNFAVORITED], 0, tile->group);
    }

  return NULL;
}

static void
unfavorite_cb (BzFavoritesTile *self,
               GtkButton       *button)
{
  gtk_stack_set_visible_child_name (self->unfavorite_stack, "spinner");

  dex_future_disown (dex_scheduler_spawn (
      dex_scheduler_get_default (),
      bz_get_dex_stack_size (),
      (DexFiberFunc) unfavorite_fiber,
      g_object_ref (self),
      g_object_unref));
}
