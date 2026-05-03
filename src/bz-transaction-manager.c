/* bz-transaction-manager.c
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

#define G_LOG_DOMAIN "BAZAAR::TRANSACTIONS"

#include "config.h"

#include <glib/gi18n.h>

#include "bz-backend-transaction-op-payload.h"
#include "bz-backend-transaction-op-progress-payload.h"
#include "bz-env.h"
#include "bz-gnome-extension-entry.h"
#include "bz-marshalers.h"
#include "bz-transaction-manager.h"
#include "bz-util.h"

/* clang-format off */
G_DEFINE_QUARK (bz-transaction-mgr-error-quark, bz_transaction_mgr_error);
/* clang-format on */

enum
{
  HOOK_CONTINUE,
  HOOK_STOP,
  HOOK_CONFIRM,
  HOOK_DENY,
};

static inline void
finish_queued_schedule_data (gpointer ptr);

BZ_DEFINE_DATA (
    queued_schedule,
    QueuedSchedule,
    {
      GWeakRef      *self;
      BzTransaction *transaction;
      DexPromise    *promise;
      GTimer        *timer;
    },
    finish_queued_schedule_data (self);)

struct _BzTransactionManager
{
  GObject parent_instance;

  BzMainConfig *config;
  BzBackend    *backend;
  BzBackend    *gnome_ext_backend;

  gboolean    paused;
  GListStore *transactions;
  double      current_progress;
  gboolean    pending;

  QueuedScheduleData *current;
  DexFuture          *loop;

  GtkFlattenListModel *all_trackers;
  GtkFilterListModel  *install_trackers;
  GtkFilterListModel  *removal_trackers;

  GQueue queue;
};

G_DEFINE_FINAL_TYPE (BzTransactionManager, bz_transaction_manager, G_TYPE_OBJECT)

enum
{
  PROP_0,

  PROP_CONFIG,
  PROP_BACKEND,
  PROP_GNOME_EXT_BACKEND,
  PROP_PAUSED,
  PROP_TRANSACTIONS,
  PROP_HAS_TRANSACTIONS,
  PROP_ACTIVE,
  PROP_PENDING,
  PROP_CURRENT_PROGRESS,
  PROP_INSTALL_TRACKERS,
  PROP_REMOVAL_TRACKERS,
  PROP_ALL_TRACKERS,

  LAST_PROP
};
static GParamSpec *props[LAST_PROP] = { 0 };

enum
{
  SIGNAL_SUCCESS,
  SIGNAL_FAILURE,

  LAST_SIGNAL,
};
static guint signals[LAST_SIGNAL];

static DexFuture *
transaction_fiber (QueuedScheduleData *data);

static DexFuture *
transaction_finally (DexFuture          *future,
                     QueuedScheduleData *data);

static DexFuture *
then_loop_cb (DexFuture *future,
              GWeakRef  *wr);

static DexFuture *
dispatch_next (BzTransactionManager *self);

static void
bz_transaction_manager_dispose (GObject *object)
{
  BzTransactionManager *self = BZ_TRANSACTION_MANAGER (object);

  g_clear_object (&self->config);
  g_clear_object (&self->backend);
  g_clear_object (&self->gnome_ext_backend);
  g_clear_object (&self->transactions);
  g_queue_clear_full (&self->queue, queued_schedule_data_unref);
  g_clear_pointer (&self->current, queued_schedule_data_unref);
  dex_clear (&self->loop);

  G_OBJECT_CLASS (bz_transaction_manager_parent_class)->dispose (object);
}

static void
bz_transaction_manager_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
  BzTransactionManager *self = BZ_TRANSACTION_MANAGER (object);

  switch (prop_id)
    {
    case PROP_CONFIG:
      g_value_set_boxed (value, bz_transaction_manager_get_config (self));
      break;
    case PROP_BACKEND:
      g_value_set_object (value, bz_transaction_manager_get_backend (self));
      break;
    case PROP_GNOME_EXT_BACKEND:
      g_value_set_object (value, bz_transaction_manager_get_gnome_ext_backend (self));
      break;
    case PROP_PAUSED:
      g_value_set_boolean (value, bz_transaction_manager_get_paused (self));
      break;
    case PROP_TRANSACTIONS:
      g_value_set_object (value, self->transactions);
      break;
    case PROP_HAS_TRANSACTIONS:
      g_value_set_boolean (value, bz_transaction_manager_get_has_transactions (self));
      break;
    case PROP_ACTIVE:
      g_value_set_boolean (value, bz_transaction_manager_get_active (self));
      break;
    case PROP_PENDING:
      g_value_set_boolean (value, bz_transaction_manager_get_pending (self));
      break;
    case PROP_CURRENT_PROGRESS:
      g_value_set_double (value, self->current_progress);
      break;
    case PROP_INSTALL_TRACKERS:
      g_value_set_object (value, self->install_trackers);
      break;
    case PROP_REMOVAL_TRACKERS:
      g_value_set_object (value, self->removal_trackers);
      break;
    case PROP_ALL_TRACKERS:
      g_value_set_object (value, self->all_trackers);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_transaction_manager_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
  BzTransactionManager *self = BZ_TRANSACTION_MANAGER (object);

  switch (prop_id)
    {
    case PROP_CONFIG:
      bz_transaction_manager_set_config (self, g_value_get_boxed (value));
      break;
    case PROP_BACKEND:
      bz_transaction_manager_set_backend (self, g_value_get_object (value));
      break;
    case PROP_GNOME_EXT_BACKEND:
      bz_transaction_manager_set_gnome_ext_backend (self, g_value_get_object (value));
      break;
    case PROP_PAUSED:
      bz_transaction_manager_set_paused (self, g_value_get_boolean (value));
      break;
    case PROP_TRANSACTIONS:
    case PROP_HAS_TRANSACTIONS:
    case PROP_ACTIVE:
    case PROP_PENDING:
    case PROP_CURRENT_PROGRESS:
    case PROP_INSTALL_TRACKERS:
    case PROP_REMOVAL_TRACKERS:
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static gboolean
filter_install_trackers (gpointer item,
                         gpointer user_data)
{
  BzTransactionEntryTracker *tracker = NULL;
  BzTransactionEntryKind     kind    = 0;

  tracker = BZ_TRANSACTION_ENTRY_TRACKER (item);
  if (tracker == NULL)
    return FALSE;

  kind = bz_transaction_entry_tracker_get_kind (tracker);

  return kind == BZ_TRANSACTION_ENTRY_KIND_INSTALL ||
         kind == BZ_TRANSACTION_ENTRY_KIND_UPDATE;
}

static gboolean
filter_removal_trackers (gpointer item,
                         gpointer user_data)
{
  BzTransactionEntryTracker *tracker = NULL;

  tracker = BZ_TRANSACTION_ENTRY_TRACKER (item);
  if (tracker == NULL)
    return FALSE;

  return bz_transaction_entry_tracker_get_kind (tracker) == BZ_TRANSACTION_ENTRY_KIND_REMOVAL;
}

static gpointer
get_trackers_model (gpointer item,
                    gpointer user_data)
{
  BzTransaction *transaction      = NULL;
  g_autoptr (GListModel) trackers = NULL;

  transaction = BZ_TRANSACTION (item);
  if (transaction == NULL)
    return NULL;

  trackers = g_object_ref (bz_transaction_get_trackers (transaction));
  g_object_unref (item);
  return g_steal_pointer (&trackers);
}

static void
bz_transaction_manager_class_init (BzTransactionManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose      = bz_transaction_manager_dispose;
  object_class->get_property = bz_transaction_manager_get_property;
  object_class->set_property = bz_transaction_manager_set_property;

  props[PROP_CONFIG] =
      g_param_spec_object (
          "config",
          NULL, NULL,
          BZ_TYPE_MAIN_CONFIG,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_BACKEND] =
      g_param_spec_object (
          "backend",
          NULL, NULL,
          BZ_TYPE_BACKEND,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_GNOME_EXT_BACKEND] =
      g_param_spec_object (
          "gnome-ext-backend",
          NULL, NULL,
          BZ_TYPE_BACKEND,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_PAUSED] =
      g_param_spec_boolean (
          "paused",
          NULL, NULL, FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_TRANSACTIONS] =
      g_param_spec_object (
          "transactions",
          NULL, NULL,
          G_TYPE_LIST_MODEL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  props[PROP_HAS_TRANSACTIONS] =
      g_param_spec_boolean (
          "has-transactions",
          NULL, NULL, FALSE,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  props[PROP_ACTIVE] =
      g_param_spec_boolean (
          "active",
          NULL, NULL, FALSE,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  props[PROP_PENDING] =
      g_param_spec_boolean (
          "pending",
          NULL, NULL, FALSE,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  props[PROP_CURRENT_PROGRESS] =
      g_param_spec_double (
          "current-progress",
          NULL, NULL,
          0.0, 1.0, 0.0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  props[PROP_INSTALL_TRACKERS] =
      g_param_spec_object (
          "install-trackers",
          NULL, NULL,
          G_TYPE_LIST_MODEL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  props[PROP_REMOVAL_TRACKERS] =
      g_param_spec_object (
          "removal-trackers",
          NULL, NULL,
          G_TYPE_LIST_MODEL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  props[PROP_ALL_TRACKERS] =
      g_param_spec_object (
          "all-trackers",
          NULL, NULL,
          G_TYPE_LIST_MODEL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  signals[SIGNAL_SUCCESS] =
      g_signal_new (
          "success",
          G_OBJECT_CLASS_TYPE (klass),
          G_SIGNAL_RUN_FIRST,
          0,
          NULL, NULL,
          g_cclosure_marshal_VOID__OBJECT,
          G_TYPE_NONE,
          1, BZ_TYPE_TRANSACTION, 0);
  g_signal_set_va_marshaller (
      signals[SIGNAL_SUCCESS],
      G_TYPE_FROM_CLASS (klass),
      bz_marshal_VOID__OBJECT_BOXEDv);

  signals[SIGNAL_FAILURE] =
      g_signal_new (
          "failure",
          G_OBJECT_CLASS_TYPE (klass),
          G_SIGNAL_RUN_FIRST,
          0,
          NULL, NULL,
          g_cclosure_marshal_VOID__OBJECT,
          G_TYPE_NONE,
          1, BZ_TYPE_TRANSACTION, 0);
  g_signal_set_va_marshaller (
      signals[SIGNAL_FAILURE],
      G_TYPE_FROM_CLASS (klass),
      g_cclosure_marshal_VOID__OBJECTv);
}

static void
bz_transaction_manager_init (BzTransactionManager *self)
{
  GtkCustomFilter *install_filter;
  GtkCustomFilter *removal_filter;
  GtkMapListModel *map_model;

  self->transactions = g_list_store_new (BZ_TYPE_TRANSACTION);
  g_queue_init (&self->queue);

  map_model = gtk_map_list_model_new (
      g_object_ref (G_LIST_MODEL (self->transactions)),
      get_trackers_model,
      NULL,
      NULL);
  self->all_trackers = gtk_flatten_list_model_new (G_LIST_MODEL (map_model));

  install_filter = gtk_custom_filter_new (
      filter_install_trackers,
      NULL,
      NULL);
  self->install_trackers = gtk_filter_list_model_new (
      g_object_ref (G_LIST_MODEL (self->all_trackers)),
      GTK_FILTER (install_filter));

  removal_filter = gtk_custom_filter_new (
      filter_removal_trackers,
      NULL,
      NULL);
  self->removal_trackers = gtk_filter_list_model_new (
      g_object_ref (G_LIST_MODEL (self->all_trackers)),
      GTK_FILTER (removal_filter));
}

BzTransactionManager *
bz_transaction_manager_new (void)
{
  return g_object_new (BZ_TYPE_TRANSACTION_MANAGER, NULL);
}

void
bz_transaction_manager_set_config (BzTransactionManager *self,
                                   BzMainConfig         *config)
{
  g_return_if_fail (BZ_IS_TRANSACTION_MANAGER (self));

  g_clear_object (&self->config);
  if (config != NULL)
    self->config = g_object_ref (config);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_CONFIG]);
}

BzMainConfig *
bz_transaction_manager_get_config (BzTransactionManager *self)
{
  g_return_val_if_fail (BZ_IS_TRANSACTION_MANAGER (self), NULL);
  return self->config;
}

void
bz_transaction_manager_set_backend (BzTransactionManager *self,
                                    BzBackend            *backend)
{
  g_return_if_fail (BZ_IS_TRANSACTION_MANAGER (self));
  g_return_if_fail (backend == NULL || BZ_IS_BACKEND (backend));

  g_clear_object (&self->backend);
  if (backend != NULL)
    self->backend = g_object_ref (backend);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_BACKEND]);
}

BzBackend *
bz_transaction_manager_get_backend (BzTransactionManager *self)
{
  g_return_val_if_fail (BZ_IS_TRANSACTION_MANAGER (self), NULL);
  return self->backend;
}

void
bz_transaction_manager_set_gnome_ext_backend (BzTransactionManager *self,
                                              BzBackend            *backend)
{
  g_return_if_fail (BZ_IS_TRANSACTION_MANAGER (self));
  g_return_if_fail (backend == NULL || BZ_IS_BACKEND (backend));

  g_clear_object (&self->gnome_ext_backend);
  if (backend != NULL)
    self->gnome_ext_backend = g_object_ref (backend);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_GNOME_EXT_BACKEND]);
}

BzBackend *
bz_transaction_manager_get_gnome_ext_backend (BzTransactionManager *self)
{
  g_return_val_if_fail (BZ_IS_TRANSACTION_MANAGER (self), NULL);
  return self->gnome_ext_backend;
}

void
bz_transaction_manager_set_paused (BzTransactionManager *self,
                                   gboolean              paused)
{
  g_return_if_fail (BZ_IS_TRANSACTION_MANAGER (self));

  if (!!self->paused == !!paused)
    return;

  self->paused = paused;
  if (!paused)
    dex_future_disown (dispatch_next (self));

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_PAUSED]);
}

gboolean
bz_transaction_manager_get_paused (BzTransactionManager *self)
{
  g_return_val_if_fail (BZ_IS_TRANSACTION_MANAGER (self), FALSE);
  return self->paused;
}

gboolean
bz_transaction_manager_get_active (BzTransactionManager *self)
{
  g_return_val_if_fail (BZ_IS_TRANSACTION_MANAGER (self), FALSE);
  return self->loop != NULL;
}

gboolean
bz_transaction_manager_get_pending (BzTransactionManager *self)
{
  g_return_val_if_fail (BZ_IS_TRANSACTION_MANAGER (self), FALSE);
  return self->loop != NULL && self->pending;
}

gboolean
bz_transaction_manager_get_has_transactions (BzTransactionManager *self)
{
  g_return_val_if_fail (BZ_IS_TRANSACTION_MANAGER (self), FALSE);
  return g_list_model_get_n_items (G_LIST_MODEL (self->transactions)) > 0;
}

GListModel *
bz_transaction_manager_get_all_trackers (BzTransactionManager *self)
{
  g_return_val_if_fail (BZ_IS_TRANSACTION_MANAGER (self), NULL);
  return G_LIST_MODEL (self->all_trackers);
}

DexFuture *
bz_transaction_manager_add (BzTransactionManager *self,
                            BzTransaction        *transaction)
{
  g_autoptr (QueuedScheduleData) data = NULL;

  dex_return_error_if_fail (BZ_IS_TRANSACTION_MANAGER (self));
  dex_return_error_if_fail (self->backend != NULL);
  dex_return_error_if_fail (BZ_IS_TRANSACTION (transaction));

  bz_transaction_hold (transaction);

  if (self->queue.length > 0)
    {
      BzTransaction *to_merge[2]                = { 0 };
      g_autoptr (BzTransaction) new_transaction = NULL;
      guint position                            = 0;

      data = g_queue_pop_head (&self->queue);

      g_list_store_find (self->transactions, data->transaction, &position);
      g_assert (position != G_MAXUINT);

      to_merge[0]     = g_steal_pointer (&data->transaction);
      to_merge[1]     = g_object_ref (transaction);
      new_transaction = bz_transaction_new_merged (to_merge, G_N_ELEMENTS (to_merge));

      g_list_store_splice (self->transactions, position, 1, (gpointer *) &new_transaction, 1);
      for (guint i = 0; i < G_N_ELEMENTS (to_merge); i++)
        g_object_unref (to_merge[i]);

      data->transaction = g_steal_pointer (&new_transaction);
    }
  else
    {
      data              = queued_schedule_data_new ();
      data->self        = bz_track_weak (self);
      data->transaction = g_object_ref (transaction);
      data->promise     = dex_promise_new_cancellable ();

      g_list_store_insert (self->transactions, 0, transaction);
    }

  g_queue_push_head (&self->queue, queued_schedule_data_ref (data));
  if (self->loop == NULL && !self->paused)
    dex_future_disown (dispatch_next (self));

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_HAS_TRANSACTIONS]);
  return dex_ref (data->promise);
}

void
bz_transaction_manager_cancel_current (BzTransactionManager *self)
{
  g_return_if_fail (BZ_IS_TRANSACTION_MANAGER (self));

  if (self->current == NULL)
    return;

  dex_promise_reject (
      self->current->promise,
      g_error_new (G_IO_ERROR, G_IO_ERROR_CANCELLED, "Cancelled by API"));
  g_object_set (
      self->current->transaction,
      "status", "Cancelled",
      "progress", 1.0,
      "finished", TRUE,
      "success", FALSE,
      "error", "Cancelled by API",
      NULL);
  g_clear_pointer (&self->current, queued_schedule_data_unref);
  dex_clear (&self->loop);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ACTIVE]);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_PENDING]);
}

void
bz_transaction_manager_clear_finished (BzTransactionManager *self)
{
  guint    n_items   = 0;
  gboolean had_items = FALSE;

  g_return_if_fail (BZ_IS_TRANSACTION_MANAGER (self));

  n_items   = g_list_model_get_n_items (G_LIST_MODEL (self->transactions));
  had_items = n_items > 0;

  for (guint i = 0; i < n_items;)
    {
      g_autoptr (BzTransaction) transaction = NULL;
      gboolean finished                     = FALSE;

      transaction = g_list_model_get_item (G_LIST_MODEL (self->transactions), i);
      g_object_get (transaction, "finished", &finished, NULL);

      if (finished)
        {
          g_list_store_remove (self->transactions, i);
          n_items--;
        }
      else
        i++;
    }

  if (had_items && n_items == 0)
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP_HAS_TRANSACTIONS]);
}

static DexFuture *
transaction_fiber (QueuedScheduleData *data)
{
  g_autoptr (BzTransactionManager) self = NULL;
  BzTransaction *transaction            = data->transaction;
  DexPromise    *promise                = data->promise;
  g_autoptr (GError) local_error        = NULL;
  gboolean result                       = FALSE;
  g_autoptr (GListStore) store          = NULL;
  g_autoptr (DexChannel) channel        = NULL;
  g_autoptr (DexFuture) future          = NULL;
  g_autoptr (GHashTable) op_set         = NULL;
  g_autoptr (GHashTable) pending_set    = NULL;
  BzBackend     *chosen_backend         = NULL;
  GHashTableIter iter                   = { 0 };

  bz_weak_get_or_return_reject (self, data->self);

  g_object_set (
      transaction,
      "status", "Starting up...",
      "progress", 0.0,
      NULL);

  self->current_progress = 0.0;
  self->pending          = TRUE;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ACTIVE]);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_PENDING]);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_CURRENT_PROGRESS]);

  store = g_list_store_new (BZ_TYPE_TRANSACTION);
  g_list_store_append (store, transaction);

  {
    GListModel *installs            = NULL;
    GListModel *removals            = NULL;
    g_autoptr (BzEntry) first_entry = NULL;

    installs = bz_transaction_get_installs (transaction);
    removals = bz_transaction_get_removals (transaction);

    if (installs != NULL && g_list_model_get_n_items (installs) > 0)
      first_entry = g_list_model_get_item (installs, 0);
    else if (removals != NULL && g_list_model_get_n_items (removals) > 0)
      first_entry = g_list_model_get_item (removals, 0);

    if (first_entry != NULL &&
        self->gnome_ext_backend != NULL &&
        BZ_IS_GNOME_EXTENSION_ENTRY (first_entry))
      chosen_backend = self->gnome_ext_backend;
    else
      chosen_backend = self->backend;
  }

  channel = dex_channel_new (0);
  future  = bz_backend_merge_and_schedule_transactions (
      chosen_backend,
      G_LIST_MODEL (store),
      channel,
      dex_promise_get_cancellable (promise));

  op_set      = g_hash_table_new_full (g_direct_hash, g_direct_equal, g_object_unref, NULL);
  pending_set = g_hash_table_new_full (g_direct_hash, g_direct_equal, g_object_unref, NULL);
  for (;;)
    {
      g_autoptr (GObject) object = NULL;

      object = dex_await_object (dex_channel_receive (channel), NULL);
      if (object == NULL)
        break;

      if (BZ_IS_BACKEND_TRANSACTION_OP_PAYLOAD (object))
        {
          if (g_hash_table_contains (op_set, object))
            {
              g_autofree char *error = NULL;

              error = g_object_steal_data (object, "error");
              if (error != NULL)
                bz_transaction_error_out_task (
                    transaction, BZ_BACKEND_TRANSACTION_OP_PAYLOAD (object), error);
              else
                bz_transaction_finish_task (
                    transaction, BZ_BACKEND_TRANSACTION_OP_PAYLOAD (object));
              g_hash_table_remove (op_set, object);

              if (g_hash_table_contains (pending_set, object))
                {
                  g_hash_table_remove (pending_set, object);
                  self->pending = g_hash_table_size (pending_set) ==
                                  g_hash_table_size (op_set);
                  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_PENDING]);
                }
            }
          else
            {
              bz_transaction_add_task (
                  transaction, BZ_BACKEND_TRANSACTION_OP_PAYLOAD (object));
              g_hash_table_replace (op_set, g_object_ref (object), NULL);
            }
        }
      else if (BZ_IS_BACKEND_TRANSACTION_OP_PROGRESS_PAYLOAD (object))
        {
          const char *status         = NULL;
          gboolean    is_estimating  = FALSE;
          double      total_progress = 0.0;

          bz_transaction_update_task (
              transaction, BZ_BACKEND_TRANSACTION_OP_PROGRESS_PAYLOAD (object));

          status = bz_backend_transaction_op_progress_payload_get_status (
              BZ_BACKEND_TRANSACTION_OP_PROGRESS_PAYLOAD (object));
          is_estimating = bz_backend_transaction_op_progress_payload_get_is_estimating (
              BZ_BACKEND_TRANSACTION_OP_PROGRESS_PAYLOAD (object));
          total_progress = bz_backend_transaction_op_progress_payload_get_total_progress (
              BZ_BACKEND_TRANSACTION_OP_PROGRESS_PAYLOAD (object));

          g_object_set (
              transaction,
              "pending", is_estimating,
              "status", status,
              "progress", total_progress,
              NULL);

          self->current_progress = total_progress;
          g_object_notify_by_pspec (G_OBJECT (self), props[PROP_CURRENT_PROGRESS]);

          if (is_estimating && !g_hash_table_contains (pending_set, object))
            {
              g_hash_table_replace (pending_set, g_object_ref (object), NULL);
              self->pending = g_hash_table_size (pending_set) ==
                              g_hash_table_size (op_set);
              g_object_notify_by_pspec (G_OBJECT (self), props[PROP_PENDING]);
            }
          else if (!is_estimating && g_hash_table_contains (pending_set, object))
            {
              g_hash_table_remove (pending_set, object);
              self->pending = g_hash_table_size (pending_set) ==
                              g_hash_table_size (op_set);
              g_object_notify_by_pspec (G_OBJECT (self), props[PROP_PENDING]);
            }
        }
    }

  /* Finish off tasks that may not have received a final update */
  g_hash_table_iter_init (&iter, op_set);
  for (;;)
    {
      BzBackendTransactionOpPayload *payload = NULL;
      gpointer                       dummy   = NULL;

      if (!g_hash_table_iter_next (
              &iter,
              (gpointer *) &payload,
              (gpointer *) &dummy))
        break;

      bz_transaction_error_out_task (transaction, payload, "Cancelled");
    }

  result = dex_await (g_steal_pointer (&future), &local_error);
  if (!result)
    return dex_future_new_for_error (g_steal_pointer (&local_error));

  return dex_future_new_true ();
}

static DexFuture *
transaction_finally (DexFuture          *future,
                     QueuedScheduleData *data)
{
  g_autoptr (BzTransactionManager) self = NULL;
  g_autoptr (GError) local_error        = NULL;
  BzTransaction   *transaction          = data->transaction;
  DexPromise      *promise              = data->promise;
  GTimer          *timer                = data->timer;
  const GValue    *value                = NULL;
  g_autofree char *status               = NULL;

  bz_weak_get_or_return_reject (self, data->self);

  g_timer_stop (timer);
  status = g_strdup_printf (
      _ ("Finished in %.02f seconds"),
      g_timer_elapsed (data->timer, NULL));

  value = dex_future_get_value (future, &local_error);
  g_object_set (
      transaction,
      "status", status,
      "progress", 1.0,
      "finished", TRUE,
      "success", value != NULL,
      "error", local_error != NULL ? local_error->message : NULL,
      NULL);

  self->current_progress = 1.0;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_CURRENT_PROGRESS]);

  if (value != NULL)
    {
      g_signal_emit (self, signals[SIGNAL_SUCCESS], 0, transaction);
      dex_promise_resolve_boolean (promise, TRUE);
    }
  else
    {
      g_warning ("Transaction failed to complete: %s", local_error->message);
      g_signal_emit (self, signals[SIGNAL_FAILURE], 0, transaction);
      dex_promise_resolve_boolean (promise, FALSE);
    }

  return dex_future_new_true ();
}

static DexFuture *
then_loop_cb (DexFuture *future,
              GWeakRef  *wr)
{
  g_autoptr (BzTransactionManager) self = NULL;

  bz_weak_get_or_return_reject (self, wr);
  g_clear_pointer (&self->current, queued_schedule_data_unref);
  return dispatch_next (self);
}

static DexFuture *
dispatch_next (BzTransactionManager *self)
{
  g_autoptr (QueuedScheduleData) data = NULL;
  g_autoptr (DexFuture) future        = NULL;

  if (self->queue.length == 0 || self->paused)
    {
      dex_clear (&self->loop);
      g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ACTIVE]);
      g_object_notify_by_pspec (G_OBJECT (self), props[PROP_PENDING]);

      if (self->queue.length == 0)
        return dex_future_new_reject (G_IO_ERROR, G_IO_ERROR_UNKNOWN, "No more futures in queue");
      if (self->paused)
        return dex_future_new_reject (G_IO_ERROR, G_IO_ERROR_UNKNOWN, "Paused");
    }

  if (self->current != NULL)
    {
      QueuedScheduleData *peek = NULL;

      peek = g_queue_peek_head (&self->queue);
      return dex_ref (peek->promise);
    }

  data = g_queue_pop_tail (&self->queue);

  g_clear_pointer (&data->timer, g_timer_destroy);
  data->timer = g_timer_new ();

  future = dex_scheduler_spawn (
      dex_scheduler_get_default (),
      bz_get_dex_stack_size (),
      (DexFiberFunc) transaction_fiber,
      queued_schedule_data_ref (data),
      queued_schedule_data_unref);
  future = dex_future_finally (
      future, (DexFutureCallback) transaction_finally,
      queued_schedule_data_ref (data),
      queued_schedule_data_unref);
  future = dex_future_first (
      future,
      dex_ref (data->promise),
      NULL);

  self->current = queued_schedule_data_ref (data);

  if (self->loop == NULL)
    self->loop = dex_future_then_loop (
        dex_ref (future),
        (DexFutureCallback) then_loop_cb,
        bz_track_weak (self),
        bz_weak_release);

  return dex_ref (future);
}

static inline void
finish_queued_schedule_data (gpointer ptr)
{
  QueuedScheduleData *data = ptr;

  g_clear_pointer (&data->self, bz_weak_release);

  if (data->transaction != NULL)
    bz_transaction_release (data->transaction);
  g_clear_object (&data->transaction);

  if (data->promise != NULL &&
      dex_future_is_pending (DEX_FUTURE (data->promise)))
    dex_promise_reject (
        data->promise,
        g_error_new (G_IO_ERROR,
                     G_IO_ERROR_CANCELLED,
                     "User data was destroyed"));
  dex_clear (&data->promise);

  g_clear_pointer (&data->timer, g_timer_destroy);
}
