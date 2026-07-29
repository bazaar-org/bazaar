/* bz-metainfo-preview.c
 *
 * Copyright 2026 Alexander Vanhee
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
#include "bz-application.h"
#include "bz-dynamic-list-view.h"
#include "bz-entry-group.h"
#include "bz-featured-carousel.h"
#include "bz-flathub-category.h"
#include "bz-metainfo-preview.h"
#include "bz-rich-app-tile.h"
#include "bz-state-info.h"
#include "error.h"
#include "util.h"

BZ_DEFINE_DATA (
    open_metainfo,
    OpenMetainfo,
    {
      DexPromise *promise;
      GFile      *metainfo_file;
      GtkWindow  *parent_window;
    },
    BZ_RELEASE_DATA (promise, dex_unref);
    BZ_RELEASE_DATA (metainfo_file, g_object_unref);
    BZ_RELEASE_DATA (parent_window, g_object_unref))

static DexFuture *
on_icon_prompt_finally (DexFuture        *future,
                        OpenMetainfoData *data);

static void
on_icon_chosen (GtkFileDialog    *dialog,
                GAsyncResult     *result,
                OpenMetainfoData *data);

static BzMetainfoPickResult *
bz_metainfo_pick_result_copy (BzMetainfoPickResult *result);

static void
append_quality_apps (BzFlathubState *flathub,
                     const char     *category_name,
                     guint           count,
                     GListStore     *destination);

GType
bz_metainfo_pick_result_get_type (void)
{
  static GType type = 0;

  if (type == 0)
    type = g_boxed_type_register_static (
        "BzMetainfoPickResult",
        (GBoxedCopyFunc) bz_metainfo_pick_result_copy,
        (GBoxedFreeFunc) bz_metainfo_pick_result_free);
  return type;
}

void
bz_metainfo_pick_result_free (BzMetainfoPickResult *result)
{
  g_clear_object (&result->metainfo_file);
  g_clear_object (&result->icon_file);
  g_free (result);
}

DexFuture *
bz_metainfo_preview_open_file (GFile     *metainfo_file,
                               GtkWindow *parent_window)
{
  g_autoptr (DexPromise) promise     = dex_promise_new ();
  g_autoptr (DexFuture) alert_future = NULL;
  AdwDialog        *alert            = NULL;
  OpenMetainfoData *data             = NULL;

  g_assert (metainfo_file != NULL);

  data                = open_metainfo_data_new ();
  data->promise       = dex_ref (promise);
  data->metainfo_file = g_object_ref (metainfo_file);
  data->parent_window = parent_window != NULL ? g_object_ref (parent_window) : NULL;

  alert = adw_alert_dialog_new (NULL, NULL);
  adw_alert_dialog_format_heading (ADW_ALERT_DIALOG (alert), _ ("Add Icon to Metainfo Preview?"));
  adw_alert_dialog_format_body (
      ADW_ALERT_DIALOG (alert),
      _ ("Metainfo files don't include app icons by themselves. Would you like to select one manually?"));
  adw_alert_dialog_add_responses (
      ADW_ALERT_DIALOG (alert),
      "skip", _ ("Skip"),
      "select", _ ("Select Icon"),
      NULL);
  adw_alert_dialog_set_response_appearance (
      ADW_ALERT_DIALOG (alert), "select", ADW_RESPONSE_SUGGESTED);
  adw_alert_dialog_set_default_response (ADW_ALERT_DIALOG (alert), "select");
  adw_alert_dialog_set_close_response (ADW_ALERT_DIALOG (alert), "skip");

  adw_dialog_present (
      alert, parent_window != NULL ? GTK_WIDGET (parent_window) : NULL);

  alert_future = bz_make_alert_dialog_future (ADW_ALERT_DIALOG (alert));
  alert_future = dex_future_then (
      g_steal_pointer (&alert_future),
      (DexFutureCallback) on_icon_prompt_finally,
      data,
      (GDestroyNotify) open_metainfo_data_unref);
  dex_future_disown (g_steal_pointer (&alert_future));

  return DEX_FUTURE (g_steal_pointer (&promise));
}

static BzMetainfoPickResult *
bz_metainfo_pick_result_copy (BzMetainfoPickResult *result)
{
  BzMetainfoPickResult *copy = NULL;

  copy                = g_new0 (BzMetainfoPickResult, 1);
  copy->metainfo_file = result->metainfo_file ? g_object_ref (result->metainfo_file) : NULL;
  copy->icon_file     = result->icon_file ? g_object_ref (result->icon_file) : NULL;

  return copy;
}

static DexFuture *
on_icon_prompt_finally (DexFuture        *future,
                        OpenMetainfoData *data)
{
  g_autoptr (GError) local_error = NULL;
  const GValue *value            = NULL;
  const char   *response         = NULL;

  value    = dex_future_get_value (future, &local_error);
  response = value != NULL ? g_value_get_string (value) : NULL;

  if (g_strcmp0 (response, "select") == 0)
    {
      g_autoptr (GtkFileDialog) icon_dialog = NULL;
      g_autoptr (GtkFileFilter) filter      = NULL;
      g_autoptr (GListStore) filters        = NULL;

      icon_dialog = gtk_file_dialog_new ();
      gtk_file_dialog_set_title (icon_dialog, _ ("Select Icon"));

      filter = gtk_file_filter_new ();
      gtk_file_filter_set_name (filter, _ ("Image Files"));
      gtk_file_filter_add_pattern (filter, "*.png");
      gtk_file_filter_add_pattern (filter, "*.svg");
      gtk_file_filter_add_pattern (filter, "*.jpg");
      gtk_file_filter_add_pattern (filter, "*.jpeg");

      filters = g_list_store_new (GTK_TYPE_FILE_FILTER);
      g_list_store_append (filters, filter);
      gtk_file_dialog_set_filters (icon_dialog, G_LIST_MODEL (filters));

      gtk_file_dialog_open (
          icon_dialog,
          data->parent_window,
          NULL,
          (GAsyncReadyCallback) on_icon_chosen,
          open_metainfo_data_ref (data));
    }
  else
    {
      BzMetainfoPickResult *pick = NULL;

      pick                = g_new0 (BzMetainfoPickResult, 1);
      pick->metainfo_file = g_object_ref (data->metainfo_file);
      pick->icon_file     = NULL;

      dex_promise_resolve_boxed (
          data->promise,
          bz_metainfo_pick_result_get_type (),
          g_steal_pointer (&pick));
    }

  return dex_future_new_true ();
}

static void
on_icon_chosen (GtkFileDialog    *dialog,
                GAsyncResult     *result,
                OpenMetainfoData *data)
{
  g_autoptr (OpenMetainfoData) owned_data = data;
  g_autoptr (GFile) icon_file             = NULL;
  BzMetainfoPickResult *pick              = NULL;

  icon_file = gtk_file_dialog_open_finish (dialog, result, NULL);

  pick                = g_new0 (BzMetainfoPickResult, 1);
  pick->metainfo_file = g_object_ref (owned_data->metainfo_file);
  pick->icon_file     = g_steal_pointer (&icon_file);

  dex_promise_resolve_boxed (
      owned_data->promise,
      bz_metainfo_pick_result_get_type (),
      g_steal_pointer (&pick));
}

static void
append_quality_apps (BzFlathubState *flathub,
                     const char     *category_name,
                     guint           count,
                     GListStore     *destination)
{
  GListModel *categories   = NULL;
  guint       n_categories = 0;

  if (flathub == NULL)
    return;

  categories   = bz_flathub_state_get_categories (flathub);
  n_categories = categories != NULL ? g_list_model_get_n_items (categories) : 0;

  for (guint i = 0; i < n_categories; i++)
    {
      g_autoptr (BzFlathubCategory) category = NULL;

      category = g_list_model_get_item (categories, i);
      if (g_strcmp0 (bz_flathub_category_get_name (category), category_name) == 0)
        {
          g_autoptr (GListModel) quality_apps = NULL;
          guint n_quality_apps                = 0;
          guint n_pick                        = 0;
          guint offset                        = 0;

          quality_apps   = bz_flathub_category_dup_quality_applications (category);
          n_quality_apps = quality_apps != NULL ? g_list_model_get_n_items (quality_apps) : 0;

          n_pick = MIN (count, n_quality_apps);
          if (n_quality_apps > 0)
            offset = g_random_int_range (0, n_quality_apps);

          for (guint j = 0; j < n_pick; j++)
            {
              g_autoptr (GObject) picked_obj = NULL;
              guint index                    = (offset + j) % n_quality_apps;

              picked_obj = g_list_model_get_item (quality_apps, index);
              if (BZ_IS_ENTRY_GROUP (picked_obj))
                g_list_store_append (destination, BZ_ENTRY_GROUP (picked_obj));
            }
          break;
        }
    }
}

AdwNavigationPage *
create_entry_group_preview_page (BzEntryGroup *group)
{
  g_autoptr (GtkBuilder) builder            = NULL;
  AdwNavigationPage  *page                  = NULL;
  BzFeaturedCarousel *carousel              = NULL;
  GtkWidget          *section_list          = NULL;
  GtkWidget          *rich_section_list     = NULL;
  g_autoptr (GListStore) store              = NULL;
  g_autoptr (GListStore) section_store      = NULL;
  g_autoptr (GListStore) rich_section_store = NULL;
  BzStateInfo    *state                     = NULL;
  BzFlathubState *flathub                   = NULL;

  builder = gtk_builder_new_from_resource (
      "/io/github/kolunmi/Bazaar/bz-metainfo-preview-page.ui");

  page              = ADW_NAVIGATION_PAGE (gtk_builder_get_object (builder, "page"));
  carousel          = BZ_FEATURED_CAROUSEL (gtk_builder_get_object (builder, "carousel"));
  section_list      = GTK_WIDGET (gtk_builder_get_object (builder, "section_list"));
  rich_section_list = GTK_WIDGET (gtk_builder_get_object (builder, "rich_section_list"));

  state   = bz_state_info_get_default ();
  flathub = bz_state_info_get_flathub (state);

  store = g_list_store_new (BZ_TYPE_ENTRY_GROUP);
  g_list_store_append (store, group);
  append_quality_apps (flathub, "utility", 4, store);

  bz_featured_carousel_set_model (carousel, G_LIST_MODEL (store));

  section_store = g_list_store_new (BZ_TYPE_ENTRY_GROUP);
  append_quality_apps (flathub, "utility", 11, section_store);
  g_list_store_insert (
      section_store,
      MIN (1, g_list_model_get_n_items (G_LIST_MODEL (section_store))),
      group);
  g_object_set (section_list, "model", G_LIST_MODEL (section_store), NULL);

  rich_section_store = g_list_store_new (BZ_TYPE_ENTRY_GROUP);
  append_quality_apps (flathub, "utility", 5, rich_section_store);
  g_list_store_insert (
      rich_section_store,
      MIN (1, g_list_model_get_n_items (G_LIST_MODEL (rich_section_store))),
      group);
  g_object_set (rich_section_list, "model", G_LIST_MODEL (rich_section_store), NULL);

  g_object_ref (page);
  return page;
}
