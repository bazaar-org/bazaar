/* bz-all-apps-page.c
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

#include "bz-all-apps-page.h"
#include "bz-app-tile.h"
#include "bz-entry-group.h"

struct _BzAllAppsPage
{
  AdwNavigationPage parent_instance;

  char       *title;
  GListModel *applications;

  GtkGridView *grid_view;
};

G_DEFINE_FINAL_TYPE (BzAllAppsPage, bz_all_apps_page, ADW_TYPE_NAVIGATION_PAGE)

enum
{
  PROP_0,

  PROP_PAGE_TITLE,
  PROP_APPLICATIONS,

  LAST_PROP
};
static GParamSpec *props[LAST_PROP] = { 0 };

static void
bz_all_apps_page_dispose (GObject *object)
{
  BzAllAppsPage *self = BZ_ALL_APPS_PAGE (object);

  g_clear_pointer (&self->title, g_free);
  g_clear_object (&self->applications);

  G_OBJECT_CLASS (bz_all_apps_page_parent_class)->dispose (object);
}

static void
bz_all_apps_page_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  BzAllAppsPage *self = BZ_ALL_APPS_PAGE (object);

  switch (prop_id)
    {
    case PROP_PAGE_TITLE:
      g_value_set_string (value, self->title);
      break;
    case PROP_APPLICATIONS:
      g_value_set_object (value, self->applications);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_all_apps_page_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  BzAllAppsPage *self = BZ_ALL_APPS_PAGE (object);

  switch (prop_id)
    {
    case PROP_PAGE_TITLE:
      g_clear_pointer (&self->title, g_free);
      self->title = g_value_dup_string (value);
      break;
    case PROP_APPLICATIONS:
      g_clear_object (&self->applications);
      self->applications = g_value_dup_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_all_apps_page_class_init (BzAllAppsPageClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose      = bz_all_apps_page_dispose;
  object_class->get_property = bz_all_apps_page_get_property;
  object_class->set_property = bz_all_apps_page_set_property;

  props[PROP_PAGE_TITLE] =
      g_param_spec_string (
          "page-title",
          NULL, NULL, NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  props[PROP_APPLICATIONS] =
      g_param_spec_object (
          "applications",
          NULL, NULL,
          G_TYPE_LIST_MODEL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  g_type_ensure (BZ_TYPE_APP_TILE);

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/kolunmi/Bazaar/bz-all-apps-page.ui");
  gtk_widget_class_bind_template_child (widget_class, BzAllAppsPage, grid_view);
}

static void
bz_all_apps_page_init (BzAllAppsPage *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

AdwNavigationPage *
bz_all_apps_page_new (const char *title,
                      GListModel *applications)
{
  BzAllAppsPage *apps_page                   = NULL;
  g_autoptr (GtkNoSelection) selection_model = NULL;

  g_return_val_if_fail (applications != NULL, NULL);

  apps_page = g_object_new (
      BZ_TYPE_ALL_APPS_PAGE,
      "page-title", title,
      "applications", applications,
      "title", title,
      NULL);

  selection_model = gtk_no_selection_new (NULL);
  gtk_no_selection_set_model (selection_model, applications);
  gtk_grid_view_set_model (apps_page->grid_view, GTK_SELECTION_MODEL (selection_model));

  return ADW_NAVIGATION_PAGE (apps_page);
}
