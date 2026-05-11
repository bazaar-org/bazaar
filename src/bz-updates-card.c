/* bz-updates-card.c
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

#include "bz-entry.h"
#include "bz-release.h"
#include "bz-releases-list.h"
#include "bz-template-callbacks.h"
#include "bz-updates-card.h"

struct _BzUpdatesCard
{
  AdwBin parent_instance;

  BzStateInfo *state;

  /* Template widgets */
  AdwExpanderRow     *expander_row;
  GtkCustomFilter    *runtimes_filter;
  GtkFilterListModel *runtimes_filter_model;

  GPtrArray *app_rows;
  GtkWidget *runtimes_row;
};

G_DEFINE_FINAL_TYPE (BzUpdatesCard, bz_updates_card, ADW_TYPE_BIN)

enum
{
  PROP_0,
  PROP_STATE,
  LAST_PROP
};

static GParamSpec *props[LAST_PROP] = { 0 };

enum
{
  SIGNAL_UPDATE,
  LAST_SIGNAL,
};

static guint signals[LAST_SIGNAL];

static char    *format_update_count (gpointer object, GListModel *updates);
static void     update_all_cb (GtkButton *button, BzUpdatesCard *self);
static void     update_runtimes_cb (GtkButton *button, BzUpdatesCard *self);
static gboolean filter_runtimes (BzEntry *entry, BzUpdatesCard *self);
static void     on_available_updates_changed (BzUpdatesCard *self, GParamSpec *pspec, BzStateInfo *state);
static void     on_runtimes_changed (GtkFilterListModel *model, guint position, guint removed, guint added, BzUpdatesCard *self);

static void
on_update_single_cb (GtkButton *button,
                     BzEntry   *entry)
{
  BzUpdatesCard *self          = NULL;
  g_autoptr (GListStore) store = NULL;

  self  = BZ_UPDATES_CARD (gtk_widget_get_ancestor (GTK_WIDGET (button), BZ_TYPE_UPDATES_CARD));
  store = g_list_store_new (BZ_TYPE_ENTRY);
  g_list_store_append (store, entry);
  g_signal_emit (self, signals[SIGNAL_UPDATE], 0, store);
}

static void
on_version_history_cb (GtkButton *button,
                       BzEntry   *entry)
{
  GtkRoot *root                  = NULL;
  g_autoptr (GListModel) history = NULL;
  GtkWidget *dialog              = NULL;

  root = gtk_widget_get_root (GTK_WIDGET (button));
  if (root == NULL)
    return;

  g_object_get (entry, "version-history", &history, NULL);
  dialog = bz_releases_dialog_new (history, NULL);
  adw_dialog_present (ADW_DIALOG (dialog), GTK_WIDGET (root));
}

static GtkWidget *
build_app_row (BzEntry       *entry,
               BzUpdatesCard *self)
{
  AdwActionRow *row              = NULL;
  GtkWidget    *icon             = NULL;
  GtkWidget    *history_button   = NULL;
  GtkWidget    *update_button    = NULL;
  GdkPaintable *paintable        = NULL;
  g_autoptr (GListModel) history = NULL;
  g_autofree char *installed_ver = NULL;
  const char      *new_ver       = NULL; // This will probably the same as the installed version if using cache...

  row = ADW_ACTION_ROW (adw_action_row_new ());
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row),
                                 bz_entry_get_title (entry));

  g_object_get (entry, "icon-paintable", &paintable, NULL);
  icon = paintable != NULL
             ? gtk_image_new_from_paintable (paintable)
             : gtk_image_new_from_icon_name ("application-x-executable");
  g_clear_object (&paintable);

  gtk_image_set_pixel_size (GTK_IMAGE (icon), 48);
  gtk_widget_set_size_request (icon, 48, 48);
  gtk_widget_set_margin_top (icon, 6);
  gtk_widget_set_margin_bottom (icon, 6);
  gtk_widget_set_valign (icon, GTK_ALIGN_CENTER);
  gtk_widget_add_css_class (icon, "icon-dropshadow");
  adw_action_row_add_prefix (row, icon);

  g_object_get (entry,
                "version-history", &history,
                "installed-version", &installed_ver,
                NULL);

  if (history != NULL && g_list_model_get_n_items (history) > 0 && installed_ver != NULL)
    {
      g_autoptr (BzRelease) first = g_list_model_get_item (history, 0);
      new_ver                     = bz_release_get_version (first);

      if (new_ver != NULL && g_strcmp0 (installed_ver, new_ver) != 0)
        {
          g_autofree char *subtitle = g_strdup_printf ("%s → %s", installed_ver, new_ver);
          adw_action_row_set_subtitle (row, subtitle);
        }
    }

  history_button = gtk_button_new_from_icon_name ("view-list-bullet-symbolic");
  gtk_widget_set_valign (history_button, GTK_ALIGN_CENTER);
  gtk_widget_set_tooltip_text (history_button, _ ("Version History"));
  gtk_widget_add_css_class (history_button, "flat");
  gtk_widget_set_visible (history_button, history != NULL && g_list_model_get_n_items (history) > 0);
  g_signal_connect_data (history_button, "clicked",
                         G_CALLBACK (on_version_history_cb),
                         g_object_ref (entry), (GClosureNotify) g_object_unref,
                         0);
  adw_action_row_add_suffix (row, history_button);

  update_button = gtk_button_new_with_label (_ ("Update"));
  gtk_widget_set_valign (update_button, GTK_ALIGN_CENTER);
  g_signal_connect_data (update_button, "clicked",
                         G_CALLBACK (on_update_single_cb),
                         g_object_ref (entry), (GClosureNotify) g_object_unref,
                         0);
  adw_action_row_add_suffix (row, update_button);
  return GTK_WIDGET (row);
}

static GtkWidget *
build_runtimes_row (BzUpdatesCard *self)
{
  AdwActionRow *row           = NULL;
  GtkWidget    *update_button = NULL;

  row = ADW_ACTION_ROW (adw_action_row_new ());
  gtk_widget_set_visible (GTK_WIDGET (row), FALSE);

  update_button = gtk_button_new_with_label (_ ("Update"));
  gtk_widget_set_valign (update_button, GTK_ALIGN_CENTER);
  g_signal_connect (update_button, "clicked",
                    G_CALLBACK (update_runtimes_cb), self);
  adw_action_row_add_suffix (row, update_button);

  return GTK_WIDGET (row);
}

static void
on_runtimes_changed (GtkFilterListModel *model,
                     guint               position,
                     guint               removed,
                     guint               added,
                     BzUpdatesCard      *self)
{
  guint            n_items = 0;
  g_autofree char *title   = NULL;

  if (self->runtimes_row == NULL)
    return;

  n_items = g_list_model_get_n_items (G_LIST_MODEL (model));
  gtk_widget_set_visible (self->runtimes_row, n_items > 0);

  if (n_items == 0)
    return;

  title = g_strdup_printf (ngettext ("%u Runtime Update", "%u Runtime Updates", n_items), n_items);
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (self->runtimes_row), title);
}

static void
repopulate_expander_row (BzUpdatesCard *self)
{
  GListModel *updates = NULL;
  guint       n_items = 0;
  guint       i       = 0;

  for (i = 0; i < self->app_rows->len; i++)
    adw_expander_row_remove (self->expander_row, g_ptr_array_index (self->app_rows, i));
  g_ptr_array_set_size (self->app_rows, 0);

  if (self->runtimes_row != NULL)
    {
      adw_expander_row_remove (self->expander_row, self->runtimes_row);
      self->runtimes_row = NULL;
    }

  if (self->state == NULL)
    return;

  updates = bz_state_info_get_available_updates (self->state);
  if (updates == NULL)
    return;

  n_items = g_list_model_get_n_items (updates);

  for (i = 0; i < n_items; i++)
    {
      g_autoptr (BzEntry) entry = NULL;
      GtkWidget *row            = NULL;

      entry = g_list_model_get_item (updates, i);

      if (entry == NULL || !BZ_IS_ENTRY (entry))
        continue;

      if (!bz_entry_is_of_kinds (entry, BZ_ENTRY_KIND_APPLICATION))
        continue;

      row = build_app_row (entry, self);
      adw_expander_row_add_row (self->expander_row, row);
      g_ptr_array_add (self->app_rows, row);
    }

  self->runtimes_row = build_runtimes_row (self);
  adw_expander_row_add_row (self->expander_row, self->runtimes_row);
  on_runtimes_changed (self->runtimes_filter_model, 0, 0, 0, self);
}

static void
on_available_updates_changed (BzUpdatesCard *self,
                              GParamSpec    *pspec,
                              BzStateInfo   *state)
{
  gtk_filter_changed (GTK_FILTER (self->runtimes_filter),
                      GTK_FILTER_CHANGE_DIFFERENT);
  repopulate_expander_row (self);
}

static void
bz_updates_card_dispose (GObject *object)
{
  BzUpdatesCard *self = BZ_UPDATES_CARD (object);

  g_clear_object (&self->state);
  g_clear_pointer (&self->app_rows, g_ptr_array_unref);
  gtk_widget_dispose_template (GTK_WIDGET (self), BZ_TYPE_UPDATES_CARD);

  G_OBJECT_CLASS (bz_updates_card_parent_class)->dispose (object);
}

static void
bz_updates_card_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  BzUpdatesCard *self = BZ_UPDATES_CARD (object);

  switch (prop_id)
    {
    case PROP_STATE:
      g_value_set_object (value, bz_updates_card_get_state (self));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_updates_card_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  BzUpdatesCard *self = BZ_UPDATES_CARD (object);

  switch (prop_id)
    {
    case PROP_STATE:
      bz_updates_card_set_state (self, g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_updates_card_class_init (BzUpdatesCardClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose      = bz_updates_card_dispose;
  object_class->get_property = bz_updates_card_get_property;
  object_class->set_property = bz_updates_card_set_property;

  props[PROP_STATE] =
      g_param_spec_object (
          "state",
          NULL, NULL,
          BZ_TYPE_STATE_INFO,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

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

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/kolunmi/Bazaar/bz-updates-card.ui");

  bz_widget_class_bind_all_util_callbacks (widget_class);
  gtk_widget_class_bind_template_child (widget_class, BzUpdatesCard, expander_row);
  gtk_widget_class_bind_template_child (widget_class, BzUpdatesCard, runtimes_filter);
  gtk_widget_class_bind_template_child (widget_class, BzUpdatesCard, runtimes_filter_model);
  gtk_widget_class_bind_template_callback (widget_class, format_update_count);
  gtk_widget_class_bind_template_callback (widget_class, update_all_cb);
}

static void
bz_updates_card_init (BzUpdatesCard *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  self->app_rows = g_ptr_array_new ();

  gtk_custom_filter_set_filter_func (
      self->runtimes_filter, (GtkCustomFilterFunc) filter_runtimes,
      self, NULL);

  g_signal_connect (self->runtimes_filter_model, "items-changed",
                    G_CALLBACK (on_runtimes_changed), self);
}

GtkWidget *
bz_updates_card_new (void)
{
  return g_object_new (BZ_TYPE_UPDATES_CARD, NULL);
}

void
bz_updates_card_set_state (BzUpdatesCard *self,
                           BzStateInfo   *state)
{
  g_return_if_fail (BZ_IS_UPDATES_CARD (self));
  g_return_if_fail (state == NULL || BZ_IS_STATE_INFO (state));

  g_clear_object (&self->state);
  if (state != NULL)
    {
      self->state = g_object_ref (state);
      g_signal_connect_object (state, "notify::available-updates",
                               G_CALLBACK (on_available_updates_changed),
                               self, G_CONNECT_SWAPPED);
    }

  gtk_filter_changed (GTK_FILTER (self->runtimes_filter),
                      GTK_FILTER_CHANGE_DIFFERENT);

  repopulate_expander_row (self);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_STATE]);
}

BzStateInfo *
bz_updates_card_get_state (BzUpdatesCard *self)
{
  g_return_val_if_fail (BZ_IS_UPDATES_CARD (self), NULL);
  return self->state;
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

static void
update_all_cb (GtkButton     *button,
               BzUpdatesCard *self)
{
  GListModel *updates = NULL;

  g_return_if_fail (GTK_IS_BUTTON (button));
  g_return_if_fail (BZ_IS_UPDATES_CARD (self));

  if (self->state == NULL)
    return;

  updates = bz_state_info_get_available_updates (self->state);
  if (updates == NULL)
    return;

  g_signal_emit (self, signals[SIGNAL_UPDATE], 0, updates);
}

static void
update_runtimes_cb (GtkButton     *button,
                    BzUpdatesCard *self)
{
  GListModel *runtimes         = NULL;
  g_autoptr (GListStore) store = NULL;
  guint n_items                = 0;

  runtimes = G_LIST_MODEL (self->runtimes_filter_model);
  n_items  = g_list_model_get_n_items (runtimes);

  if (n_items == 0)
    return;

  store = g_list_store_new (BZ_TYPE_ENTRY);

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr (BzEntry) entry = g_list_model_get_item (runtimes, i);
      if (entry != NULL)
        g_list_store_append (store, entry);
    }

  g_signal_emit (self, signals[SIGNAL_UPDATE], 0, store);
}

static gboolean
filter_runtimes (BzEntry       *entry,
                 BzUpdatesCard *self)
{
  g_return_val_if_fail (BZ_IS_ENTRY (entry), FALSE);

  return bz_entry_is_of_kinds (entry, BZ_ENTRY_KIND_RUNTIME) ||
         bz_entry_is_of_kinds (entry, BZ_ENTRY_KIND_ADDON);
}
