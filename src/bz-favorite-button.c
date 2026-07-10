/* bz-favorite-button.c
 *
 * Copyright 2025 Alexander Vanhee
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

#include "bz-entry.h"
#include "env.h"
#include "error.h"
#include "bz-favorite-button.h"
#include "global-net.h"
#include "bz-state-info.h"

struct _BzFavoriteButton
{
  GtkButton parent_instance;

  BzEntry     *entry;
  BzStateInfo *state;

  GtkBox      *box;
  GtkImage    *icon;
  GtkRevealer *count_revealer;
  GtkLabel    *count_label;
  GtkStack    *stack;

  gboolean is_favorited;
};

G_DEFINE_FINAL_TYPE (BzFavoriteButton, bz_favorite_button, GTK_TYPE_BUTTON)

enum
{
  PROP_0,
  PROP_ENTRY,
  PROP_STATE,
  LAST_PROP
};

static GParamSpec *props[LAST_PROP] = { 0 };

static DexFuture *
fetch_favorite_status_fiber (BzFavoriteButton *button);

static DexFuture *
toggle_favorite_fiber (BzFavoriteButton *button);

static void
button_clicked_cb (BzFavoriteButton *self,
                   GtkButton        *button);

static void
update_icon (BzFavoriteButton *self);

static void
update_count (BzFavoriteButton *self);

static void
on_login_button_clicked (GtkButton  *button,
                         GtkPopover *popover);

static void
show_login_popover (BzFavoriteButton *self);

static gboolean
is_positive (gpointer object,
             int      value);

static char *
format_favorites_count (gpointer object,
                        int      count);

static void
bz_favorite_button_dispose (GObject *object)
{
  BzFavoriteButton *self = BZ_FAVORITE_BUTTON (object);

  g_clear_object (&self->entry);
  g_clear_object (&self->state);

  G_OBJECT_CLASS (bz_favorite_button_parent_class)->dispose (object);
}

static void
bz_favorite_button_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  BzFavoriteButton *self = BZ_FAVORITE_BUTTON (object);

  switch (prop_id)
    {
    case PROP_ENTRY:
      g_value_set_object (value, bz_favorite_button_get_entry (self));
      break;
    case PROP_STATE:
      g_value_set_object (value, bz_favorite_button_get_state (self));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_favorite_button_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  BzFavoriteButton *self = BZ_FAVORITE_BUTTON (object);

  switch (prop_id)
    {
    case PROP_ENTRY:
      bz_favorite_button_set_entry (self, g_value_get_object (value));
      break;
    case PROP_STATE:
      bz_favorite_button_set_state (self, g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_favorite_button_class_init (BzFavoriteButtonClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose      = bz_favorite_button_dispose;
  object_class->get_property = bz_favorite_button_get_property;
  object_class->set_property = bz_favorite_button_set_property;

  props[PROP_ENTRY] =
      g_param_spec_object (
          "entry",
          NULL, NULL,
          BZ_TYPE_ENTRY,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_STATE] =
      g_param_spec_object (
          "state",
          NULL, NULL,
          BZ_TYPE_STATE_INFO,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/kolunmi/Bazaar/bz-favorite-button.ui");
  gtk_widget_class_bind_template_child (widget_class, BzFavoriteButton, box);
  gtk_widget_class_bind_template_child (widget_class, BzFavoriteButton, icon);
  gtk_widget_class_bind_template_child (widget_class, BzFavoriteButton, count_revealer);
  gtk_widget_class_bind_template_child (widget_class, BzFavoriteButton, count_label);
  gtk_widget_class_bind_template_child (widget_class, BzFavoriteButton, stack);
  gtk_widget_class_bind_template_callback (widget_class, is_positive);
  gtk_widget_class_bind_template_callback (widget_class, format_favorites_count);
  gtk_widget_class_bind_template_callback (widget_class, button_clicked_cb);
}

static void
bz_favorite_button_init (BzFavoriteButton *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
  self->is_favorited = FALSE;
}

GtkWidget *
bz_favorite_button_new (void)
{
  return g_object_new (BZ_TYPE_FAVORITE_BUTTON, NULL);
}

void
bz_favorite_button_set_entry (BzFavoriteButton *self,
                              BzEntry          *entry)
{
  g_return_if_fail (BZ_IS_FAVORITE_BUTTON (self));
  g_return_if_fail (entry == NULL || BZ_IS_ENTRY (entry));

  if (g_set_object (&self->entry, entry))
    {
      update_count (self);
      g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ENTRY]);

      if (self->state != NULL && self->entry != NULL)
        {
          dex_future_disown (dex_scheduler_spawn (
              dex_scheduler_get_default (),
              bz_get_dex_stack_size (),
              (DexFiberFunc) fetch_favorite_status_fiber,
              g_object_ref (self),
              g_object_unref));
        }
    }
}

BzEntry *
bz_favorite_button_get_entry (BzFavoriteButton *self)
{
  g_return_val_if_fail (BZ_IS_FAVORITE_BUTTON (self), NULL);
  return self->entry;
}

void
bz_favorite_button_set_state (BzFavoriteButton *self,
                              BzStateInfo      *state)
{
  g_return_if_fail (BZ_IS_FAVORITE_BUTTON (self));
  g_return_if_fail (state == NULL || BZ_IS_STATE_INFO (state));

  if (g_set_object (&self->state, state))
    {
      g_object_notify_by_pspec (G_OBJECT (self), props[PROP_STATE]);

      if (self->state != NULL && self->entry != NULL)
        {
          dex_future_disown (dex_scheduler_spawn (
              dex_scheduler_get_default (),
              bz_get_dex_stack_size (),
              (DexFiberFunc) fetch_favorite_status_fiber,
              g_object_ref (self),
              g_object_unref));
        }
    }
}

BzStateInfo *
bz_favorite_button_get_state (BzFavoriteButton *self)
{
  g_return_val_if_fail (BZ_IS_FAVORITE_BUTTON (self), NULL);
  return self->state;
}

static gboolean
is_positive (gpointer object,
             int      value)
{
  return value > 0;
}

static char *
format_favorites_count (gpointer object,
                        int      count)
{
  if (count >= 1000000)
    return g_strdup_printf ("%.1fM", count / 1000000.0);
  else if (count >= 1000)
    return g_strdup_printf ("%.1fK", count / 1000.0);
  else
    return g_strdup_printf ("%d", count);
}

static void
update_icon (BzFavoriteButton *self)
{
  if (self->is_favorited)
    gtk_image_set_from_icon_name (self->icon, "bookmark-filled-symbolic");
  else
    gtk_image_set_from_icon_name (self->icon, "bookmark-outline-symbolic");
}

static void
update_count (BzFavoriteButton *self)
{
  int count = 0;

  if (self->entry != NULL)
    g_object_get (self->entry, "favorites-count", &count, NULL);

  gtk_revealer_set_reveal_child (self->count_revealer, count > 0);

  if (count > 0)
    {
      g_autofree char *formatted = format_favorites_count (NULL, count);
      gtk_label_set_label (self->count_label, formatted);
    }
}

static DexFuture *
fetch_favorite_status_fiber (BzFavoriteButton *button)
{
  g_autoptr (GError) local_error = NULL;
  g_autoptr (JsonNode) node      = NULL;
  g_autofree char *request       = NULL;
  BzAuthState     *auth_state    = NULL;
  const char      *token         = NULL;
  const char      *app_id        = NULL;

  if (button->state == NULL || button->entry == NULL)
    return NULL;

  auth_state = bz_state_info_get_auth_state (button->state);

  if (!bz_auth_state_is_authenticated (auth_state))
    return NULL;

  token = bz_auth_state_get_token (auth_state);
  if (token == NULL)
    return NULL;

  g_object_get (button->entry, "id", &app_id, NULL);

  request = g_strdup_printf ("/favorites/%s", app_id);

  node = dex_await_boxed (
      bz_query_flathub_v2_json_authenticated (request, token),
      &local_error);

  if (local_error == NULL && node != NULL && JSON_NODE_HOLDS_VALUE (node))
    {
      button->is_favorited = json_node_get_boolean (node);
      update_icon (button);
    }

  return NULL;
}

static DexFuture *
toggle_favorite_fiber (BzFavoriteButton *button)
{
  g_autoptr (GError) local_error = NULL;
  g_autofree char *request       = NULL;
  BzAuthState     *auth_state    = NULL;
  const char      *token         = NULL;
  const char      *app_id        = NULL;
  int              current_count = 0;

  if (button->state == NULL || button->entry == NULL)
    goto err;

  auth_state = bz_state_info_get_auth_state (button->state);

  if (!bz_auth_state_is_authenticated (auth_state))
    goto err;

  token = bz_auth_state_get_token (auth_state);
  if (token == NULL)
    goto err;

  g_object_get (button->entry,
                "id", &app_id,
                "favorites-count", &current_count,
                NULL);

  if (button->is_favorited)
    request = g_strdup_printf ("/favorites/%s/remove", app_id);
  else
    request = g_strdup_printf ("/favorites/%s/add", app_id);

  if (button->is_favorited)
    dex_await (
        bz_query_flathub_v2_json_authenticated_delete (request, token),
        &local_error);
  else
    dex_await (
        bz_query_flathub_v2_json_authenticated_post (request, token),
        &local_error);

  if (local_error != NULL)
    {
      GtkWidget *window = NULL;

      gtk_stack_set_visible_child_name (button->stack, "content");
      window = gtk_widget_get_ancestor (GTK_WIDGET (button), GTK_TYPE_WINDOW);
      if (window != NULL)
        bz_show_error_for_widget (window, _ ("Failed to update favorite"), local_error->message);
    }
  else
    {
      button->is_favorited = !button->is_favorited;

      g_object_set (button->entry,
                    "favorites-count", button->is_favorited ? current_count + 1 : current_count - 1,
                    NULL);

      update_icon (button);
      update_count (button);
      gtk_stack_set_visible_child_name (button->stack, "content");
    }

  return NULL;

err:
  gtk_stack_set_visible_child_name (button->stack, "content");
  return NULL;
}

static void
on_login_button_clicked (GtkButton  *button,
                         GtkPopover *popover)
{
  gtk_popover_popdown (popover);
}

static void
show_login_popover (BzFavoriteButton *self)
{
  GtkWidget *popover;
  GtkWidget *box;
  GtkWidget *label;
  GtkWidget *button;

  popover = gtk_popover_new ();
  gtk_widget_set_parent (popover, GTK_WIDGET (self));

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top (box, 6);
  gtk_widget_set_margin_bottom (box, 6);
  gtk_widget_set_margin_start (box, 6);
  gtk_widget_set_margin_end (box, 6);

  label = gtk_label_new (_ ("Log in with Flathub to manage favorites"));
  gtk_label_set_wrap (GTK_LABEL (label), TRUE);
  gtk_label_set_max_width_chars (GTK_LABEL (label), 17);
  gtk_label_set_justify (GTK_LABEL (label), GTK_JUSTIFY_CENTER);
  gtk_box_append (GTK_BOX (box), label);

  button = gtk_button_new_with_label (_ ("Log In"));
  gtk_widget_add_css_class (button, "suggested-action");
  gtk_actionable_set_action_name (GTK_ACTIONABLE (button), "app.flathub-login");
  g_signal_connect (button, "clicked", G_CALLBACK (on_login_button_clicked), popover);
  gtk_box_append (GTK_BOX (box), button);

  gtk_popover_set_child (GTK_POPOVER (popover), box);
  gtk_popover_popup (GTK_POPOVER (popover));
}

static void
button_clicked_cb (BzFavoriteButton *self,
                   GtkButton        *button)
{
  BzAuthState *auth_state = NULL;

  if (self->state == NULL)
    return;

  auth_state = bz_state_info_get_auth_state (self->state);

  if (!bz_auth_state_is_authenticated (auth_state))
    {
      show_login_popover (self);
      return;
    }

  gtk_stack_set_visible_child_name (self->stack, "spinner");

  dex_future_disown (dex_scheduler_spawn (
      dex_scheduler_get_default (),
      bz_get_dex_stack_size (),
      (DexFiberFunc) toggle_favorite_fiber,
      g_object_ref (self),
      g_object_unref));
}
