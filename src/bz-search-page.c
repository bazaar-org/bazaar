/* bz-search-page.c
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

#include "bz-apps-page.h"
#include "bz-async-texture.h"
#include "bz-category-tile.h"
#include "bz-dynamic-list-view.h"
#include "bz-entry-inspector.h"
#include "bz-finished-search-query.h"
#include "bz-group-tile-css-watcher.h"
#include "bz-rich-app-tile.h"
#include "bz-screenshot.h"
#include "bz-search-filter-popover.h"
#include "bz-search-bar.h"
#include "bz-search-page.h"
#include "bz-search-pill-list.h"
#include "bz-search-result.h"
#include "bz-template-callbacks.h"
#include "bz-util.h"

struct _BzSearchPage
{
  AdwBin parent_instance;

  BzStateInfo           *state;
  BzEntryGroup          *selected;
  gboolean               remove;
  gboolean               search_in_progress;
  BzFinishedSearchQuery *current_query;

  BzContentProvider *blocklists_provider;
  BzContentProvider *txt_blocklists_provider;
  GListStore        *search_model;
  GtkSelectionModel *selection_model;
  guint              search_update_timeout;
  DexFuture         *search_query;

  /* Template widgets */
  BzSearchBar           *search_bar;
  GtkBox                *content_box;
  GtkStack              *search_stack;
  GtkGridView           *grid_view;
  GtkWidget             *filter_button;
  BzSearchFilterPopover *filter_popover;
  GtkCustomFilter       *categories_filter;
};

G_DEFINE_FINAL_TYPE (BzSearchPage, bz_search_page, ADW_TYPE_BIN)

enum
{
  PROP_0,

  PROP_STATE,
  PROP_TEXT,
  PROP_CURRENT_QUERY,

  LAST_PROP
};

static GParamSpec *props[LAST_PROP] = { 0 };

static void
grid_activate (GtkGridView  *grid_view,
               guint         position,
               BzSearchPage *self);

static void
invalidating_state_prop_changed (BzSearchPage *self,
                                 GParamSpec   *pspec,
                                 BzStateInfo  *info);

static void
blocklists_items_changed (BzSearchPage *self,
                          guint         position,
                          guint         removed,
                          guint         added,
                          GListModel   *model);

static DexFuture *
search_query_then (DexFuture *future,
                   GWeakRef  *wr);

static void
update_filter (BzSearchPage *self);

static void
emit_idx (BzSearchPage *self,
          GListModel   *model,
          guint         selected_idx);

static gboolean
bz_search_page_grab_focus (GtkWidget *widget)
{
  BzSearchPage *self = BZ_SEARCH_PAGE (widget);

  return gtk_widget_grab_focus (GTK_WIDGET (self->search_bar));
}

static void
bz_search_page_dispose (GObject *object)
{
  BzSearchPage *self = BZ_SEARCH_PAGE (object);

  if (self->state != NULL)
    g_signal_handlers_disconnect_by_func (self->state, invalidating_state_prop_changed, self);
  if (self->blocklists_provider != NULL)
    g_signal_handlers_disconnect_by_func (self->blocklists_provider, blocklists_items_changed, self);
  if (self->txt_blocklists_provider != NULL)
    g_signal_handlers_disconnect_by_func (self->txt_blocklists_provider, blocklists_items_changed, self);

  g_clear_handle_id (&self->search_update_timeout, g_source_remove);
  dex_clear (&self->search_query);

  g_clear_object (&self->state);
  g_clear_object (&self->selected);
  g_clear_object (&self->current_query);
  g_clear_object (&self->blocklists_provider);
  g_clear_object (&self->txt_blocklists_provider);
  g_clear_object (&self->search_model);
  g_clear_object (&self->selection_model);

  G_OBJECT_CLASS (bz_search_page_parent_class)->dispose (object);
}

static void
bz_search_page_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
  BzSearchPage *self = BZ_SEARCH_PAGE (object);

  switch (prop_id)
    {
    case PROP_STATE:
      g_value_set_object (value, bz_search_page_get_state (self));
      break;
    case PROP_TEXT:
      g_value_set_string (value, bz_search_page_get_text (self));
      break;
    case PROP_CURRENT_QUERY:
      g_value_set_object (value, self->current_query);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_search_page_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
  BzSearchPage *self = BZ_SEARCH_PAGE (object);

  switch (prop_id)
    {
    case PROP_STATE:
      bz_search_page_set_state (self, g_value_get_object (value));
      break;
    case PROP_TEXT:
      bz_search_page_set_text (self, g_value_get_string (value));
      break;
    case PROP_CURRENT_QUERY:
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
is_zero (gpointer object,
         int      value)
{
  return value == 0;
}

static gboolean
is_null (gpointer object,
         GObject *value)
{
  return value == NULL;
}

static gboolean
is_empty (gpointer    object,
          GListModel *model)
{
  if (model == NULL)
    return TRUE;
  return g_list_model_get_n_items (model) == 0;
}

static gboolean
is_valid_string (gpointer    object,
                 const char *value)
{
  return value != NULL && *value != '\0';
}

static char *
idx_to_string (gpointer object,
               guint    value)
{
  return g_strdup_printf ("%d", value + 1);
}

static char *
score_to_string (gpointer object,
                 double   value)
{
  return g_strdup_printf ("%0.1f", value);
}

static char *
no_results_found_subtitle (gpointer    object,
                           const char *search_text)
{
  if (search_text == NULL || *search_text == '\0')
    return g_strdup ("");

  return g_strdup_printf (_ ("No results found for \"%s\" in Flathub"), search_text);
}

static void
has_active_filters_cb (BzSearchFilterPopover *filter_popover,
                       GParamSpec            *pspec,
                       BzSearchPage          *self)
{
  gboolean active = FALSE;

  g_object_get (filter_popover, "has-active-filters", &active, NULL);

  if (active)
    gtk_widget_add_css_class (self->filter_button, "accent");
  else
    gtk_widget_remove_css_class (self->filter_button, "accent");
}

static void
pill_list_cb (BzSearchPage *self,
              const char   *label,
              GtkWidget    *pill_list)
{
  bz_search_page_set_text (self, label);
  update_filter (self);
}

static void
category_clicked (BzFlathubCategory *category,
                  GtkButton         *button)
{
  GtkWidget         *self      = NULL;
  GtkWidget         *nav_view  = NULL;
  AdwNavigationPage *apps_page = NULL;

  self = gtk_widget_get_ancestor (GTK_WIDGET (button), BZ_TYPE_SEARCH_PAGE);
  g_assert (self != NULL);

  nav_view = gtk_widget_get_ancestor (GTK_WIDGET (self), ADW_TYPE_NAVIGATION_VIEW);
  g_assert (nav_view != NULL);

  apps_page = bz_apps_page_new_from_category (category);

  adw_navigation_view_push (ADW_NAVIGATION_VIEW (nav_view), apps_page);
}

static void
bind_category_tile_cb (BzSearchPage      *self,
                       BzCategoryTile    *tile,
                       BzFlathubCategory *category,
                       BzDynamicListView *view)
{
  g_signal_connect_swapped (tile, "clicked", G_CALLBACK (category_clicked), category);
}

static void
search_changed (BzSearchPage *self,
                GtkEditable  *editable)
{
  g_clear_handle_id (&self->search_update_timeout, g_source_remove);
  self->search_update_timeout = g_timeout_add_once (
      150, (GSourceOnceFunc) update_filter, self);
  bz_search_bar_set_busy (BZ_SEARCH_BAR (editable), TRUE);
}

static void
search_activate (BzSearchPage *self,
                 BzSearchBar  *search_bar)
{
  GtkSelectionModel *model          = NULL;
  guint              n_items        = 0;

  model   = gtk_grid_view_get_model (self->grid_view);
  n_items = g_list_model_get_n_items (G_LIST_MODEL (model));

  bz_search_bar_set_busy (self->search_bar, FALSE);

  if (n_items > 0)
    {
      GtkWidget *cell = NULL;
      GtkWidget *box  = NULL;
      GtkWidget *tile = NULL;

      gtk_widget_activate_action (GTK_WIDGET (self->grid_view), "list.scroll-to-item", "u", 0);

      cell = gtk_widget_get_first_child (GTK_WIDGET (self->grid_view));
      if (cell != NULL)
        box = gtk_widget_get_first_child (cell);
      if (box != NULL)
        tile = gtk_widget_get_first_child (box);

      if (BZ_IS_RICH_APP_TILE (tile))
        bz_rich_app_tile_focus_action_button (BZ_RICH_APP_TILE (tile));
    }
}

static void
unbind_category_tile_cb (BzSearchPage      *self,
                         BzCategoryTile    *tile,
                         BzFlathubCategory *category,
                         BzDynamicListView *view)
{
  g_signal_handlers_disconnect_by_func (tile, category_clicked, category);
}

static void
reset_search_cb (BzSearchPage *self,
                 GtkButton    *button)
{
  bz_search_page_set_text (self, "");
  bz_search_filter_popover_clear (self->filter_popover);
  bz_search_page_refresh (self);
}

static void
copy_id_cb (GtkListItem *list_item,
            GtkButton   *button)
{
  BzSearchResult *result    = NULL;
  BzEntryGroup   *group     = NULL;
  const char     *id        = NULL;
  GdkClipboard   *clipboard = NULL;

  result = gtk_list_item_get_item (list_item);
  group  = bz_search_result_get_group (result);
  id     = bz_entry_group_get_id (group);

  clipboard = gdk_display_get_clipboard (gdk_display_get_default ());
  gdk_clipboard_set_text (clipboard, id);
}

static void
debug_id_inspect_cb (GtkListItem *list_item,
                     GtkButton   *button)
{
  BzSearchResult  *search_result = NULL;
  BzStateInfo     *state         = NULL;
  BzEntryGroup    *group         = NULL;
  g_autofree char *unique_id     = NULL;
  g_autoptr (BzResult) result    = NULL;

  search_result = gtk_list_item_get_item (list_item);
  state         = bz_search_result_get_state (search_result);
  if (state == NULL)
    return;

  group     = bz_search_result_get_group (search_result);
  unique_id = bz_entry_group_dup_ui_entry_id (group);
  result    = bz_application_map_factory_convert_one (
      bz_state_info_get_entry_factory (state),
      gtk_string_object_new (unique_id));
  if (result != NULL)
    {
      BzEntryInspector *inspector = NULL;

      inspector = bz_entry_inspector_new ();
      bz_entry_inspector_set_result (inspector, result);

      gtk_window_present (GTK_WINDOW (inspector));
    }
}

static void
bz_search_page_class_init (BzSearchPageClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose      = bz_search_page_dispose;
  object_class->get_property = bz_search_page_get_property;
  object_class->set_property = bz_search_page_set_property;

  widget_class->grab_focus = bz_search_page_grab_focus;

  props[PROP_STATE] =
      g_param_spec_object (
          "state",
          NULL, NULL,
          BZ_TYPE_STATE_INFO,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_TEXT] =
      g_param_spec_string (
          "text",
          NULL, NULL, NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_CURRENT_QUERY] =
      g_param_spec_object (
          "current-query",
          NULL, NULL,
          BZ_TYPE_FINISHED_SEARCH_QUERY,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  g_type_ensure (BZ_TYPE_ASYNC_TEXTURE);
  g_type_ensure (BZ_TYPE_CATEGORY_TILE);
  g_type_ensure (BZ_TYPE_DYNAMIC_LIST_VIEW);
  g_type_ensure (BZ_TYPE_GROUP_TILE_CSS_WATCHER);
  g_type_ensure (BZ_TYPE_RICH_APP_TILE);
  g_type_ensure (BZ_TYPE_SCREENSHOT);
  g_type_ensure (BZ_TYPE_SEARCH_RESULT);
  g_type_ensure (BZ_TYPE_SEARCH_PILL_LIST);
  g_type_ensure (BZ_TYPE_SEARCH_FILTER_POPOVER);

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/kolunmi/Bazaar/bz-search-page.ui");
  bz_widget_class_bind_all_util_callbacks (widget_class);

  gtk_widget_class_bind_template_child (widget_class, BzSearchPage, search_bar);
  gtk_widget_class_bind_template_child (widget_class, BzSearchPage, content_box);
  gtk_widget_class_bind_template_child (widget_class, BzSearchPage, search_stack);
  gtk_widget_class_bind_template_child (widget_class, BzSearchPage, grid_view);
  gtk_widget_class_bind_template_child (widget_class, BzSearchPage, filter_button);
  gtk_widget_class_bind_template_child (widget_class, BzSearchPage, filter_popover);
  gtk_widget_class_bind_template_child (widget_class, BzSearchPage, categories_filter);
  gtk_widget_class_bind_template_callback (widget_class, bind_category_tile_cb);
  gtk_widget_class_bind_template_callback (widget_class, unbind_category_tile_cb);
  gtk_widget_class_bind_template_callback (widget_class, invert_boolean);
  gtk_widget_class_bind_template_callback (widget_class, is_zero);
  gtk_widget_class_bind_template_callback (widget_class, is_null);
  gtk_widget_class_bind_template_callback (widget_class, is_empty);
  gtk_widget_class_bind_template_callback (widget_class, has_active_filters_cb);
  gtk_widget_class_bind_template_callback (widget_class, is_valid_string);
  gtk_widget_class_bind_template_callback (widget_class, idx_to_string);
  gtk_widget_class_bind_template_callback (widget_class, score_to_string);
  gtk_widget_class_bind_template_callback (widget_class, reset_search_cb);
  gtk_widget_class_bind_template_callback (widget_class, pill_list_cb);
  gtk_widget_class_bind_template_callback (widget_class, no_results_found_subtitle);
  gtk_widget_class_bind_template_callback (widget_class, copy_id_cb);
  gtk_widget_class_bind_template_callback (widget_class, debug_id_inspect_cb);
  gtk_widget_class_bind_template_callback (widget_class, search_activate);
  gtk_widget_class_bind_template_callback (widget_class, search_changed);
}

static void
bz_search_page_init (BzSearchPage *self)
{
  self->search_model = g_list_store_new (BZ_TYPE_SEARCH_RESULT);

  gtk_widget_init_template (GTK_WIDGET (self));

  /* TODO: move all this to blueprint */

  self->selection_model = GTK_SELECTION_MODEL (gtk_no_selection_new (NULL));
  gtk_no_selection_set_model (GTK_NO_SELECTION (self->selection_model), G_LIST_MODEL (self->search_model));
  gtk_grid_view_set_model (self->grid_view, self->selection_model);

  g_signal_connect (self->grid_view, "activate", G_CALLBACK (grid_activate), self);

  g_signal_connect_swapped (self->filter_popover, "notify::selected-categories",
                            G_CALLBACK (update_filter), self);
  g_signal_connect_swapped (self->filter_popover, "notify::only-verified",
                            G_CALLBACK (update_filter), self);
  g_signal_connect_swapped (self->filter_popover, "notify::only-free",
                            G_CALLBACK (update_filter), self);
  g_signal_connect_swapped (self->filter_popover, "notify::only-non-eol",
                            G_CALLBACK (update_filter), self);
  g_signal_connect_swapped (self->filter_popover, "notify::only-mobile",
                            G_CALLBACK (update_filter), self);

  gtk_custom_filter_set_filter_func (
    self->categories_filter,
    (GtkCustomFilterFunc) bz_flathub_category_get_show_in_list,
    NULL, NULL);
}

GtkWidget *
bz_search_page_new (GListModel *model,
                    const char *initial)
{
  BzSearchPage *self = NULL;

  self = g_object_new (
      BZ_TYPE_SEARCH_PAGE,
      "model", model,
      NULL);

  if (initial != NULL)
    gtk_editable_set_text (GTK_EDITABLE (self->search_bar), initial);

  return GTK_WIDGET (self);
}

BzEntryGroup *
bz_search_page_get_selected (BzSearchPage *self,
                             gboolean     *remove)
{
  g_return_val_if_fail (BZ_IS_SEARCH_PAGE (self), NULL);

  if (remove != NULL)
    *remove = self->remove;
  return self->selected;
}

void
bz_search_page_set_state (BzSearchPage *self,
                          BzStateInfo  *state)
{
  g_return_if_fail (BZ_IS_SEARCH_PAGE (self));

  if (self->state != NULL)
    g_signal_handlers_disconnect_by_func (self->state, invalidating_state_prop_changed, self);
  g_clear_object (&self->state);

  if (self->blocklists_provider != NULL)
    g_signal_handlers_disconnect_by_func (self->blocklists_provider, blocklists_items_changed, self);
  g_clear_object (&self->blocklists_provider);

  if (self->txt_blocklists_provider != NULL)
    g_signal_handlers_disconnect_by_func (self->txt_blocklists_provider, blocklists_items_changed, self);
  g_clear_object (&self->txt_blocklists_provider);

  if (state != NULL)
    {
      self->state = g_object_ref (state);
      g_signal_connect_swapped (
          state,
          "notify::disable-blocklists",
          G_CALLBACK (invalidating_state_prop_changed),
          self);
      g_signal_connect_swapped (
          state,
          "notify::hide-eol",
          G_CALLBACK (invalidating_state_prop_changed),
          self);
      g_signal_connect_swapped (
          state,
          "notify::show-only-foss",
          G_CALLBACK (invalidating_state_prop_changed),
          self);
      g_signal_connect_swapped (
          state,
          "notify::show-only-flathub",
          G_CALLBACK (invalidating_state_prop_changed),
          self);
      g_signal_connect_swapped (
          state,
          "notify::show-only-verified",
          G_CALLBACK (invalidating_state_prop_changed),
          self);

      g_signal_connect_swapped (
          state,
          "notify::parental-age-rating",
          G_CALLBACK (invalidating_state_prop_changed),
          self);

      g_object_get (
          state,
          "blocklists-provider", &self->blocklists_provider,
          "txt-blocklists-provider", &self->txt_blocklists_provider,
          NULL);
      if (self->blocklists_provider != NULL)
        g_signal_connect_data (
            self->blocklists_provider,
            "items-changed",
            G_CALLBACK (blocklists_items_changed),
            self, NULL,
            G_CONNECT_SWAPPED | G_CONNECT_AFTER);
      if (self->txt_blocklists_provider != NULL)
        g_signal_connect_data (
            self->txt_blocklists_provider,
            "items-changed",
            G_CALLBACK (blocklists_items_changed),
            self, NULL,
            G_CONNECT_SWAPPED | G_CONNECT_AFTER);
    }

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_STATE]);
}

BzStateInfo *
bz_search_page_get_state (BzSearchPage *self)
{
  g_return_val_if_fail (BZ_IS_SEARCH_PAGE (self), NULL);
  return self->state;
}

void
bz_search_page_set_text (BzSearchPage *self,
                         const char   *text)
{
  g_return_if_fail (BZ_IS_SEARCH_PAGE (self));

  gtk_editable_set_text (GTK_EDITABLE (self->search_bar), text);
  if (text != NULL)
    gtk_editable_set_position (GTK_EDITABLE (self->search_bar), g_utf8_strlen (text, -1));

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_TEXT]);
}

const char *
bz_search_page_get_text (BzSearchPage *self)
{
  g_return_val_if_fail (BZ_IS_SEARCH_PAGE (self), NULL);
  return gtk_editable_get_text (GTK_EDITABLE (self->search_bar));
}

void
bz_search_page_refresh (BzSearchPage *self)
{
  g_return_if_fail (BZ_IS_SEARCH_PAGE (self));
  update_filter (self);
}

gboolean
bz_search_page_ensure_active (BzSearchPage *self,
                              const char   *initial)
{
  g_return_val_if_fail (BZ_IS_SEARCH_PAGE (self), FALSE);

  bz_search_page_set_text (self, initial);

  return gtk_widget_grab_focus (GTK_WIDGET (self));
}

static void
grid_activate (GtkGridView  *grid_view,
               guint         position,
               BzSearchPage *self)
{
  GtkSelectionModel *model = NULL;

  model = gtk_grid_view_get_model (self->grid_view);
  emit_idx (self, G_LIST_MODEL (model), position);
}

static void
invalidating_state_prop_changed (BzSearchPage *self,
                                 GParamSpec   *pspec,
                                 BzStateInfo  *info)
{
  update_filter (self);
}

static void
blocklists_items_changed (BzSearchPage *self,
                          guint         position,
                          guint         removed,
                          guint         added,
                          GListModel   *model)
{
  update_filter (self);
}

static DexFuture *
search_query_then (DexFuture *future,
                   GWeakRef  *wr)
{
  g_autoptr (BzSearchPage) self        = NULL;
  g_autoptr (GPtrArray) filtered       = NULL;
  BzFinishedSearchQuery *finished      = NULL;
  GPtrArray             *results       = NULL;
  guint                  old_length    = 0;
  const char            *page_name     = NULL;
  BzCategoryFlags        categories    = BZ_CATEGORY_FLAGS_NONE;
  gboolean               only_verified = FALSE;
  gboolean               only_free     = FALSE;
  gboolean               only_non_eol  = FALSE;
  gboolean               only_mobile   = FALSE;
  const char *search_text              = NULL;

  bz_weak_get_or_return_reject (self, wr);

  finished      = g_value_get_object (dex_future_get_value (future, NULL));
  results       = bz_finished_search_query_get_results (finished);
  categories    = bz_search_filter_popover_get_selected_categories (self->filter_popover);
  only_verified = bz_search_filter_popover_get_only_verified (self->filter_popover);
  only_free     = bz_search_filter_popover_get_only_free (self->filter_popover);
  only_non_eol  = bz_search_filter_popover_get_only_non_eol (self->filter_popover);
  only_mobile   = bz_search_filter_popover_get_only_mobile (self->filter_popover);

  filtered = g_ptr_array_new_with_free_func (g_object_unref);
  search_text = gtk_editable_get_text (GTK_EDITABLE (self->search_bar));

  for (guint i = 0; i < results->len; i++)
    {
      BzSearchResult *result = g_ptr_array_index (results, i);
      BzEntryGroup   *group  = bz_search_result_get_group (result);

      if (self->state != NULL)
        /* This is for debug mode */
        bz_search_result_set_state (result, self->state);

      if (categories != BZ_CATEGORY_FLAGS_NONE &&
          !(bz_entry_group_get_categories (group) & categories))
        continue;

      if (only_verified && !bz_entry_group_get_is_verified (group))
        continue;

      if (only_free && !bz_entry_group_get_is_floss (group))
        continue;

      if (only_non_eol && bz_entry_group_get_eol (group))
        continue;

      if (only_mobile && !bz_entry_group_get_is_mobile_friendly (group))
        continue;

      g_ptr_array_add (filtered, g_object_ref (result));
    }

  old_length = g_list_model_get_n_items (G_LIST_MODEL (self->search_model));
  g_list_store_splice (
      self->search_model,
      0, old_length,
      (gpointer *) filtered->pdata, filtered->len);
  bz_search_bar_set_busy (self->search_bar, FALSE);

  if (filtered->len > 0)
    {
      page_name = "results";
      gtk_widget_activate_action (GTK_WIDGET (self->grid_view), "list.scroll-to-item", "u", 0);
    }
  else
    {
      page_name = (search_text && *search_text) ? "no-results" : "empty";
    }

  if (search_text && *search_text) {
    const char *message = NULL;

    message = filtered->len == 0 ? _("No applications found") : g_strdup_printf (ngettext ("One application found",
                                                                                "%u applications found",
                                                                                filtered->len),
                                                                            filtered->len);

    gtk_accessible_announce (GTK_ACCESSIBLE (self), message, GTK_ACCESSIBLE_ANNOUNCEMENT_PRIORITY_MEDIUM);
  }

  self->current_query = g_object_ref (finished);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_CURRENT_QUERY]);

  gtk_stack_set_visible_child_name (self->search_stack, page_name);

  dex_clear (&self->search_query);
  return NULL;
}

static void
update_filter (BzSearchPage *self)
{
  BzSearchEngine *engine           = NULL;
  const char     *search_text      = NULL;
  g_autoptr (GStrvBuilder) builder = NULL;
  guint n_terms                    = 0;
  g_auto (GStrv) terms             = NULL;
  g_autoptr (DexFuture) future     = NULL;
  g_autofree gchar **tokens        = NULL;

  g_clear_handle_id (&self->search_update_timeout, g_source_remove);
  dex_clear (&self->search_query);

  g_clear_object (&self->current_query);
  self->current_query = bz_finished_search_query_new ();
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_CURRENT_QUERY]);

  bz_search_bar_set_busy (self->search_bar, FALSE);

  if (self->state == NULL)
    return;
  engine = bz_state_info_get_search_engine (self->state);
  if (engine == NULL)
    return;

  search_text = gtk_editable_get_text (GTK_EDITABLE (self->search_bar));

  if (search_text == NULL || *search_text == '\0')
    {
      g_list_store_remove_all (self->search_model);
      gtk_stack_set_visible_child_name (self->search_stack, "empty");
      return;
    }

  builder = g_strv_builder_new ();

  tokens = g_strsplit_set (search_text, " \t\n", -1);
  for (gchar **token = tokens; *token != NULL; token++)
    {
      if (**token != '\0')
        {
          g_strv_builder_take (builder, *token);
          n_terms++;
        }
      else
        g_free (*token);
    }

  if (n_terms == 0)
    {
      g_list_store_remove_all (self->search_model);
      gtk_stack_set_visible_child_name (self->search_stack, "empty");
      return;
    }

  terms = g_strv_builder_end (builder);

  self->search_in_progress = TRUE;

  future = bz_search_engine_query (
      engine,
      (const char *const *) terms);

  bz_search_bar_set_busy (self->search_bar, dex_future_is_pending (future));

  future = dex_future_then (
      future,
      (DexFutureCallback) search_query_then,
      bz_track_weak (self), bz_weak_release);
  self->search_query = g_steal_pointer (&future);
}

static void
emit_idx (BzSearchPage *self,
          GListModel   *model,
          guint         selected_idx)
{
  g_autoptr (BzSearchResult) result = NULL;
  BzEntryGroup *group               = NULL;

  result = g_list_model_get_item (G_LIST_MODEL (model), selected_idx);
  group  = bz_search_result_get_group (result);

  gtk_widget_activate_action (GTK_WIDGET (self), "window.show-group", "s",
                              bz_entry_group_get_id (group));
}
