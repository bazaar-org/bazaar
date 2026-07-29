/* bz-featured-carousel.c
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

#include <bge.h>

#include "bz-entry-group.h"
#include "bz-featured-carousel.h"
#include "bz-featured-tile.h"
#include "template-callbacks.h"

#define FEATURED_ROTATE_TIME       5
#define MANUAL_ROTATE_RECOVER_TIME 7.5

struct _BzFeaturedCarousel
{
  GtkBox parent_instance;

  GListModel *model;
  gboolean    auto_rotate;

  guint    rotation_timer_source;
  GTimer  *time_since_manual_rotate;
  gboolean has_focus;

  BgeCarousel        *carousel;
  GtkSingleSelection *selection;
  GtkButton          *next_button;
  GtkButton          *previous_button;
};

G_DEFINE_FINAL_TYPE (BzFeaturedCarousel, bz_featured_carousel, GTK_TYPE_BOX)

enum
{
  PROP_0,
  PROP_MODEL,
  PROP_AUTO_ROTATE,
  LAST_PROP
};

static GParamSpec *props[LAST_PROP] = {
  NULL,
};

static void
update_buttons_for_tile (BzFeaturedCarousel *self)
{
  GtkWidget  *child      = NULL;
  const char *class_name = NULL;
  guint       index      = 0;

  index = gtk_single_selection_get_selected (self->selection);
  child = bge_carousel_get_nth_page (self->carousel, index);
  if (child == NULL)
    return;

  class_name = gtk_widget_has_css_class (child, "flathub-lotion")
                   ? "flathub-lotion"
                   : "flathub-gunmetal";

  gtk_widget_remove_css_class (GTK_WIDGET (self->next_button), "flathub-lotion");
  gtk_widget_remove_css_class (GTK_WIDGET (self->next_button), "flathub-gunmetal");
  gtk_widget_remove_css_class (GTK_WIDGET (self->previous_button), "flathub-lotion");
  gtk_widget_remove_css_class (GTK_WIDGET (self->previous_button), "flathub-gunmetal");
  gtk_widget_add_css_class (GTK_WIDGET (self->next_button), class_name);
  gtk_widget_add_css_class (GTK_WIDGET (self->previous_button), class_name);
}

static void
on_style_changed (AdwStyleManager    *style_manager,
                  GParamSpec         *pspec,
                  BzFeaturedCarousel *self)
{
  g_idle_add_once ((GSourceOnceFunc) update_buttons_for_tile, self);
}

static void
announce_selected_entry (BzFeaturedCarousel *self)
{
  guint index                    = 0;
  g_autoptr (BzEntryGroup) group = NULL;
  const char      *title         = NULL;
  const char      *description   = NULL;
  g_autofree char *message       = NULL;

  index = gtk_single_selection_get_selected (self->selection);

  group = g_list_model_get_item (G_LIST_MODEL (self->selection), index);

  if (group == NULL)
    return;

  title       = bz_entry_group_get_title (group);
  description = bz_entry_group_get_description (group);

  message = g_strdup_printf ("%s. %s",
                             title != NULL ? title : "",
                             description != NULL ? description : "");

  gtk_accessible_announce (GTK_ACCESSIBLE (self), message,
                           GTK_ACCESSIBLE_ANNOUNCEMENT_PRIORITY_MEDIUM);
}

static void
carousel_focus_cb (BgeCarousel        *self,
                   GParamSpec         *pspec,
                   BzFeaturedCarousel *carousel)
{
  if (gtk_widget_has_focus (GTK_WIDGET (self)))
    announce_selected_entry (carousel);
}

static void
on_focus_enter_cb (BzFeaturedCarousel *self)
{
  self->has_focus = TRUE;
}

static void
on_focus_leave_cb (BzFeaturedCarousel *self)
{
  self->has_focus = FALSE;
}

static void
on_notify_selected (BzFeaturedCarousel *self,
                    GParamSpec         *pspec,
                    GtkSingleSelection *selection)
{
  g_timer_start (self->time_since_manual_rotate);
  update_buttons_for_tile (self);
}

static void
show_relative_page (BzFeaturedCarousel *self,
                    gint                delta,
                    gboolean            use_custom_spring)
{
  guint        n_pages;
  guint        current_page;
  guint        new_page;
  GtkSettings *settings       = NULL;
  gboolean     reduced_motion = FALSE;

  settings = gtk_widget_get_settings (GTK_WIDGET (self));
  g_object_get (settings, "gtk-interface-reduced-motion", &reduced_motion, NULL);

  if (reduced_motion && use_custom_spring)
    return;

  n_pages = g_list_model_get_n_items (G_LIST_MODEL (self->selection));
  if (n_pages == 0)
    return;

  current_page = gtk_single_selection_get_selected (self->selection);
  new_page     = (n_pages + current_page + delta) % n_pages;

  g_signal_handlers_block_by_func (self->selection, on_notify_selected, self);
  gtk_single_selection_set_selected (self->selection, new_page);
  g_signal_handlers_unblock_by_func (self->selection, on_notify_selected, self);

  if (self->has_focus)
    announce_selected_entry (self);

  update_buttons_for_tile (self);
}

static gboolean
rotate_cb (gpointer user_data)
{
  BzFeaturedCarousel *self    = BZ_FEATURED_CAROUSEL (user_data);
  double              elapsed = 0.0;

  if (!self->auto_rotate)
    return G_SOURCE_CONTINUE;

  if (self->has_focus)
    return G_SOURCE_CONTINUE;

  elapsed = g_timer_elapsed (self->time_since_manual_rotate, NULL);
  if (elapsed > MANUAL_ROTATE_RECOVER_TIME)
    show_relative_page (self, +1, TRUE);

  return G_SOURCE_CONTINUE;
}

static void
next_button_clicked_cb (GtkButton *button,
                        gpointer   user_data)
{
  BzFeaturedCarousel *self;

  self = BZ_FEATURED_CAROUSEL (user_data);
  show_relative_page (self, +1, FALSE);
}

static void
previous_button_clicked_cb (GtkButton *button,
                            gpointer   user_data)
{
  BzFeaturedCarousel *self;

  self = BZ_FEATURED_CAROUSEL (user_data);
  show_relative_page (self, -1, FALSE);
}

static void
tile_clicked_cb (BzFeaturedTile *tile,
                 gpointer        user_data)
{
  BzEntryGroup *group = NULL;
  group               = bz_featured_tile_get_group (tile);
  gtk_widget_activate_action (GTK_WIDGET (user_data), "window.show-group", "s",
                              bz_entry_group_get_id (group));
}

static gboolean
key_pressed_cb (GtkEventControllerKey *controller,
                guint                  keyval,
                guint                  keycode,
                GdkModifierType        state,
                BzFeaturedCarousel    *self)
{
  if (gtk_widget_is_visible (GTK_WIDGET (self->previous_button)) &&
      gtk_widget_is_sensitive (GTK_WIDGET (self->previous_button)) &&
      ((gtk_widget_get_direction (GTK_WIDGET (self->previous_button)) == GTK_TEXT_DIR_LTR && keyval == GDK_KEY_Left) ||
       (gtk_widget_get_direction (GTK_WIDGET (self->previous_button)) == GTK_TEXT_DIR_RTL && keyval == GDK_KEY_Right)))
    {
      gtk_widget_activate (GTK_WIDGET (self->previous_button));
      return GDK_EVENT_STOP;
    }

  if (gtk_widget_is_visible (GTK_WIDGET (self->next_button)) &&
      gtk_widget_is_sensitive (GTK_WIDGET (self->next_button)) &&
      ((gtk_widget_get_direction (GTK_WIDGET (self->next_button)) == GTK_TEXT_DIR_LTR && keyval == GDK_KEY_Right) ||
       (gtk_widget_get_direction (GTK_WIDGET (self->next_button)) == GTK_TEXT_DIR_RTL && keyval == GDK_KEY_Left)))
    {
      gtk_widget_activate (GTK_WIDGET (self->next_button));
      return GDK_EVENT_STOP;
    }

  switch (keyval)
    {
    case GDK_KEY_Return:
    case GDK_KEY_KP_Enter:
    case GDK_KEY_ISO_Enter:
    case GDK_KEY_space:
    case GDK_KEY_KP_Space:
      {
        GtkWidget *tile = NULL;

        tile = bge_carousel_get_nth_page (self->carousel, gtk_single_selection_get_selected (self->selection));

        if (tile != NULL)
          g_signal_emit_by_name (tile, "clicked");

        return GDK_EVENT_STOP;
      }
    default:
      break;
    }

  return GDK_EVENT_PROPAGATE;
}

static void
bz_featured_carousel_dispose (GObject *object)
{
  BzFeaturedCarousel *self = BZ_FEATURED_CAROUSEL (object);

  g_signal_handlers_disconnect_by_func (adw_style_manager_get_default (),
                                        on_style_changed, self);

  g_clear_handle_id (&self->rotation_timer_source, g_source_remove);
  g_clear_pointer (&self->time_since_manual_rotate, g_timer_destroy);

  g_clear_object (&self->model);

  G_OBJECT_CLASS (bz_featured_carousel_parent_class)->dispose (object);
}

static void
bz_featured_carousel_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  BzFeaturedCarousel *self;

  self = BZ_FEATURED_CAROUSEL (object);

  switch (prop_id)
    {
    case PROP_MODEL:
      g_value_set_object (value, bz_featured_carousel_get_model (self));
      break;
    case PROP_AUTO_ROTATE:
      g_value_set_boolean (value, self->auto_rotate);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
bz_featured_carousel_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  BzFeaturedCarousel *self;

  self = BZ_FEATURED_CAROUSEL (object);

  switch (prop_id)
    {
    case PROP_MODEL:
      bz_featured_carousel_set_model (self, g_value_get_object (value));
      break;
    case PROP_AUTO_ROTATE:
      self->auto_rotate = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static GtkWidget *
on_create_widget (BzFeaturedCarousel *self,
                  BzEntryGroup       *group,
                  BgeCarousel        *carousel)
{
  BzFeaturedTile *tile = NULL;

  tile = bz_featured_tile_new (group);

  gtk_widget_set_hexpand (GTK_WIDGET (tile), TRUE);
  gtk_widget_set_vexpand (GTK_WIDGET (tile), TRUE);
  gtk_widget_set_can_focus (GTK_WIDGET (tile), FALSE);

  g_signal_connect (tile, "clicked",
                    G_CALLBACK (tile_clicked_cb), self);

  return GTK_WIDGET (tile);
}

static void
on_remove_widget (BzFeaturedCarousel *self,
                  BzFeaturedTile     *tile,
                  BzEntryGroup       *group,
                  BgeCarousel        *carousel)
{
}

static void
bz_featured_carousel_class_init (BzFeaturedCarouselClass *klass)
{
  GObjectClass   *object_class;
  GtkWidgetClass *widget_class;

  object_class = G_OBJECT_CLASS (klass);
  widget_class = GTK_WIDGET_CLASS (klass);

  object_class->get_property = bz_featured_carousel_get_property;
  object_class->set_property = bz_featured_carousel_set_property;
  object_class->dispose      = bz_featured_carousel_dispose;

  props[PROP_MODEL] =
      g_param_spec_object ("model", NULL, NULL,
                           G_TYPE_LIST_MODEL,
                           G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  props[PROP_AUTO_ROTATE] =
      g_param_spec_boolean ("auto-rotate", NULL, NULL,
                            TRUE,
                            G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/kolunmi/Bazaar/bz-featured-carousel.ui");
  gtk_widget_class_set_accessible_role (widget_class, GTK_ACCESSIBLE_ROLE_GROUP);

  bz_widget_class_bind_all_util_callbacks (widget_class);

  gtk_widget_class_bind_template_child (widget_class, BzFeaturedCarousel, carousel);
  gtk_widget_class_bind_template_child (widget_class, BzFeaturedCarousel, selection);
  gtk_widget_class_bind_template_child (widget_class, BzFeaturedCarousel, next_button);
  gtk_widget_class_bind_template_child (widget_class, BzFeaturedCarousel, previous_button);

  gtk_widget_class_bind_template_callback (widget_class, on_create_widget);
  gtk_widget_class_bind_template_callback (widget_class, on_remove_widget);
  gtk_widget_class_bind_template_callback (widget_class, on_notify_selected);
  gtk_widget_class_bind_template_callback (widget_class, next_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, previous_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, carousel_focus_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_focus_enter_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_focus_leave_cb);
  gtk_widget_class_bind_template_callback (widget_class, key_pressed_cb);
}

static void
bz_featured_carousel_init (BzFeaturedCarousel *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
  self->auto_rotate = TRUE;

  self->rotation_timer_source = g_timeout_add_seconds (
      FEATURED_ROTATE_TIME,
      rotate_cb, self);
  self->time_since_manual_rotate = g_timer_new ();

  g_signal_connect (adw_style_manager_get_default (), "notify::dark",
                    G_CALLBACK (on_style_changed), self);
}

BzFeaturedCarousel *
bz_featured_carousel_new (void)
{
  return g_object_new (BZ_TYPE_FEATURED_CAROUSEL, NULL);
}

GListModel *
bz_featured_carousel_get_model (BzFeaturedCarousel *self)
{
  g_return_val_if_fail (BZ_IS_FEATURED_CAROUSEL (self), NULL);
  return self->model;
}

void
bz_featured_carousel_set_model (BzFeaturedCarousel *self,
                                GListModel         *model)
{
  g_return_if_fail (BZ_IS_FEATURED_CAROUSEL (self));
  g_return_if_fail (model == NULL || G_IS_LIST_MODEL (model));

  if (model != NULL && model == self->model)
    return;

  g_clear_object (&self->model);
  if (model != NULL)
    self->model = g_object_ref (model);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_MODEL]);
}
