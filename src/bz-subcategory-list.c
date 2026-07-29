/* bz-subcategory-list.c
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

#include <glib/gi18n.h>
#include <libdex.h>

#include "bz-apps-page.h"
#include "bz-flathub-state.h"
#include "bz-flathub-sub-category.h"
#include "bz-subcategory-list.h"
#include "util.h"
#include "bz-window.h"

struct _BzSubcategoryList
{
  GtkBox parent_instance;

  BzFlathubCategory *category;
  BzFlathubState    *flathub_state;

  DexFuture *task;
};

G_DEFINE_FINAL_TYPE (BzSubcategoryList, bz_subcategory_list, GTK_TYPE_BOX);

enum
{
  PROP_0,
  PROP_CATEGORY,
  PROP_FLATHUB_STATE,
  LAST_PROP
};

static GParamSpec *props[LAST_PROP] = { 0 };


static DexFuture *
search_finally (DexFuture *future,
                GWeakRef  *wr)
{
  g_autoptr (BzSubcategoryList) self = NULL;
  g_autoptr (GError) local_error     = NULL;
  GtkRoot      *root                 = NULL;
  const GValue *value                = NULL;

  bz_weak_get_or_return_reject (self, wr);

  root = gtk_widget_get_root (GTK_WIDGET (self));

  value = dex_future_get_value (future, &local_error);
  if (value != NULL)
    {
      GListModel *model   = NULL;
      guint       n_items = 0;

      model   = g_value_get_object (value);
      n_items = g_list_model_get_n_items (model);

      if (n_items <= 1)
        {
          AdwToast *toast = adw_toast_new (_ ("No Results Found"));
          bz_window_add_toast (BZ_WINDOW (root), toast);
        }
      else
        {
          const char        *subcategory_name = NULL;
          g_autofree char   *subtitle         = NULL;
          AdwNavigationPage *apps_page        = NULL;
          GtkWidget         *nav_view         = NULL;

          subcategory_name = g_object_get_data (G_OBJECT (self), "current-subcategory");
          subtitle         = g_strdup_printf (_ ("%d Applications"), n_items);

          apps_page = bz_apps_page_new (subcategory_name, model);
          bz_apps_page_set_subtitle (BZ_APPS_PAGE (apps_page), subtitle);

          nav_view = gtk_widget_get_ancestor (GTK_WIDGET (self), ADW_TYPE_NAVIGATION_VIEW);
          adw_navigation_view_push (ADW_NAVIGATION_VIEW (nav_view), apps_page);
        }
    }
  else
    {
      AdwToast *toast = adw_toast_new (_ ("Search failed"));
      bz_window_add_toast (BZ_WINDOW (root), toast);
      g_warning ("Search failed: %s", local_error->message);
    }

  dex_clear (&self->task);
  return dex_future_new_true ();
}

static void
subcategory_button_clicked_cb (BzSubcategoryList *self,
                                GtkButton         *button)
{
  g_autoptr (DexFuture) future    = NULL;
  const char            *category = NULL;
  const char            *label    = NULL;
  g_autofree char       *route    = NULL;

  g_return_if_fail (BZ_IS_SUBCATEGORY_LIST (self));
  g_return_if_fail (GTK_IS_BUTTON (button));

  if (self->flathub_state == NULL)
    return;
  if (self->category == NULL)
    return;
  if (self->task != NULL)
    return;

  category = bz_flathub_category_get_name (self->category);
  if (category == NULL || *category == '\0')
    return;

  label = gtk_button_get_label (button);
  if (label == NULL || *label == '\0')
    return;

  g_object_set_data_full (G_OBJECT (self), "current-subcategory", g_strdup (label), g_free);

  route = g_strdup (g_object_get_data (G_OBJECT (button), "subcategory-id"));
  if (route == NULL)
    return;

  future = bz_flathub_state_search_collection (self->flathub_state, route);
  future = dex_future_finally (
      future,
      (DexFutureCallback) search_finally,
      bz_track_weak (self),
      bz_weak_release);
  self->task = g_steal_pointer (&future);
}

static void
rebuild_subcategories (BzSubcategoryList *self)
{
  GtkWidget *child;
  GListModel *subcategories;
  guint      n_items;

  dex_clear (&self->task);

  while ((child = gtk_widget_get_first_child (GTK_WIDGET (self))) != NULL)
    gtk_box_remove (GTK_BOX (self), child);

  if (self->category == NULL)
    {
      gtk_widget_set_visible (GTK_WIDGET (self), FALSE);
      return;
    }

  subcategories = bz_flathub_category_get_subcategories (self->category);
  if (subcategories == NULL)
    {
      gtk_widget_set_visible (GTK_WIDGET (self), FALSE);
      return;
    }

  n_items = g_list_model_get_n_items (subcategories);

  if (n_items == 0)
    {
      gtk_widget_set_visible (GTK_WIDGET (self), FALSE);
      return;
    }

  gtk_widget_set_visible (GTK_WIDGET (self), TRUE);

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr (BzFlathubSubCategory) subcategory = NULL;
      const char                    *label      = NULL;
      const char                    *subcat_id  = NULL;
      const char                    *category   = NULL;
      g_autofree char               *route      = NULL;
      GtkWidget                     *button     = NULL;

      subcategory = g_list_model_get_item (subcategories, i);
      if (subcategory == NULL)
        continue;

      label     = bz_flathub_sub_category_get_name(subcategory);
      subcat_id = bz_flathub_sub_category_get_id(subcategory);
      category  = bz_flathub_category_get_name (self->category);

      route = g_strdup_printf ("/collection/category/%s/subcategories?subcategory=%s",
                               category, subcat_id);

      button = gtk_button_new_with_label (label);
      gtk_widget_add_css_class (button, "small-pill");
      g_object_set_data_full (G_OBJECT (button), "subcategory-id", g_steal_pointer (&route), g_free);
      g_signal_connect_swapped (button, "clicked",
                                G_CALLBACK (subcategory_button_clicked_cb), self);
      gtk_box_append (GTK_BOX (self), button);
    }
}

static void
bz_subcategory_list_dispose (GObject *object)
{
  BzSubcategoryList *self = BZ_SUBCATEGORY_LIST (object);

  dex_clear (&self->task);
  g_clear_object (&self->category);
  g_clear_object (&self->flathub_state);

  G_OBJECT_CLASS (bz_subcategory_list_parent_class)->dispose (object);
}

static void
bz_subcategory_list_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  BzSubcategoryList *self = BZ_SUBCATEGORY_LIST (object);
  switch (prop_id)
    {
    case PROP_CATEGORY:
      g_value_set_object (value, bz_subcategory_list_get_category (self));
      break;
    case PROP_FLATHUB_STATE:
      g_value_set_object (value, bz_subcategory_list_get_flathub_state (self));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_subcategory_list_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  BzSubcategoryList *self = BZ_SUBCATEGORY_LIST (object);

  switch (prop_id)
    {
    case PROP_CATEGORY:
      bz_subcategory_list_set_category (self, g_value_get_object (value));
      break;
    case PROP_FLATHUB_STATE:
      bz_subcategory_list_set_flathub_state (self, g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_subcategory_list_class_init (BzSubcategoryListClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = bz_subcategory_list_set_property;
  object_class->get_property = bz_subcategory_list_get_property;
  object_class->dispose      = bz_subcategory_list_dispose;

  props[PROP_CATEGORY] =
      g_param_spec_object (
          "category",
          NULL, NULL,
          BZ_TYPE_FLATHUB_CATEGORY,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_FLATHUB_STATE] =
      g_param_spec_object (
          "flathub-state",
          NULL, NULL,
          BZ_TYPE_FLATHUB_STATE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, LAST_PROP, props);
}

static void
bz_subcategory_list_init (BzSubcategoryList *self)
{
  GtkLayoutManager *layout;

  gtk_orientable_set_orientation (GTK_ORIENTABLE (self), GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_set_visible (GTK_WIDGET (self), FALSE);

  layout = GTK_LAYOUT_MANAGER (adw_wrap_layout_new ());
  adw_wrap_layout_set_child_spacing (ADW_WRAP_LAYOUT (layout), 8);
  adw_wrap_layout_set_line_spacing (ADW_WRAP_LAYOUT (layout), 8);
  gtk_widget_set_layout_manager (GTK_WIDGET (self), layout);
}

GtkWidget *
bz_subcategory_list_new (void)
{
  return g_object_new (BZ_TYPE_SUBCATEGORY_LIST, NULL);
}

BzFlathubCategory *
bz_subcategory_list_get_category (BzSubcategoryList *self)
{
  g_return_val_if_fail (BZ_IS_SUBCATEGORY_LIST (self), NULL);
  return self->category;
}

void
bz_subcategory_list_set_category (BzSubcategoryList *self,
                                  BzFlathubCategory *category)
{
  g_return_if_fail (BZ_IS_SUBCATEGORY_LIST (self));
  g_return_if_fail (category == NULL || BZ_IS_FLATHUB_CATEGORY (category));

  g_clear_object (&self->category);

  if (category != NULL)
    self->category = g_object_ref (category);

  rebuild_subcategories (self);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_CATEGORY]);
}

BzFlathubState *
bz_subcategory_list_get_flathub_state (BzSubcategoryList *self)
{
  g_return_val_if_fail (BZ_IS_SUBCATEGORY_LIST (self), NULL);
  return self->flathub_state;
}

void
bz_subcategory_list_set_flathub_state (BzSubcategoryList *self,
                                       BzFlathubState    *flathub_state)
{
  g_return_if_fail (BZ_IS_SUBCATEGORY_LIST (self));
  g_return_if_fail (flathub_state == NULL || BZ_IS_FLATHUB_STATE (flathub_state));

  g_clear_object (&self->flathub_state);

  if (flathub_state != NULL)
    self->flathub_state = g_object_ref (flathub_state);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_FLATHUB_STATE]);
}
