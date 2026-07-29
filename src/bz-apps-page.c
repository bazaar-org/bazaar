/* bz-apps-page.c
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

#include <glib/gi18n.h>
#include <libdex.h>

#include "bz-all-apps-page.h"
#include "bz-app-tile.h"
#include "bz-application.h"
#include "bz-apps-page.h"
#include "bz-dynamic-list-view.h"
#include "bz-entry-group.h"
#include "env.h"
#include "bz-flathub-category.h"
#include "bz-subcategory-list.h"
#include "bz-state-info.h"

struct _BzAppsPage
{
  AdwNavigationPage parent_instance;

  char       *title;
  GListModel *applications;
  GListModel *all_applications;
  GListModel *carousel_applications;
  char       *subtitle;
  BzFlathubCategory *category;

  /* Template widgets */
};

G_DEFINE_FINAL_TYPE (BzAppsPage, bz_apps_page, ADW_TYPE_NAVIGATION_PAGE)

enum
{
  PROP_0,

  PROP_PAGE_TITLE,
  PROP_APPLICATIONS,
  PROP_ALL_APPLICATIONS,
  PROP_CAROUSEL_APPLICATIONS,
  PROP_PAGE_SUBTITLE,
  PROP_CATEGORY,
  PROP_FLATHUB_STATE,

  LAST_PROP
};
static GParamSpec *props[LAST_PROP] = { 0 };

static void
bz_apps_page_dispose (GObject *object)
{
  BzAppsPage *self = BZ_APPS_PAGE (object);

  g_clear_pointer (&self->title, g_free);
  g_clear_object (&self->applications);
  g_clear_object (&self->all_applications);
  g_clear_object (&self->carousel_applications);
  g_clear_pointer (&self->subtitle, g_free);
  g_clear_object (&self->category);

  G_OBJECT_CLASS (bz_apps_page_parent_class)->dispose (object);
}

static void
bz_apps_page_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
  BzAppsPage *self = BZ_APPS_PAGE (object);

  switch (prop_id)
    {
    case PROP_PAGE_TITLE:
      g_value_set_string (value, self->title);
      break;
    case PROP_APPLICATIONS:
      g_value_set_object (value, self->applications);
      break;
    case PROP_ALL_APPLICATIONS:
      g_value_set_object (value, self->all_applications);
      break;
    case PROP_CAROUSEL_APPLICATIONS:
      g_value_set_object (value, self->carousel_applications);
      break;
    case PROP_PAGE_SUBTITLE:
      g_value_set_string (value, self->subtitle);
      break;
    case PROP_CATEGORY:
      g_value_set_object (value, self->category);
      break;
    case PROP_FLATHUB_STATE:
      g_value_set_object (value, bz_state_info_get_flathub(bz_state_info_get_default ()));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_apps_page_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  BzAppsPage *self = BZ_APPS_PAGE (object);

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
    case PROP_ALL_APPLICATIONS:
      g_clear_object (&self->all_applications);
      self->all_applications = g_value_dup_object (value);
      break;
    case PROP_CAROUSEL_APPLICATIONS:
      g_clear_object (&self->carousel_applications);
      self->carousel_applications = g_value_dup_object (value);
      break;
    case PROP_PAGE_SUBTITLE:
      g_clear_pointer (&self->subtitle, g_free);
      self->subtitle = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static gboolean
is_not_null (gpointer object,
             GObject *value)
{
  return value != NULL;
}

static gboolean
is_not_empty_string (gpointer    object,
                     const char *str)
{
  return str != NULL && str[0] != '\0';
}

static gboolean
is_not_empty_list (gpointer    object,
                   GListModel *list)
{
  return list != NULL && g_list_model_get_n_items (list) > 0;
}

static gboolean
is_scrolled_down (gpointer object,
                  double   value,
                  gboolean carousel_visible)
{
  return value > (carousel_visible ? 400.0 : 50.0);
}

static void
featured_carousel_group_clicked_cb (BzAppsPage   *self,
                                    BzEntryGroup *group,
                                    GtkWidget    *carousel)
{
  gtk_widget_activate_action (GTK_WIDGET (self), "window.show-group", "s",
                              bz_entry_group_get_id (group));
}

static void
show_all_cb (BzAppsPage *self,
             GtkButton  *button)
{
  GtkWidget         *nav_view  = NULL;
  AdwNavigationPage *all_page  = NULL;
  g_autofree char   *all_title = NULL;
  guint              n_items   = 0;

  g_return_if_fail (BZ_IS_APPS_PAGE (self));

  if (self->all_applications == NULL)
    return;

  n_items = g_list_model_get_n_items (self->all_applications);

  if (n_items == 0)
    return;

  nav_view = gtk_widget_get_ancestor (GTK_WIDGET (self), ADW_TYPE_NAVIGATION_VIEW);
  if (nav_view == NULL)
    return;

  all_title = g_strdup_printf (_("All \"%s\""), self->title);
  all_page  = bz_all_apps_page_new (all_title, self->all_applications);
  if (all_page == NULL)
    return;

  adw_navigation_view_push (ADW_NAVIGATION_VIEW (nav_view), all_page);
}

static void
bz_apps_page_class_init (BzAppsPageClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose      = bz_apps_page_dispose;
  object_class->get_property = bz_apps_page_get_property;
  object_class->set_property = bz_apps_page_set_property;

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

  props[PROP_ALL_APPLICATIONS] =
      g_param_spec_object (
          "all-applications",
          NULL, NULL,
          G_TYPE_LIST_MODEL,
          G_PARAM_READWRITE);

  props[PROP_CAROUSEL_APPLICATIONS] =
      g_param_spec_object (
          "carousel-applications",
          NULL, NULL,
          G_TYPE_LIST_MODEL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
  props[PROP_PAGE_SUBTITLE] =
      g_param_spec_string (
          "page-subtitle",
          NULL, NULL, NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
  props[PROP_CATEGORY] =
      g_param_spec_object (
          "category",
          NULL, NULL,
          BZ_TYPE_FLATHUB_CATEGORY,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  props[PROP_FLATHUB_STATE] =
      g_param_spec_object (
          "flathub-state",
          NULL, NULL,
          BZ_TYPE_FLATHUB_STATE,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  g_type_ensure (BZ_TYPE_APP_TILE);
  g_type_ensure (BZ_TYPE_SUBCATEGORY_LIST);

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/kolunmi/Bazaar/bz-apps-page.ui");
  gtk_widget_class_bind_template_callback (widget_class, is_not_null);
  gtk_widget_class_bind_template_callback (widget_class, is_not_empty_string);
  gtk_widget_class_bind_template_callback (widget_class, is_not_empty_list);
  gtk_widget_class_bind_template_callback (widget_class, is_scrolled_down);
  gtk_widget_class_bind_template_callback (widget_class, featured_carousel_group_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, show_all_cb);
}

static void
bz_apps_page_init (BzAppsPage *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

AdwNavigationPage *
bz_apps_page_new (const char *title,
                  GListModel *applications)
{
  return bz_apps_page_new_with_carousel (title, applications, NULL);
}

AdwNavigationPage *
bz_apps_page_new_with_carousel (const char *title,
                                GListModel *applications,
                                GListModel *carousel_applications)
{
  BzAppsPage *apps_page = NULL;

  apps_page = g_object_new (
      BZ_TYPE_APPS_PAGE,
      "page-title", title,
      "applications", applications,
      "carousel-applications", carousel_applications,
      NULL);

  adw_navigation_page_set_title (ADW_NAVIGATION_PAGE (apps_page), title);

  return ADW_NAVIGATION_PAGE (apps_page);
}

static gboolean
filter_by_category (BzEntryGroup *group,
                    const char   *category_name)
{
  g_return_val_if_fail (BZ_IS_ENTRY_GROUP (group), FALSE);
  g_return_val_if_fail (category_name != NULL, FALSE);

  return bz_entry_group_has_category (group, category_name);
}

static DexFuture *
filter_applications_fiber (BzAppsPage *self)
{
  g_autoptr (GError) error                      = NULL;
  g_autoptr (GtkFilterListModel) filtered_model = NULL;
  GListModel  *all_model                        = NULL;
  const char  *category_name                    = NULL;
  BzStateInfo *state_info                       = NULL;

  state_info = bz_state_info_get_default ();
  if (state_info == NULL)
    return NULL;

  all_model = bz_state_info_get_filtered_entry_groups (state_info);
  if (all_model == NULL)
    return NULL;

  category_name = g_object_get_data (G_OBJECT (self), "category-name");
  if (category_name == NULL)
    return NULL;

  filtered_model = gtk_filter_list_model_new (
      g_object_ref (all_model),
      GTK_FILTER (gtk_custom_filter_new (
          (GtkCustomFilterFunc) filter_by_category,
          g_strdup (category_name),
          g_free)));

  g_set_object (&self->all_applications, G_LIST_MODEL (filtered_model));
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ALL_APPLICATIONS]);

  return NULL;
}

static AdwNavigationPage *
create_split_page (const char *title,
                   GListModel *model,
                   GListModel *carousel_model)
{
  AdwNavigationPage *apps_page = NULL;
  guint              n_items   = g_list_model_get_n_items (model);

  if (n_items > 48)
    {
      g_autoptr (GListModel) limited_model = NULL;
      limited_model                        = G_LIST_MODEL (gtk_slice_list_model_new (
          g_object_ref (model), 0, 48));

      if (carousel_model != NULL && g_list_model_get_n_items (carousel_model) > 0)
        apps_page = bz_apps_page_new_with_carousel (title, limited_model, carousel_model);
      else
        apps_page = bz_apps_page_new (title, limited_model);

      if (g_set_object (&BZ_APPS_PAGE (apps_page)->all_applications, model))
        g_object_notify_by_pspec (G_OBJECT (apps_page), props[PROP_ALL_APPLICATIONS]);
    }
  else
    {
      if (carousel_model != NULL && g_list_model_get_n_items (carousel_model) > 0)
        apps_page = bz_apps_page_new_with_carousel (title, model, carousel_model);
      else
        apps_page = bz_apps_page_new (title, model);
    }

  return apps_page;
}

static AdwNavigationPage *
create_standard_page (const char *title,
                      GListModel *model,
                      GListModel *carousel_model)
{
  if (carousel_model != NULL && g_list_model_get_n_items (carousel_model) > 0)
    return bz_apps_page_new_with_carousel (title, model, carousel_model);
  else
    return bz_apps_page_new (title, model);
}

static void
setup_category_filter (AdwNavigationPage *apps_page,
                       const char        *category_name)
{
  if (apps_page == NULL || category_name == NULL || g_strcmp0 (category_name, "adwaita") == 0)
    return;

  g_object_set_data_full (G_OBJECT (apps_page),
                          "category-name",
                          g_strdup (category_name),
                          g_free);

  dex_future_disown (dex_scheduler_spawn (
      dex_scheduler_get_default (),
      bz_get_dex_stack_size (),
      (DexFiberFunc) filter_applications_fiber,
      g_object_ref (apps_page),
      g_object_unref));
}

AdwNavigationPage *
bz_apps_page_new_from_category (BzFlathubCategory *category)
{
  g_autoptr (GListModel) model          = NULL;
  g_autoptr (GListModel) carousel_model = NULL;
  AdwNavigationPage *apps_page          = NULL;
  const char        *title              = NULL;
  const char        *category_name      = NULL;
  g_autofree char   *subtitle           = NULL;
  int                total_entries      = 0;
  guint              n_items            = 0;

  g_return_val_if_fail (BZ_IS_FLATHUB_CATEGORY (category), NULL);

  model = bz_flathub_category_dup_applications (category);
  if (model == NULL)
    return NULL;

  title         = bz_flathub_category_get_display_name (category);
  category_name = bz_flathub_category_get_name (category);
  n_items       = g_list_model_get_n_items (model);

  carousel_model = bz_flathub_category_dup_quality_applications (category);
  total_entries  = bz_flathub_category_get_total_entries (category);

  if (n_items > 48)
    apps_page = create_split_page (title, model, carousel_model);
  else
    apps_page = create_standard_page (title, model, carousel_model);

  if (total_entries > 0 && !bz_flathub_category_get_is_spotlight (category))
    {
      subtitle = g_strdup_printf (_ ("%d Applications"), total_entries);
      bz_apps_page_set_subtitle (BZ_APPS_PAGE (apps_page), subtitle);
    }

  BZ_APPS_PAGE(apps_page)->category = g_object_ref (category);
  g_object_notify_by_pspec (G_OBJECT (apps_page), props[PROP_CATEGORY]);

  if (n_items <= 48)
    setup_category_filter (apps_page, category_name);

  return apps_page;
}

void
bz_apps_page_set_subtitle (BzAppsPage *self,
                           const char *subtitle)
{
  g_return_if_fail (BZ_IS_APPS_PAGE (self));

  if (g_strcmp0 (self->subtitle, subtitle) == 0)
    return;

  g_clear_pointer (&self->subtitle, g_free);
  if (subtitle != NULL)
    self->subtitle = g_strdup (subtitle);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_PAGE_SUBTITLE]);
}
