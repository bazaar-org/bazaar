/* bz-library-page.c
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

#include "bz-entry-group.h"
#include "bz-gnome-extension-info.h"
#include "bz-gnome-extension-tile.h"
#include "bz-installed-tile.h"
#include "bz-library-page.h"
#include "bz-section-view.h"
#include "bz-template-callbacks.h"
#include "bz-transaction-tile.h"
#include "bz-updates-card.h"
#include "bz-util.h"

struct _BzLibraryPage
{
  AdwBin parent_instance;

  GListModel           *model;
  BzTransactionManager *transactions;
  BzStateInfo          *state;

  /* Template widgets */
  AdwViewStack       *stack;
  GtkText            *search_bar;
  GtkScrolledWindow  *scroll;
  GtkFilterListModel *filter_model;
  GtkCustomFilter    *filter;
  GtkCustomFilter    *user_ext_filter;
  GtkCustomFilter    *system_ext_filter;
  GtkListView        *list_view;
  GtkSortListModel   *sort_model;
  GtkCustomSorter    *sorter;
  GtkCheckButton     *sort_name;
  GtkCheckButton     *sort_size;
};

G_DEFINE_FINAL_TYPE (BzLibraryPage, bz_library_page, ADW_TYPE_BIN)

enum
{
  PROP_0,

  PROP_MODEL,
  PROP_TRANSACTIONS,
  PROP_STATE,
  PROP_HAS_APPS,

  LAST_PROP
};
static GParamSpec *props[LAST_PROP] = { 0 };

enum
{
  SIGNAL_UPDATE,

  LAST_SIGNAL,
};
static guint signals[LAST_SIGNAL];

static void
items_changed (BzLibraryPage *self,
               guint          position,
               guint          removed,
               guint          added,
               GListModel    *model);

static void
has_transactions_changed (BzLibraryPage        *self,
                          GParamSpec           *pspec,
                          BzTransactionManager *transactions);

static void
set_page (BzLibraryPage *self);

static gboolean
set_page_idle_cb (BzLibraryPage *self);

static gboolean
filter (BzEntryGroup  *group,
        BzLibraryPage *self);

static gboolean
filter_user_extensions (BzGnomeExtensionInfo *info,
                        gpointer              user_data);

static gboolean
filter_system_extensions (BzGnomeExtensionInfo *info,
                          gpointer              user_data);

static void
bz_library_page_dispose (GObject *object)
{
  BzLibraryPage *self = BZ_LIBRARY_PAGE (object);

  if (self->model != NULL)
    g_signal_handlers_disconnect_by_func (self->model, items_changed, self);
  if (self->transactions != NULL)
    g_signal_handlers_disconnect_by_func (self->transactions, has_transactions_changed, self);

  g_clear_object (&self->model);
  g_clear_object (&self->transactions);
  g_clear_object (&self->state);

  G_OBJECT_CLASS (bz_library_page_parent_class)->dispose (object);
}

static void
bz_library_page_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  BzLibraryPage *self = BZ_LIBRARY_PAGE (object);

  switch (prop_id)
    {
    case PROP_MODEL:
      g_value_set_object (value, bz_library_page_get_model (self));
      break;
    case PROP_TRANSACTIONS:
      g_value_set_object (value, bz_library_page_get_transactions (self));
      break;
    case PROP_STATE:
      g_value_set_object (value, bz_library_page_get_state (self));
      break;
    case PROP_HAS_APPS:
      {
        guint n_apps = 0;

        if (self->model != NULL)
          n_apps = g_list_model_get_n_items (self->model);
        g_value_set_boolean (value, n_apps > 0);
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_library_page_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  BzLibraryPage *self = BZ_LIBRARY_PAGE (object);

  switch (prop_id)
    {
    case PROP_MODEL:
      bz_library_page_set_model (self, g_value_get_object (value));
      break;
    case PROP_TRANSACTIONS:
      bz_library_page_set_transactions (self, g_value_get_object (value));
      break;
    case PROP_STATE:
      bz_library_page_set_state (self, g_value_get_object (value));
      break;
    case PROP_HAS_APPS:
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static char *
no_results_found_subtitle (gpointer    object,
                           const char *search_text)
{
  if (search_text == NULL || *search_text == '\0')
    return g_strdup ("");

  return g_strdup_printf (_ ("No matches found for \"%s\" in the list of installed apps"), search_text);
}

static char *
format_update_count (gpointer    object,
                     GListModel *updates)
{
  guint n_updates = 0;

  if (updates == NULL)
    return g_strdup ("");

  n_updates = g_list_model_get_n_items (updates);
  return g_strdup_printf (ngettext ("%u Available Update",
                                    "%u Available Updates",
                                    n_updates),
                          n_updates);
}

static int
count_ext_updates (GtkWidget  *widget,
                   GListModel *infos)
{
  int   count   = 0;
  guint n_items = 0;

  if (infos == NULL)
    return 0;

  n_items = g_list_model_get_n_items (infos);
  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr (BzGnomeExtensionInfo) info = NULL;
      gboolean has_update                   = FALSE;

      info = g_list_model_get_item (infos, i);

      g_object_get (info, "has-update", &has_update, NULL);
      if (has_update)
        count++;
    }

  return count;
}

static char *
format_ext_update_count (GtkWidget *widget,
                         int        count)
{
  return g_strdup_printf (ngettext ("%d extension will be updated on next login",
                                    "%d extensions will be updated on next login",
                                    count),
                          count);
}

static char *
format_install_count (gpointer object,
                      gint     n_items)
{
  return g_strdup_printf (ngettext ("%u Installed App",
                                    "%u Installed Apps",
                                    n_items),
                          n_items);
}

static void
tile_activated_cb (BzListTile *tile)
{
  BzLibraryPage *self  = NULL;
  BzEntryGroup  *group = NULL;

  g_assert (BZ_IS_LIST_TILE (tile));

  self = (BzLibraryPage *) gtk_widget_get_ancestor (GTK_WIDGET (tile), BZ_TYPE_LIBRARY_PAGE);
  if (self == NULL)
    return;

  if (BZ_IS_INSTALLED_TILE (tile))
    {
      group = bz_installed_tile_get_group (BZ_INSTALLED_TILE (tile));
    }
  else if (BZ_IS_GNOME_EXTENSION_TILE (tile))
    {
      group = bz_gnome_extension_tile_get_group (BZ_GNOME_EXTENSION_TILE (tile));
    }
  else if (BZ_IS_TRANSACTION_TILE (tile))
    {
      BzTransactionEntryTracker *tracker = NULL;
      BzEntry                   *entry   = NULL;

      tracker = bz_transaction_tile_get_tracker (BZ_TRANSACTION_TILE (tile));
      if (tracker == NULL)
        return;

      entry = bz_transaction_entry_tracker_get_entry (tracker);
      group = bz_application_map_factory_convert_one (
          bz_state_info_get_application_factory (self->state),
          gtk_string_object_new (bz_entry_get_id (entry)));
    }
  else
    return;

  if (group == NULL)
    return;

  gtk_widget_activate_action (GTK_WIDGET (self), "window.show-group", "s",
                              bz_entry_group_get_id (group));
}

static void
search_text_changed (BzLibraryPage *self,
                     GParamSpec    *pspec,
                     GtkText       *entry)
{
  gtk_filter_changed (GTK_FILTER (self->filter),
                      GTK_FILTER_CHANGE_DIFFERENT);
  set_page (self);
}

static void
search_text_activate (BzLibraryPage *self,
                      GtkText       *entry)
{
  const char *text = NULL;

  text = gtk_editable_get_text (GTK_EDITABLE (self->search_bar));
  if (text != NULL && *text != '\0')
    gtk_widget_activate_action (GTK_WIDGET (self), "app.search", "s", text);

  gtk_editable_set_text (GTK_EDITABLE (self->search_bar), "");
}

static void
reset_search_cb (BzLibraryPage *self,
                 GtkButton     *button)
{
  gtk_text_set_buffer (self->search_bar, NULL);
}

static void
n_filtered_items_changed (BzLibraryPage      *self,
                          GParamSpec         *pspec,
                          GtkFilterListModel *model)
{
  set_page (self);
}

static void
clear_tasks_cb (BzLibraryPage *self)
{
  BzTransactionManager *manager = NULL;

  if (self->state == NULL)
    return;
  manager = bz_state_info_get_transaction_manager (self->state);

  if (manager != NULL)
    bz_transaction_manager_clear_finished (manager);
}

static void
updates_card_update_cb (BzLibraryPage *self,
                        GListModel    *entries,
                        BzUpdatesCard *card)
{
  g_signal_emit (self, signals[SIGNAL_UPDATE], 0, entries);
}

static void
global_search_cb (BzLibraryPage *self,
                  GtkButton     *button)
{
  const char *text = NULL;

  text = gtk_editable_get_text (GTK_EDITABLE (self->search_bar));
  if (text != NULL && *text != '\0')
    gtk_widget_activate_action (GTK_WIDGET (self), "app.search", "s", text);

  gtk_editable_set_text (GTK_EDITABLE (self->search_bar), "");
}

static int
sort_func (BzEntryGroup  *a,
           BzEntryGroup  *b,
           BzLibraryPage *self)
{
  if (gtk_check_button_get_active (self->sort_size))
    {
      guint64 size_a = 0;
      guint64 size_b = 0;

      size_a = bz_entry_group_get_installed_size (a);
      size_b = bz_entry_group_get_installed_size (b);

      return size_a > size_b
                 ? -1
             : size_a < size_b ? 1
                               : 0;
    }

  return g_utf8_collate (bz_entry_group_get_title (a),
                         bz_entry_group_get_title (b));
}

static void
sort_changed_cb (BzLibraryPage  *self,
                 GtkCheckButton *button)
{
  gtk_sorter_changed (GTK_SORTER (self->sorter),
                      GTK_SORTER_CHANGE_DIFFERENT);
}

static void
bz_library_page_class_init (BzLibraryPageClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose      = bz_library_page_dispose;
  object_class->get_property = bz_library_page_get_property;
  object_class->set_property = bz_library_page_set_property;

  props[PROP_MODEL] =
      g_param_spec_object (
          "model",
          NULL, NULL,
          G_TYPE_LIST_MODEL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_TRANSACTIONS] =
      g_param_spec_object (
          "transactions",
          NULL, NULL,
          BZ_TYPE_TRANSACTION_MANAGER,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_STATE] =
      g_param_spec_object (
          "state",
          NULL, NULL,
          BZ_TYPE_STATE_INFO,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_HAS_APPS] =
      g_param_spec_boolean (
          "has-apps",
          NULL, NULL, FALSE,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  signals[SIGNAL_UPDATE] =
      g_signal_new (
          "update",
          G_OBJECT_CLASS_TYPE (klass),
          G_SIGNAL_RUN_FIRST,
          0,
          NULL, NULL,
          g_cclosure_marshal_VOID__OBJECT,
          G_TYPE_NONE, 1,
          G_TYPE_LIST_MODEL);
  g_signal_set_va_marshaller (
      signals[SIGNAL_UPDATE],
      G_TYPE_FROM_CLASS (klass),
      g_cclosure_marshal_VOID__OBJECTv);

  g_type_ensure (BZ_TYPE_SECTION_VIEW);
  g_type_ensure (BZ_TYPE_ENTRY_GROUP);
  g_type_ensure (BZ_TYPE_INSTALLED_TILE);
  g_type_ensure (BZ_TYPE_GNOME_EXTENSION_TILE);
  g_type_ensure (BZ_TYPE_TRANSACTION_TILE);
  g_type_ensure (BZ_TYPE_UPDATES_CARD);

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/kolunmi/Bazaar/bz-library-page.ui");
  bz_widget_class_bind_all_util_callbacks (widget_class);

  gtk_widget_class_bind_template_child (widget_class, BzLibraryPage, stack);
  gtk_widget_class_bind_template_child (widget_class, BzLibraryPage, search_bar);
  gtk_widget_class_bind_template_child (widget_class, BzLibraryPage, scroll);
  gtk_widget_class_bind_template_child (widget_class, BzLibraryPage, filter_model);
  gtk_widget_class_bind_template_child (widget_class, BzLibraryPage, filter);
  gtk_widget_class_bind_template_child (widget_class, BzLibraryPage, user_ext_filter);
  gtk_widget_class_bind_template_child (widget_class, BzLibraryPage, system_ext_filter);
  gtk_widget_class_bind_template_child (widget_class, BzLibraryPage, list_view);
  gtk_widget_class_bind_template_child (widget_class, BzLibraryPage, sort_model);
  gtk_widget_class_bind_template_child (widget_class, BzLibraryPage, sorter);
  gtk_widget_class_bind_template_child (widget_class, BzLibraryPage, sort_name);
  gtk_widget_class_bind_template_child (widget_class, BzLibraryPage, sort_size);
  gtk_widget_class_bind_template_callback (widget_class, no_results_found_subtitle);
  gtk_widget_class_bind_template_callback (widget_class, format_update_count);
  gtk_widget_class_bind_template_callback (widget_class, format_install_count);
  gtk_widget_class_bind_template_callback (widget_class, tile_activated_cb);
  gtk_widget_class_bind_template_callback (widget_class, reset_search_cb);
  gtk_widget_class_bind_template_callback (widget_class, search_text_changed);
  gtk_widget_class_bind_template_callback (widget_class, count_ext_updates);
  gtk_widget_class_bind_template_callback (widget_class, format_ext_update_count);
  gtk_widget_class_bind_template_callback (widget_class, search_text_activate);
  gtk_widget_class_bind_template_callback (widget_class, n_filtered_items_changed);
  gtk_widget_class_bind_template_callback (widget_class, clear_tasks_cb);
  gtk_widget_class_bind_template_callback (widget_class, updates_card_update_cb);
  gtk_widget_class_bind_template_callback (widget_class, global_search_cb);
  gtk_widget_class_bind_template_callback (widget_class, sort_changed_cb);
}

static void
bz_library_page_init (BzLibraryPage *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
  gtk_custom_filter_set_filter_func (
      self->filter, (GtkCustomFilterFunc) filter,
      self, NULL);
  gtk_custom_sorter_set_sort_func (
      self->sorter, (GCompareDataFunc) sort_func,
      self, NULL);
  gtk_custom_filter_set_filter_func (
      self->user_ext_filter,
      (GtkCustomFilterFunc) filter_user_extensions,
      NULL, NULL);

  gtk_custom_filter_set_filter_func (
      self->system_ext_filter,
      (GtkCustomFilterFunc) filter_system_extensions,
      NULL, NULL);
}

GtkWidget *
bz_library_page_new (void)
{
  return g_object_new (BZ_TYPE_LIBRARY_PAGE, NULL);
}

void
bz_library_page_set_model (BzLibraryPage *self,
                           GListModel    *model)
{
  g_return_if_fail (BZ_IS_LIBRARY_PAGE (self));
  g_return_if_fail (model == NULL || G_IS_LIST_MODEL (model));

  if (self->model != NULL)
    g_signal_handlers_disconnect_by_func (self->model, items_changed, self);
  g_clear_object (&self->model);
  if (model != NULL)
    {
      self->model = g_object_ref (model);
      g_signal_connect_swapped (model, "items-changed", G_CALLBACK (items_changed), self);
    }
  g_idle_add_full (
      G_PRIORITY_DEFAULT,
      (GSourceFunc) set_page_idle_cb,
      g_object_ref (self),
      g_object_unref);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_MODEL]);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_HAS_APPS]);
}

GListModel *
bz_library_page_get_model (BzLibraryPage *self)
{
  g_return_val_if_fail (BZ_IS_LIBRARY_PAGE (self), NULL);
  return self->model;
}

void
bz_library_page_set_transactions (BzLibraryPage        *self,
                                  BzTransactionManager *transactions)
{
  g_return_if_fail (BZ_IS_LIBRARY_PAGE (self));
  g_return_if_fail (transactions == NULL || BZ_IS_TRANSACTION_MANAGER (transactions));

  if (self->transactions != NULL)
    g_signal_handlers_disconnect_by_func (self->transactions, has_transactions_changed, self);

  g_clear_object (&self->transactions);
  if (transactions != NULL)
    {
      self->transactions = g_object_ref (transactions);

      g_signal_connect_swapped (
          transactions, "notify::has-transactions",
          G_CALLBACK (has_transactions_changed), self);
    }

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_TRANSACTIONS]);
}

BzTransactionManager *
bz_library_page_get_transactions (BzLibraryPage *self)
{
  g_return_val_if_fail (BZ_IS_LIBRARY_PAGE (self), NULL);
  return self->transactions;
}

void
bz_library_page_set_state (BzLibraryPage *self,
                           BzStateInfo   *state)
{
  g_return_if_fail (BZ_IS_LIBRARY_PAGE (self));
  g_return_if_fail (state == NULL || BZ_IS_STATE_INFO (state));

  g_clear_object (&self->state);
  if (state != NULL)
    self->state = g_object_ref (state);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_STATE]);
}

BzStateInfo *
bz_library_page_get_state (BzLibraryPage *self)
{
  g_return_val_if_fail (BZ_IS_LIBRARY_PAGE (self), NULL);
  return self->state;
}

gboolean
bz_library_page_ensure_active (BzLibraryPage *self,
                               const char    *initial)
{
  const char *text = NULL;

  g_return_val_if_fail (BZ_IS_LIBRARY_PAGE (self), FALSE);

  text = gtk_editable_get_text (GTK_EDITABLE (self->search_bar));
  if (text != NULL && *text != '\0' &&
      gtk_widget_has_focus (GTK_WIDGET (self->search_bar)))
    return FALSE;

  gtk_widget_grab_focus (GTK_WIDGET (self->search_bar));
  gtk_editable_set_text (GTK_EDITABLE (self->search_bar), initial);
  if (initial != NULL)
    gtk_editable_set_position (GTK_EDITABLE (self->search_bar), g_utf8_strlen (initial, -1));

  return TRUE;
}

void
bz_library_page_reset_search (BzLibraryPage *self)
{
  GtkAdjustment *vadjustment = NULL;
  g_return_if_fail (BZ_IS_LIBRARY_PAGE (self));

  vadjustment = gtk_scrolled_window_get_vadjustment (self->scroll);
  gtk_adjustment_set_value (vadjustment, 0.0);

  gtk_text_set_buffer (self->search_bar, NULL);
}

static void
items_changed (BzLibraryPage *self,
               guint          position,
               guint          removed,
               guint          added,
               GListModel    *model)
{
  set_page (self);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_HAS_APPS]);
}

static void
has_transactions_changed (BzLibraryPage        *self,
                          GParamSpec           *pspec,
                          BzTransactionManager *transactions)
{
  set_page (self);
}

static void
set_page (BzLibraryPage *self)
{
  guint    n_apps           = 0;
  guint    n_filtered       = 0;
  gboolean has_transactions = FALSE;

  if (self->model != NULL)
    {
      n_apps     = g_list_model_get_n_items (self->model);
      n_filtered = g_list_model_get_n_items (G_LIST_MODEL (self->filter_model));
    }

  if (self->state != NULL)
    {
      BzTransactionManager *manager = NULL;

      manager          = bz_state_info_get_transaction_manager (self->state);
      has_transactions = bz_transaction_manager_get_has_transactions (manager);
    }

  if (n_apps == 0 && !has_transactions)
    {
      gtk_editable_set_text (GTK_EDITABLE (self->search_bar), "");
      adw_view_stack_set_visible_child_name (self->stack, "empty");
    }
  else if (n_apps > 0 && n_filtered == 0)
    adw_view_stack_set_visible_child_name (self->stack, "no-results");
  else
    adw_view_stack_set_visible_child_name (self->stack, "content");
}

static gboolean
set_page_idle_cb (BzLibraryPage *self)
{
  set_page (self);
  return G_SOURCE_REMOVE;
}

static gboolean
filter (BzEntryGroup  *group,
        BzLibraryPage *self)
{
  const char *id    = NULL;
  const char *title = NULL;
  const char *text  = NULL;

  if (bz_entry_group_is_addon (group))
    return FALSE;

  id    = bz_entry_group_get_id (group);
  title = bz_entry_group_get_title (group);

  text = gtk_editable_get_text (GTK_EDITABLE (self->search_bar));

  if (text != NULL && *text != '\0')
    return strcasestr (id, text) != NULL ||
           strcasestr (title, text) != NULL;
  else
    return TRUE;
}

static gboolean
filter_user_extensions (BzGnomeExtensionInfo *info,
                        gpointer              user_data)
{
  return !bz_gnome_extension_info_get_is_system (info);
}

static gboolean
filter_system_extensions (BzGnomeExtensionInfo *info,
                          gpointer              user_data)
{
  return bz_gnome_extension_info_get_is_system (info);
}
