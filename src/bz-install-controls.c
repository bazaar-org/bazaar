/* bz-install-controls.c
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

#include <bge.h>

#include "bz-install-controls.h"
#include "bz-state-info.h"
#include "bz-template-callbacks.h"
#include "bz-util.h"

struct _BzInstallControls
{
  GtkBox parent_instance;

  BzEntryGroup              *group;
  BzStateInfo               *state;
  gboolean                   wide;
  BzTransactionEntryTracker *tracker;

  /* Template widgets */
  GtkWidget *open_button;
  GtkWidget *animated_button;
  GtkWidget *install_button;
};

G_DEFINE_FINAL_TYPE (BzInstallControls, bz_install_controls, GTK_TYPE_BOX)

enum
{
  PROP_0,
  PROP_WIDE,
  PROP_ENTRY_GROUP,
  PROP_STATE,
  PROP_TRACKER,
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
update_tracker (BzInstallControls *self)
{
  BzTransactionManager              *manager  = NULL;
  g_autoptr (GListModel)             all      = NULL;
  const char                        *group_id = NULL;
  g_autoptr (BzTransactionEntryTracker) found = NULL;

  if (self->state != NULL)
    manager = bz_state_info_get_transaction_manager (self->state);
  if (manager != NULL)
    g_object_get (manager, "all-trackers", &all, NULL);
  if (self->group != NULL)
    group_id = bz_entry_group_get_id (self->group);

  if (all != NULL && group_id != NULL)
    {
      for (guint i = 0; i < g_list_model_get_n_items (all); i++)
        {
          g_autoptr (BzTransactionEntryTracker) tracker = NULL;
          BzEntry                              *entry   = NULL;

          tracker = g_list_model_get_item (all, i);
          entry   = bz_transaction_entry_tracker_get_entry (tracker);

          if (g_strcmp0 (entry != NULL ? bz_entry_get_id (entry) : NULL, group_id) == 0)
            {
              found = g_steal_pointer (&tracker);
              break;
            }
        }
    }

  if (g_set_object (&self->tracker, found))
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP_TRACKER]);
}

static void
on_all_trackers_changed (GListModel        *model,
                         guint              position,
                         guint              removed,
                         guint              added,
                         BzInstallControls *self)
{
  update_tracker (self);
}

static void
cancel_cb (BzInstallControls *self,
           GtkButton         *button)
{
  if (self->group == NULL)
    return;

  gtk_widget_activate_action (GTK_WIDGET (self), "window.cancel-group", "s",
                              bz_entry_group_get_id (self->group));
}

static void
install_cb (BzInstallControls *self,
            GtkButton         *button)
{
  if (self->group == NULL)
    return;

  gtk_widget_activate_action (GTK_WIDGET (self), "window.install-group", "(sb)",
                              bz_entry_group_get_id (self->group), TRUE);
}

static void
remove_cb (BzInstallControls *self,
           GtkButton         *button)
{
  if (self->group == NULL)
    return;

  gtk_widget_activate_action (GTK_WIDGET (self), "window.remove-group", "(sb)",
                              bz_entry_group_get_id (self->group), TRUE);
}

static void
run_cb (BzInstallControls *self,
        GtkButton         *button)
{
  if (self->group == NULL)
    return;

  gtk_widget_activate_action (GTK_WIDGET (self), "window.launch-group", "s",
                              bz_entry_group_get_id (self->group));
}

static GListStore *
find_matching_updates (BzInstallControls *self,
                       GListModel        *available_updates)
{
  const char *group_id = NULL;
  guint       n_items  = 0;
  GListStore *store    = NULL;

  if (self->group == NULL || available_updates == NULL)
    return NULL;

  group_id = bz_entry_group_get_id (self->group);
  n_items  = g_list_model_get_n_items (available_updates);
  store    = g_list_store_new (BZ_TYPE_ENTRY);

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr (BzEntry) entry = NULL;
      const char *id            = NULL;

      entry = g_list_model_get_item (available_updates, i);
      id    = bz_entry_get_id (entry);

      if (g_strcmp0 (id, group_id) == 0)
        g_list_store_append (store, entry);
    }

  if (g_list_model_get_n_items (G_LIST_MODEL (store)) == 0)
    g_clear_object (&store);

  return store;
}

static void
update_cb (BzInstallControls *self,
           GtkButton         *button)
{
  GListModel *available_updates = NULL;
  g_autoptr (GListStore) store  = NULL;

  if (self->state == NULL)
    return;

  available_updates = bz_state_info_get_available_updates (self->state);
  store             = find_matching_updates (self, available_updates);

  if (store != NULL)
    g_signal_emit (self, signals[SIGNAL_UPDATE], 0, G_LIST_MODEL (store));
}

static char *
get_visible_page (gpointer    object,
                  int         installable,
                  int         removable,
                  GListModel *available_updates,
                  gboolean    active)
{
  BzInstallControls *self      = BZ_INSTALL_CONTROLS (object);
  g_autoptr (GListStore) store = NULL;

  if (active)
    return g_strdup ("install");

  if (removable > 0)
    {
      if (g_signal_has_handler_pending (self, signals[SIGNAL_UPDATE], 0, FALSE))
        store = find_matching_updates (self, available_updates);
      return g_strdup (store != NULL ? "update" : "open");
    }
  else if (installable > 0)
    return g_strdup ("install");
  else
    return g_strdup ("empty");
}

static char *
get_install_btn_state (gpointer object,
                       gboolean active,
                       gboolean pending,
                       double   progress)
{
  if (pending)
    return g_strdup ("pending");
  else if (active)
    return g_strdup ("fraction");
  else
    return g_strdup ("inactive");
}

static gboolean
is_blocked (gpointer      object,
            GListModel   *parental_blocked,
            BzEntryGroup *group)
{
  const char *id = NULL;

  if (parental_blocked == NULL || group == NULL)
    return FALSE;

  id = bz_entry_group_get_id (group);
  if (id == NULL)
    return FALSE;

  for (guint i = 0; i < g_list_model_get_n_items (parental_blocked); i++)
    {
      g_autoptr (GtkStringObject) obj = g_list_model_get_item (parental_blocked, i);
      if (strstr (gtk_string_object_get_string (obj), id) != NULL)
        return TRUE;
    }

  return FALSE;
}

static gboolean
idle_grab_focus (GWeakRef *wr)
{
  g_autoptr (BzInstallControls) self = NULL;

  self = g_weak_ref_get (wr);
  if (self == NULL)
    goto done;

  if (gtk_widget_is_visible (GTK_WIDGET (self)))
    gtk_widget_grab_focus (self->group != NULL && bz_entry_group_get_removable (self->group) > 0
                               ? self->open_button
                               : self->install_button);

done:
  return G_SOURCE_REMOVE;
}

static void
bz_install_controls_dispose (GObject *object)
{
  BzInstallControls *self = BZ_INSTALL_CONTROLS (object);

  g_clear_object (&self->group);
  g_clear_object (&self->state);
  g_clear_object (&self->tracker);

  G_OBJECT_CLASS (bz_install_controls_parent_class)->dispose (object);
}

static void
bz_install_controls_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  BzInstallControls *self = BZ_INSTALL_CONTROLS (object);

  switch (prop_id)
    {
    case PROP_WIDE:
      g_value_set_boolean (value, self->wide);
      break;
    case PROP_ENTRY_GROUP:
      g_value_set_object (value, self->group);
      break;
    case PROP_STATE:
      g_value_set_object (value, self->state);
      break;
    case PROP_TRACKER:
      g_value_set_object (value, self->tracker);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_install_controls_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  BzInstallControls *self = BZ_INSTALL_CONTROLS (object);

  switch (prop_id)
    {
    case PROP_WIDE:
      bz_install_controls_set_wide (self, g_value_get_boolean (value));
      break;
    case PROP_ENTRY_GROUP:
      bz_install_controls_set_entry_group (self, g_value_get_object (value));
      break;
    case PROP_STATE:
      bz_install_controls_set_state (self, g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_install_controls_class_init (BzInstallControlsClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose      = bz_install_controls_dispose;
  object_class->get_property = bz_install_controls_get_property;
  object_class->set_property = bz_install_controls_set_property;

  props[PROP_WIDE] =
      g_param_spec_boolean (
          "wide",
          NULL, NULL,
          TRUE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_ENTRY_GROUP] =
      g_param_spec_object (
          "entry-group",
          NULL, NULL,
          BZ_TYPE_ENTRY_GROUP,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_STATE] =
      g_param_spec_object (
          "state",
          NULL, NULL,
          BZ_TYPE_STATE_INFO,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_TRACKER] =
      g_param_spec_object (
          "tracker",
          NULL, NULL,
          BZ_TYPE_TRANSACTION_ENTRY_TRACKER,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

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

  g_type_ensure (BZ_TYPE_ENTRY_GROUP);
  g_type_ensure (BZ_TYPE_STATE_INFO);

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/kolunmi/Bazaar/bz-install-controls.ui");
  bz_widget_class_bind_all_util_callbacks (widget_class);

  gtk_widget_class_bind_template_child (widget_class, BzInstallControls, open_button);
  gtk_widget_class_bind_template_child (widget_class, BzInstallControls, animated_button);

  gtk_widget_class_bind_template_callback (widget_class, install_cb);
  gtk_widget_class_bind_template_callback (widget_class, remove_cb);
  gtk_widget_class_bind_template_callback (widget_class, run_cb);
  gtk_widget_class_bind_template_callback (widget_class, update_cb);
  gtk_widget_class_bind_template_callback (widget_class, get_visible_page);
  gtk_widget_class_bind_template_callback (widget_class, get_install_btn_state);
  gtk_widget_class_bind_template_callback (widget_class, is_blocked);
}

static void
bz_install_controls_init (BzInstallControls *self)
{
  g_autoptr (GtkWidget) btn_cancel = NULL;

  self->wide = TRUE;

  gtk_widget_init_template (GTK_WIDGET (self));

  self->install_button = bge_wdgt_renderer_lookup_object (
      BGE_WDGT_RENDERER (self->animated_button), "btn-install");
  g_signal_connect_swapped (
      self->install_button, "clicked",
      G_CALLBACK (install_cb), self);

  btn_cancel = bge_wdgt_renderer_lookup_object (
      BGE_WDGT_RENDERER (self->animated_button), "btn-cancel");
  g_signal_connect_swapped (
      btn_cancel, "clicked",
      G_CALLBACK (cancel_cb), self);
}

GtkWidget *
bz_install_controls_new (void)
{
  return g_object_new (BZ_TYPE_INSTALL_CONTROLS, NULL);
}

gboolean
bz_install_controls_get_wide (BzInstallControls *self)
{
  g_return_val_if_fail (BZ_IS_INSTALL_CONTROLS (self), FALSE);
  return self->wide;
}

void
bz_install_controls_set_wide (BzInstallControls *self,
                              gboolean           wide)
{
  g_return_if_fail (BZ_IS_INSTALL_CONTROLS (self));

  wide = !!wide;
  if (self->wide == wide)
    return;

  self->wide = wide;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_WIDE]);
}

BzEntryGroup *
bz_install_controls_get_entry_group (BzInstallControls *self)
{
  g_return_val_if_fail (BZ_IS_INSTALL_CONTROLS (self), NULL);
  return self->group;
}

void
bz_install_controls_set_entry_group (BzInstallControls *self,
                                     BzEntryGroup      *group)
{
  g_return_if_fail (BZ_IS_INSTALL_CONTROLS (self));

  if (self->group == group)
    return;

  g_clear_object (&self->group);
  if (group != NULL)
    self->group = g_object_ref (group);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ENTRY_GROUP]);

  if (group != NULL)
    g_idle_add_full (
        G_PRIORITY_DEFAULT_IDLE,
        (GSourceFunc) idle_grab_focus,
        bz_track_weak (self), bz_weak_release);

  update_tracker (self);
}

BzStateInfo *
bz_install_controls_get_state (BzInstallControls *self)
{
  g_return_val_if_fail (BZ_IS_INSTALL_CONTROLS (self), NULL);
  return self->state;
}

void
bz_install_controls_set_state (BzInstallControls *self,
                               BzStateInfo       *state)
{
  g_return_if_fail (BZ_IS_INSTALL_CONTROLS (self));

  if (self->state != NULL)
    {
      BzTransactionManager  *old_mgr = NULL;
      g_autoptr (GListModel) all     = NULL;

      old_mgr = bz_state_info_get_transaction_manager (self->state);
      if (old_mgr != NULL)
        g_object_get (old_mgr, "all-trackers", &all, NULL);
      if (all != NULL)
        g_signal_handlers_disconnect_by_func (all, on_all_trackers_changed, self);
    }

  g_set_object (&self->state, state);

  if (state != NULL)
    {
      BzTransactionManager  *mgr = NULL;
      g_autoptr (GListModel) all = NULL;

      mgr = bz_state_info_get_transaction_manager (state);
      if (mgr != NULL)
        g_object_get (mgr, "all-trackers", &all, NULL);
      if (all != NULL)
        g_signal_connect (all, "items-changed",
                          G_CALLBACK (on_all_trackers_changed), self);
    }

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_STATE]);
  update_tracker (self);
}
