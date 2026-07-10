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

#include "bz-entry-group.h"
#include "bz-featured-carousel.h"
#include "bz-metainfo-preview.h"
#include "bz-rich-app-tile.h"
#include "util.h"

BZ_DEFINE_DATA (
    pick_files,
    PickFiles,
    {
      DexPromise *promise;
      GFile      *metainfo_file;
    },
    BZ_RELEASE_DATA (promise, dex_unref);
    BZ_RELEASE_DATA (metainfo_file, g_object_unref))

static void
on_icon_chosen (GtkFileDialog *dialog,
                GAsyncResult  *result,
                PickFilesData *data);

static void
on_metainfo_chosen (GtkFileDialog *dialog,
                    GAsyncResult  *result,
                    DexPromise    *promise);

static BzMetainfoPickResult *
bz_metainfo_pick_result_copy (BzMetainfoPickResult *result);

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
bz_metainfo_preview_pick_files (void)
{
  g_autoptr (DexPromise) promise   = dex_promise_new ();
  g_autoptr (GtkFileDialog) dialog = NULL;
  g_autoptr (GtkFileFilter) filter = NULL;
  g_autoptr (GListStore) filters   = NULL;

  dialog = gtk_file_dialog_new ();
  gtk_file_dialog_set_title (dialog, _ ("Select Metainfo File"));

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _ ("Metainfo Files"));
  gtk_file_filter_add_pattern (filter, "*.metainfo.xml*");
  gtk_file_filter_add_pattern (filter, "*.appdata.xml*");

  filters = g_list_store_new (GTK_TYPE_FILE_FILTER);
  g_list_store_append (filters, filter);
  gtk_file_dialog_set_filters (dialog, G_LIST_MODEL (filters));

  gtk_file_dialog_open (
      dialog, NULL, NULL,
      (GAsyncReadyCallback) on_metainfo_chosen,
      dex_ref (promise));

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

static void
on_metainfo_chosen (GtkFileDialog *dialog,
                    GAsyncResult  *result,
                    DexPromise    *promise)
{
  g_autoptr (DexPromise) owned_promise  = promise;
  g_autoptr (GError) local_error        = NULL;
  g_autoptr (GFile) metainfo_file       = NULL;
  g_autoptr (GtkFileDialog) icon_dialog = NULL;
  g_autoptr (GtkFileFilter) filter      = NULL;
  g_autoptr (GListStore) filters        = NULL;
  PickFilesData *data                   = NULL;

  metainfo_file = gtk_file_dialog_open_finish (dialog, result, &local_error);

  if (metainfo_file == NULL)
    {
      dex_promise_reject (owned_promise, g_steal_pointer (&local_error));
      return;
    }

  data                = pick_files_data_new ();
  data->promise       = g_steal_pointer (&owned_promise);
  data->metainfo_file = g_steal_pointer (&metainfo_file);

  icon_dialog = gtk_file_dialog_new ();
  gtk_file_dialog_set_title (icon_dialog, _ ("Select Icon (Optional)"));

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
      icon_dialog, NULL, NULL,
      (GAsyncReadyCallback) on_icon_chosen,
      data);
}

static void
on_icon_chosen (GtkFileDialog *dialog,
                GAsyncResult  *result,
                PickFilesData *data)
{
  g_autoptr (PickFilesData) owned_data = data;
  g_autoptr (GFile) icon_file          = NULL;
  BzMetainfoPickResult *pick           = NULL;

  icon_file = gtk_file_dialog_open_finish (dialog, result, NULL);

  pick                = g_new0 (BzMetainfoPickResult, 1);
  pick->metainfo_file = g_object_ref (owned_data->metainfo_file);
  pick->icon_file     = g_steal_pointer (&icon_file);

  dex_promise_resolve_boxed (
      owned_data->promise,
      bz_metainfo_pick_result_get_type (),
      g_steal_pointer (&pick));
}

AdwNavigationPage *
create_entry_group_preview_page (BzEntryGroup *group)
{
  AdwNavigationPage  *page           = NULL;
  GtkWidget          *toolbar_view   = NULL;
  AdwHeaderBar       *header_bar     = NULL;
  GtkBox             *box            = NULL;
  BzFeaturedCarousel *carousel       = NULL;
  BzRichAppTile      *tile           = NULL;
  GtkWidget          *carousel_clamp = NULL;
  GtkWidget          *tile_clamp     = NULL;
  GtkWidget          *scroll         = NULL;
  g_autoptr (GListStore) store       = NULL;

  store = g_list_store_new (BZ_TYPE_ENTRY_GROUP);
  g_list_store_append (store, group);

  carousel = bz_featured_carousel_new ();
  bz_featured_carousel_set_model (carousel, G_LIST_MODEL (store));
  gtk_widget_set_can_target (GTK_WIDGET (carousel), FALSE);

  carousel_clamp = adw_clamp_new ();
  adw_clamp_set_maximum_size (ADW_CLAMP (carousel_clamp), 1500);
  adw_clamp_set_tightening_threshold (ADW_CLAMP (carousel_clamp), 1400);
  adw_clamp_set_child (ADW_CLAMP (carousel_clamp), GTK_WIDGET (carousel));

  tile = BZ_RICH_APP_TILE (bz_rich_app_tile_new ());
  bz_rich_app_tile_set_group (tile, group);

  tile_clamp = adw_clamp_new ();
  adw_clamp_set_maximum_size (ADW_CLAMP (tile_clamp), 350);
  adw_clamp_set_child (ADW_CLAMP (tile_clamp), GTK_WIDGET (tile));

  box = GTK_BOX (gtk_box_new (GTK_ORIENTATION_VERTICAL, 12));
  gtk_widget_set_margin_top (GTK_WIDGET (box), 24);
  gtk_widget_set_margin_bottom (GTK_WIDGET (box), 24);
  gtk_widget_set_margin_start (GTK_WIDGET (box), 24);
  gtk_widget_set_margin_end (GTK_WIDGET (box), 24);
  gtk_box_append (box, GTK_WIDGET (carousel_clamp));
  gtk_box_append (box, GTK_WIDGET (tile_clamp));

  scroll = gtk_scrolled_window_new ();
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroll), GTK_WIDGET (box));
  gtk_widget_set_vexpand (scroll, TRUE);

  header_bar   = ADW_HEADER_BAR (adw_header_bar_new ());
  toolbar_view = adw_toolbar_view_new ();
  adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (toolbar_view), GTK_WIDGET (header_bar));
  adw_toolbar_view_set_content (ADW_TOOLBAR_VIEW (toolbar_view), scroll);

  page = adw_navigation_page_new (toolbar_view, _ ("Preview"));

  return page;
}
