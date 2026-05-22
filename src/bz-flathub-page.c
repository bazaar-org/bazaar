/* bz-flathub-page.c
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

#include <glib/gi18n.h>

#include "bz-app-tile.h"
#include "bz-apps-page.h"
#include "bz-dynamic-list-view.h"
#include "bz-entry-group.h"
#include "bz-featured-carousel.h"
#include "bz-flathub-category-section.h"
#include "bz-flathub-category.h"
#include "bz-flathub-page.h"
#include "bz-section-view.h"

struct _BzFlathubPage
{
  AdwBin parent_instance;

  BzStateInfo *state;

  /* Template widgets */
  AdwViewStack *stack;
};

G_DEFINE_FINAL_TYPE (BzFlathubPage, bz_flathub_page, ADW_TYPE_BIN)

enum
{
  PROP_0,

  PROP_STATE,

  LAST_PROP
};
static GParamSpec *props[LAST_PROP] = { 0 };

enum
{
  SIGNAL_OPEN_SEARCH,

  LAST_SIGNAL,
};
static guint signals[LAST_SIGNAL];

static BzFlathubCategory *
get_category_by_name (GListModel *categories,
                      const char *name);

static void
invalidating_state_changed (BzFlathubPage *self,
                            GParamSpec    *pspec,
                            BzStateInfo   *info);

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

static void
tile_clicked (BzEntryGroup *group,
              GtkButton    *button);

static void
show_more_clicked (BzFlathubPage *self,
                   GtkButton     *button,
                   const char    *category_name);

static void
bz_flathub_page_dispose (GObject *object)
{
  BzFlathubPage *self = BZ_FLATHUB_PAGE (object);

  if (self->state != NULL)
    g_signal_handlers_disconnect_by_func (self->state, invalidating_state_changed, self);
  g_clear_object (&self->state);

  G_OBJECT_CLASS (bz_flathub_page_parent_class)->dispose (object);
}

static void
bz_flathub_page_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  BzFlathubPage *self = BZ_FLATHUB_PAGE (object);

  switch (prop_id)
    {
    case PROP_STATE:
      g_value_set_object (value, bz_flathub_page_get_state (self));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_flathub_page_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  BzFlathubPage *self = BZ_FLATHUB_PAGE (object);

  switch (prop_id)
    {
    case PROP_STATE:
      bz_flathub_page_set_state (self, g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bind_widget_cb (BzFlathubPage     *self,
                BzAppTile         *tile,
                BzEntryGroup      *group,
                BzDynamicListView *view)
{
  g_signal_connect_swapped (tile, "clicked", G_CALLBACK (tile_clicked), group);
}

static void
unbind_widget_cb (BzFlathubPage     *self,
                  BzAppTile         *tile,
                  BzEntryGroup      *group,
                  BzDynamicListView *view)
{
  g_signal_handlers_disconnect_by_func (tile, G_CALLBACK (tile_clicked), group);
}

static void
show_more_mobile_cb (BzFlathubPage *self,
                     GtkButton     *button)
{
  show_more_clicked (self, button, "mobile");
}

static void
show_more_gaming_cb (BzFlathubPage *self,
                     GtkButton     *button)
{
  show_more_clicked (self, button, "game");
}

static BzFlathubCategory *
get_category_by_name (GListModel *categories,
                      const char *name)
{
  guint n_items;
  guint i;

  if (categories == NULL)
    return NULL;

  n_items = g_list_model_get_n_items (categories);

  for (i = 0; i < n_items; i++)
    {
      g_autoptr (BzFlathubCategory) category = g_list_model_get_item (categories, i);
      const char *category_name;

      category_name = bz_flathub_category_get_name (category);

      if (g_strcmp0 (category_name, name) == 0)
        return g_object_ref (category);
    }

  return NULL;
}

static gpointer
get_category_by_name_cb (gpointer    object,
                         gpointer    categories_obj,
                         const char *name)
{
  return get_category_by_name (G_LIST_MODEL (categories_obj), name);
}

static void
open_search_cb (BzFlathubPage *self,
                GtkButton     *button)
{
  g_signal_emit (self, signals[SIGNAL_OPEN_SEARCH], 0);
}

static void
show_group_action (GtkWidget    *widget,
                   BzEntryGroup *group)
{
  gtk_widget_activate_action (widget, "window.show-group", "s",
                              bz_entry_group_get_id (group));
}

static void
bz_flathub_page_class_init (BzFlathubPageClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose      = bz_flathub_page_dispose;
  object_class->get_property = bz_flathub_page_get_property;
  object_class->set_property = bz_flathub_page_set_property;

  props[PROP_STATE] =
      g_param_spec_object (
          "state",
          NULL, NULL,
          BZ_TYPE_STATE_INFO,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  signals[SIGNAL_OPEN_SEARCH] =
      g_signal_new (
          "open-search",
          G_OBJECT_CLASS_TYPE (klass),
          G_SIGNAL_RUN_FIRST,
          0,
          NULL, NULL,
          g_cclosure_marshal_VOID__VOID,
          G_TYPE_NONE, 0);

  g_type_ensure (BZ_TYPE_SECTION_VIEW);
  g_type_ensure (BZ_TYPE_FLATHUB_CATEGORY_SECTION);
  g_type_ensure (BZ_TYPE_FLATHUB_CATEGORY);
  g_type_ensure (BZ_TYPE_DYNAMIC_LIST_VIEW);
  g_type_ensure (BZ_TYPE_APP_TILE);
  g_type_ensure (BZ_TYPE_FEATURED_CAROUSEL);

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/kolunmi/Bazaar/bz-flathub-page.ui");
  gtk_widget_class_bind_template_child (widget_class, BzFlathubPage, stack);
  gtk_widget_class_bind_template_callback (widget_class, invert_boolean);
  gtk_widget_class_bind_template_callback (widget_class, is_null);
  gtk_widget_class_bind_template_callback (widget_class, bind_widget_cb);
  gtk_widget_class_bind_template_callback (widget_class, unbind_widget_cb);
  gtk_widget_class_bind_template_callback (widget_class, get_category_by_name_cb);
  gtk_widget_class_bind_template_callback (widget_class, show_more_mobile_cb);
  gtk_widget_class_bind_template_callback (widget_class, show_more_gaming_cb);
  gtk_widget_class_bind_template_callback (widget_class, open_search_cb);
}

static void
bz_flathub_page_init (BzFlathubPage *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

GtkWidget *
bz_flathub_page_new (void)
{
  return g_object_new (BZ_TYPE_FLATHUB_PAGE, NULL);
}

void
bz_flathub_page_set_state (BzFlathubPage *self,
                           BzStateInfo   *state)
{
  g_return_if_fail (BZ_IS_FLATHUB_PAGE (self));
  g_return_if_fail (state == NULL || BZ_IS_STATE_INFO (state));

  if (self->state != NULL)
    g_signal_handlers_disconnect_by_func (self->state, invalidating_state_changed, self);

  g_clear_object (&self->state);
  if (state != NULL)
    {
      self->state = g_object_ref (state);
      g_signal_connect_swapped (
          state,
          "notify::flathub",
          G_CALLBACK (invalidating_state_changed),
          self);
      g_signal_connect_swapped (
          state,
          "notify::has-flathub",
          G_CALLBACK (invalidating_state_changed),
          self);
      g_signal_connect_swapped (
          state,
          "notify::online",
          G_CALLBACK (invalidating_state_changed),
          self);
    }

  invalidating_state_changed (self, NULL, state);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_STATE]);
}

BzStateInfo *
bz_flathub_page_get_state (BzFlathubPage *self)
{
  g_return_val_if_fail (BZ_IS_FLATHUB_PAGE (self), NULL);
  return self->state;
}

static void
show_more_clicked (BzFlathubPage *self,
                   GtkButton     *button,
                   const char    *category_name)
{
  g_autoptr (BzFlathubCategory) category = NULL;
  GtkWidget         *nav_view            = NULL;
  AdwNavigationPage *apps_page           = NULL;

  category = get_category_by_name (
      bz_flathub_state_get_categories (
          bz_state_info_get_flathub (self->state)),
      category_name);
  if (category == NULL)
    return;

  apps_page = bz_apps_page_new_from_category (category);

  nav_view = gtk_widget_get_ancestor (GTK_WIDGET (self), ADW_TYPE_NAVIGATION_VIEW);
  g_assert (nav_view != NULL);

  adw_navigation_view_push (ADW_NAVIGATION_VIEW (nav_view), apps_page);
}

static void
tile_clicked (BzEntryGroup *group,
              GtkButton    *button)
{
  show_group_action (GTK_WIDGET (button), group);
}

static void
invalidating_state_changed (BzFlathubPage *self,
                            GParamSpec    *pspec,
                            BzStateInfo   *info)
{
  BzFlathubState *flathub  = NULL;
  gboolean        has_repo = FALSE;
  const char     *page     = NULL;

  if (self->state != NULL)
    {
      flathub  = bz_state_info_get_flathub (self->state);
      has_repo = bz_state_info_get_has_flathub (self->state);
    }

  if (flathub != NULL && has_repo)
    page = "content";
  else if (!has_repo)
    page = "empty";
  else
    page = "offline";

  adw_view_stack_set_visible_child_name (self->stack, page);
}
