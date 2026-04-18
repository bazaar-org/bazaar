/* bz-transaction.c
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

/* TODO: This file probably needs to be rewritten */

#include "config.h"

#include <glib/gi18n.h>

#include "bz-transaction.h"

typedef struct
{
  GListStore *installs;
  GListStore *updates;
  GListStore *removals;

  char       *name;
  gboolean    pending;
  GListStore *current_tasks;
  GListStore *finished_tasks;
  GListStore *trackers;
  char       *status;
  double      progress;
  gboolean    finished;
  gboolean    success;
  char       *error;
} BzTransactionPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (BzTransaction, bz_transaction, G_TYPE_OBJECT)

enum
{
  PROP_0,

  PROP_NAME,
  PROP_INSTALLS,
  PROP_UPDATES,
  PROP_REMOVALS,
  PROP_PENDING,
  PROP_CURRENT_OPS,
  PROP_FINISHED_OPS,
  PROP_TRACKERS,
  PROP_STATUS,
  PROP_PROGRESS,
  PROP_FINISHED,
  PROP_SUCCESS,
  PROP_ERROR,

  LAST_PROP
};
static GParamSpec *props[LAST_PROP] = { 0 };

static void
finish (BzTransactionPrivate *priv);

static gboolean
find_payload_eq_func (BzTransactionTask             *task,
                      gpointer                       none,
                      BzBackendTransactionOpPayload *payload);

static gboolean
find_entry_eq_func (BzTransactionEntryTracker *tracker,
                    gpointer                   none,
                    BzEntry                   *entry);

static gboolean
find_and_maybe_transfer (GListStore    *from,
                         GListStore    *to,
                         gpointer       lookup,
                         GEqualFuncFull eql,
                         gpointer      *out);

static void
tracker_update (BzTransactionPrivate                  *priv,
                BzBackendTransactionOpPayload         *payload,
                BzBackendTransactionOpProgressPayload *progress_payload,
                gboolean                               transfer,
                gboolean                               set_done);

static void
bz_transaction_dispose (GObject *object)
{
  BzTransaction        *self = BZ_TRANSACTION (object);
  BzTransactionPrivate *priv = bz_transaction_get_instance_private (self);

  g_clear_pointer (&priv->name, g_free);
  g_clear_object (&priv->installs);
  g_clear_object (&priv->updates);
  g_clear_object (&priv->removals);
  g_clear_object (&priv->current_tasks);
  g_clear_object (&priv->finished_tasks);
  g_clear_object (&priv->trackers);
  g_clear_pointer (&priv->status, g_free);
  g_clear_pointer (&priv->error, g_free);

  G_OBJECT_CLASS (bz_transaction_parent_class)->dispose (object);
}

static void
bz_transaction_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
  BzTransaction        *self = BZ_TRANSACTION (object);
  BzTransactionPrivate *priv = bz_transaction_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_NAME:
      g_value_set_string (value, priv->name);
      break;
    case PROP_INSTALLS:
      g_value_set_object (value, priv->installs);
      break;
    case PROP_UPDATES:
      g_value_set_object (value, priv->updates);
      break;
    case PROP_REMOVALS:
      g_value_set_object (value, priv->removals);
      break;
    case PROP_PENDING:
      g_value_set_boolean (value, priv->pending);
      break;
    case PROP_CURRENT_OPS:
      g_value_set_object (value, priv->current_tasks);
      break;
    case PROP_FINISHED_OPS:
      g_value_set_object (value, priv->finished_tasks);
      break;
    case PROP_TRACKERS:
      g_value_set_object (value, priv->trackers);
      break;
    case PROP_STATUS:
      g_value_set_string (value, priv->status);
      break;
    case PROP_PROGRESS:
      g_value_set_double (value, priv->progress);
      break;
    case PROP_FINISHED:
      g_value_set_boolean (value, priv->finished);
      break;
    case PROP_SUCCESS:
      g_value_set_boolean (value, priv->success);
      break;
    case PROP_ERROR:
      g_value_set_string (value, priv->error);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_transaction_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
  BzTransaction        *self = BZ_TRANSACTION (object);
  BzTransactionPrivate *priv = bz_transaction_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_NAME:
      g_clear_pointer (&priv->name, g_free);
      priv->name = g_value_dup_string (value);
      break;
    case PROP_INSTALLS:
      g_clear_object (&priv->installs);
      priv->installs = g_value_dup_object (value);
      break;
    case PROP_UPDATES:
      g_clear_object (&priv->updates);
      priv->updates = g_value_dup_object (value);
      break;
    case PROP_REMOVALS:
      g_clear_object (&priv->removals);
      priv->removals = g_value_dup_object (value);
      break;
    case PROP_PENDING:
      priv->pending = g_value_get_boolean (value);
      break;
    case PROP_STATUS:
      g_clear_pointer (&priv->status, g_free);
      priv->status = g_value_dup_string (value);
      break;
    case PROP_PROGRESS:
      priv->progress = g_value_get_double (value);
      break;
    case PROP_FINISHED:
      priv->finished = g_value_get_boolean (value);
      if (priv->finished)
        finish (priv);
      break;
    case PROP_SUCCESS:
      priv->success = g_value_get_boolean (value);
      break;
    case PROP_ERROR:
      g_clear_pointer (&priv->error, g_free);
      priv->error = g_value_dup_string (value);
      break;
    case PROP_CURRENT_OPS:
    case PROP_FINISHED_OPS:
    case PROP_TRACKERS:
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_transaction_class_init (BzTransactionClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = bz_transaction_set_property;
  object_class->get_property = bz_transaction_get_property;
  object_class->dispose      = bz_transaction_dispose;

  props[PROP_NAME] =
      g_param_spec_string (
          "name",
          NULL, NULL, NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  props[PROP_INSTALLS] =
      g_param_spec_object (
          "installs",
          NULL, NULL,
          G_TYPE_LIST_MODEL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  props[PROP_UPDATES] =
      g_param_spec_object (
          "updates",
          NULL, NULL,
          G_TYPE_LIST_MODEL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  props[PROP_REMOVALS] =
      g_param_spec_object (
          "removals",
          NULL, NULL,
          G_TYPE_LIST_MODEL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  props[PROP_PENDING] =
      g_param_spec_boolean (
          "pending",
          NULL, NULL, TRUE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  props[PROP_CURRENT_OPS] =
      g_param_spec_object (
          "current-ops",
          NULL, NULL,
          G_TYPE_LIST_MODEL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  props[PROP_FINISHED_OPS] =
      g_param_spec_object (
          "finished-ops",
          NULL, NULL,
          G_TYPE_LIST_MODEL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  props[PROP_TRACKERS] =
      g_param_spec_object (
          "trackers",
          NULL, NULL,
          G_TYPE_LIST_MODEL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  props[PROP_STATUS] =
      g_param_spec_string (
          "status",
          NULL, NULL, NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  props[PROP_PROGRESS] =
      g_param_spec_double (
          "progress",
          NULL, NULL,
          0.0, 1.0, 0.0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  props[PROP_FINISHED] =
      g_param_spec_boolean (
          "finished",
          NULL, NULL, FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  props[PROP_SUCCESS] =
      g_param_spec_boolean (
          "success",
          NULL, NULL, FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  props[PROP_ERROR] =
      g_param_spec_string (
          "error",
          NULL, NULL, NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, LAST_PROP, props);
}

static void
bz_transaction_init (BzTransaction *self)
{
  BzTransactionPrivate *priv = NULL;
  g_autoptr (GDateTime) now  = NULL;

  priv = bz_transaction_get_instance_private (self);

  now        = g_date_time_new_now_local ();
  priv->name = g_date_time_format (now, "%X");

  priv->installs       = g_list_store_new (BZ_TYPE_ENTRY);
  priv->updates        = g_list_store_new (BZ_TYPE_ENTRY);
  priv->removals       = g_list_store_new (BZ_TYPE_ENTRY);
  priv->pending        = TRUE;
  priv->current_tasks  = g_list_store_new (BZ_TYPE_TRANSACTION_TASK);
  priv->finished_tasks = g_list_store_new (BZ_TYPE_TRANSACTION_TASK);
  priv->trackers       = g_list_store_new (BZ_TYPE_TRANSACTION_ENTRY_TRACKER);
  priv->status         = g_strdup (_ ("Pending"));
  priv->success        = TRUE;
}

BzTransaction *
bz_transaction_new_full (BzEntry **installs,
                         guint     n_installs,
                         BzEntry **updates,
                         guint     n_updates,
                         BzEntry **removals,
                         guint     n_removals)

{
  g_autoptr (BzTransaction) self = NULL;
  BzTransactionPrivate *priv     = NULL;

  g_return_val_if_fail ((installs != NULL && n_installs > 0) ||
                            (updates != NULL && n_updates > 0) ||
                            (removals != NULL && n_removals > 0),
                        NULL);

  for (guint i = 0; i < n_installs; i++)
    g_return_val_if_fail (BZ_IS_ENTRY (installs[i]), NULL);
  for (guint i = 0; i < n_updates; i++)
    g_return_val_if_fail (BZ_IS_ENTRY (updates[i]), NULL);
  for (guint i = 0; i < n_removals; i++)
    g_return_val_if_fail (BZ_IS_ENTRY (removals[i]), NULL);

  self = g_object_new (BZ_TYPE_TRANSACTION, NULL);
  priv = bz_transaction_get_instance_private (self);

#define ADD_ENTRY(type, entry, transaction_type)                                           \
  G_STMT_START                                                                             \
  {                                                                                        \
    g_autoptr (BzTransactionEntryTracker) tracker = NULL;                                  \
    g_autoptr (GListStore) current_ops            = NULL;                                  \
    g_autoptr (GListStore) finished_ops           = NULL;                                  \
                                                                                           \
    g_list_store_append (priv->type, (entry));                                             \
                                                                                           \
    tracker      = bz_transaction_entry_tracker_new ();                                    \
    current_ops  = g_list_store_new (BZ_TYPE_TRANSACTION_TASK);                            \
    finished_ops = g_list_store_new (BZ_TYPE_TRANSACTION_TASK);                            \
                                                                                           \
    bz_transaction_entry_tracker_set_entry (tracker, (entry));                             \
    bz_transaction_entry_tracker_set_current_ops (tracker, G_LIST_MODEL (current_ops));    \
    bz_transaction_entry_tracker_set_finished_ops (tracker, G_LIST_MODEL (finished_ops));  \
    bz_transaction_entry_tracker_set_kind (tracker, transaction_type);                     \
    bz_transaction_entry_tracker_set_status (tracker, BZ_TRANSACTION_ENTRY_STATUS_QUEUED); \
    bz_transaction_entry_tracker_set_pending (tracker, TRUE);                              \
    if ((transaction_type) == BZ_TRANSACTION_ENTRY_KIND_REMOVAL)                           \
      bz_transaction_entry_tracker_set_active (tracker, TRUE);                             \
                                                                                           \
    g_list_store_append (priv->trackers, tracker);                                         \
  }                                                                                        \
  G_STMT_END

  for (guint i = 0; i < n_installs; i++)
    ADD_ENTRY (installs, installs[i], BZ_TRANSACTION_ENTRY_KIND_INSTALL);
  for (guint i = 0; i < n_updates; i++)
    ADD_ENTRY (updates, updates[i], BZ_TRANSACTION_ENTRY_KIND_UPDATE);
  for (guint i = 0; i < n_removals; i++)
    ADD_ENTRY (removals, removals[i], BZ_TRANSACTION_ENTRY_KIND_REMOVAL);

#undef ADD_ENTRY

  return g_steal_pointer (&self);
}

BzTransaction *
bz_transaction_new_merged (BzTransaction **transactions,
                           guint           n_transactions)
{
  g_autoptr (GPtrArray) installs_pa = NULL;
  g_autoptr (GPtrArray) updates_pa  = NULL;
  g_autoptr (GPtrArray) removals_pa = NULL;

  g_return_val_if_fail (transactions != NULL, NULL);
  g_return_val_if_fail (n_transactions >= 2, NULL);

  installs_pa = g_ptr_array_new_with_free_func (g_object_unref);
  updates_pa  = g_ptr_array_new_with_free_func (g_object_unref);
  removals_pa = g_ptr_array_new_with_free_func (g_object_unref);

  for (guint i = 0; i < n_transactions; i++)
    {
      GListModel *installs   = NULL;
      GListModel *updates    = NULL;
      GListModel *removals   = NULL;
      guint       n_installs = 0;
      guint       n_updates  = 0;
      guint       n_removals = 0;

      installs = bz_transaction_get_installs (transactions[i]);
      updates  = bz_transaction_get_updates (transactions[i]);
      removals = bz_transaction_get_removals (transactions[i]);

      if (installs != NULL)
        n_installs = g_list_model_get_n_items (installs);
      if (updates != NULL)
        n_updates = g_list_model_get_n_items (updates);
      if (removals != NULL)
        n_removals = g_list_model_get_n_items (removals);

      for (guint j = 0; j < n_installs; j++)
        g_ptr_array_add (installs_pa, g_list_model_get_item (installs, j));
      for (guint j = 0; j < n_updates; j++)
        g_ptr_array_add (updates_pa, g_list_model_get_item (updates, j));
      for (guint j = 0; j < n_removals; j++)
        g_ptr_array_add (removals_pa, g_list_model_get_item (removals, j));
    }

  return bz_transaction_new_full (
      (BzEntry **) installs_pa->pdata,
      installs_pa->len,
      (BzEntry **) updates_pa->pdata,
      updates_pa->len,
      (BzEntry **) removals_pa->pdata,
      removals_pa->len);
}

GListModel *
bz_transaction_get_installs (BzTransaction *self)
{
  BzTransactionPrivate *priv = NULL;

  g_return_val_if_fail (BZ_IS_TRANSACTION (self), NULL);

  priv = bz_transaction_get_instance_private (self);
  return G_LIST_MODEL (priv->installs);
}

GListModel *
bz_transaction_get_updates (BzTransaction *self)
{
  BzTransactionPrivate *priv = NULL;

  g_return_val_if_fail (BZ_IS_TRANSACTION (self), NULL);

  priv = bz_transaction_get_instance_private (self);
  return G_LIST_MODEL (priv->updates);
}

GListModel *
bz_transaction_get_removals (BzTransaction *self)
{
  BzTransactionPrivate *priv = NULL;

  g_return_val_if_fail (BZ_IS_TRANSACTION (self), NULL);

  priv = bz_transaction_get_instance_private (self);
  return G_LIST_MODEL (priv->removals);
}

GListModel *
bz_transaction_get_trackers (BzTransaction *self)
{
  BzTransactionPrivate *priv = NULL;

  g_return_val_if_fail (BZ_IS_TRANSACTION (self), NULL);

  priv = bz_transaction_get_instance_private (self);
  return G_LIST_MODEL (priv->trackers);
}

void
bz_transaction_hold (BzTransaction *self)
{
  BzTransactionPrivate *priv = NULL;

  g_return_if_fail (BZ_IS_TRANSACTION (self));

  priv = bz_transaction_get_instance_private (self);

#define HOLD_MODEL(_model)                                            \
  if ((_model) != NULL)                                               \
    {                                                                 \
      guint n_items = 0;                                              \
                                                                      \
      n_items = g_list_model_get_n_items (G_LIST_MODEL ((_model)));   \
      for (guint i = 0; i < n_items; i++)                             \
        {                                                             \
          g_autoptr (BzEntry) entry = NULL;                           \
                                                                      \
          entry = g_list_model_get_item (G_LIST_MODEL ((_model)), i); \
          bz_entry_hold (entry);                                      \
        }                                                             \
    }

  HOLD_MODEL (priv->installs);
  HOLD_MODEL (priv->updates);
  HOLD_MODEL (priv->removals);

#undef HOLD_MODEL
}

void
bz_transaction_release (BzTransaction *self)
{
  BzTransactionPrivate *priv = NULL;

  g_return_if_fail (BZ_IS_TRANSACTION (self));

  priv = bz_transaction_get_instance_private (self);

#define RELEASE_MODEL(_model)                                         \
  if ((_model) != NULL)                                               \
    {                                                                 \
      guint n_items = 0;                                              \
                                                                      \
      n_items = g_list_model_get_n_items (G_LIST_MODEL ((_model)));   \
      for (guint i = 0; i < n_items; i++)                             \
        {                                                             \
          g_autoptr (BzEntry) entry = NULL;                           \
                                                                      \
          entry = g_list_model_get_item (G_LIST_MODEL ((_model)), i); \
          bz_entry_release (entry);                                   \
        }                                                             \
    }

  RELEASE_MODEL (priv->installs);
  RELEASE_MODEL (priv->updates);
  RELEASE_MODEL (priv->removals);

#undef RELEASE_MODEL
}

void
bz_transaction_add_task (BzTransaction                 *self,
                         BzBackendTransactionOpPayload *payload)
{
  BzTransactionPrivate *priv                    = NULL;
  g_autoptr (BzTransactionTask) task            = NULL;
  BzEntry *entry                                = NULL;
  g_autoptr (BzTransactionEntryTracker) tracker = NULL;
  gboolean result                               = FALSE;

  g_return_if_fail (BZ_IS_TRANSACTION (self));
  g_return_if_fail (BZ_IS_BACKEND_TRANSACTION_OP_PAYLOAD (payload));

  priv = bz_transaction_get_instance_private (self);

  task = bz_transaction_task_new ();
  bz_transaction_task_set_op (task, payload);

  g_list_store_append (priv->current_tasks, task);

  entry  = bz_backend_transaction_op_payload_get_entry (payload);
  result = find_and_maybe_transfer (
      priv->trackers,
      NULL,
      entry,
      (GEqualFuncFull) find_entry_eq_func,
      (gpointer *) &tracker);
  if (result)
    {
      GListModel *current_ops = NULL;

      if (bz_transaction_entry_tracker_get_kind (tracker) == BZ_TRANSACTION_ENTRY_KIND_UPDATE)
        bz_backend_transaction_op_payload_set_download_size (payload, (guint64) -1);

      current_ops = bz_transaction_entry_tracker_get_current_ops (tracker);
      g_list_store_append (G_LIST_STORE (current_ops), task);
      g_object_notify (G_OBJECT (tracker), "current-ops");
    }
}

void
bz_transaction_update_task (BzTransaction                         *self,
                            BzBackendTransactionOpProgressPayload *payload)
{
  BzTransactionPrivate          *priv = NULL;
  BzBackendTransactionOpPayload *op   = NULL;
  g_autoptr (BzTransactionTask) task  = NULL;
  gboolean result                     = FALSE;

  g_return_if_fail (BZ_IS_TRANSACTION (self));
  g_return_if_fail (BZ_IS_BACKEND_TRANSACTION_OP_PROGRESS_PAYLOAD (payload));

  priv = bz_transaction_get_instance_private (self);
  op   = bz_backend_transaction_op_progress_payload_get_op (payload);

  result = find_and_maybe_transfer (
      priv->current_tasks,
      NULL,
      op,
      (GEqualFuncFull) find_payload_eq_func,
      (gpointer *) &task);
  if (result)
    bz_transaction_task_set_last_progress (task, payload);

  tracker_update (priv, op, payload, FALSE, FALSE);
}

void
bz_transaction_finish_task (BzTransaction                 *self,
                            BzBackendTransactionOpPayload *payload)
{
  BzTransactionPrivate *priv = NULL;

  g_return_if_fail (BZ_IS_TRANSACTION (self));
  g_return_if_fail (BZ_IS_BACKEND_TRANSACTION_OP_PAYLOAD (payload));

  priv = bz_transaction_get_instance_private (self);

  find_and_maybe_transfer (
      priv->current_tasks,
      priv->finished_tasks,
      payload,
      (GEqualFuncFull) find_payload_eq_func,
      NULL);

  tracker_update (priv, payload, NULL, TRUE, FALSE);
}

void
bz_transaction_error_out_task (BzTransaction                 *self,
                               BzBackendTransactionOpPayload *payload,
                               const char                    *message)
{
  BzTransactionPrivate *priv         = NULL;
  gboolean              result       = FALSE;
  g_autoptr (BzTransactionTask) task = NULL;

  g_return_if_fail (BZ_IS_TRANSACTION (self));
  g_return_if_fail (BZ_IS_BACKEND_TRANSACTION_OP_PAYLOAD (payload));
  g_return_if_fail (message != NULL);

  priv = bz_transaction_get_instance_private (self);

  result = find_and_maybe_transfer (
      priv->current_tasks,
      priv->finished_tasks,
      payload,
      (GEqualFuncFull) find_payload_eq_func,
      (gpointer *) &task);
  if (result)
    bz_transaction_task_set_error (task, message);

  tracker_update (priv, payload, NULL, TRUE, TRUE);
}

static void
finish (BzTransactionPrivate *priv)
{
  guint n_trackers = 0;

  n_trackers = g_list_model_get_n_items (G_LIST_MODEL (priv->trackers));
  for (guint i = 0; i < n_trackers; i++)
    {
      g_autoptr (BzTransactionEntryTracker) tracker = NULL;

      tracker = g_list_model_get_item (G_LIST_MODEL (priv->trackers), i);
      bz_transaction_entry_tracker_set_active (tracker, FALSE);
      bz_transaction_entry_tracker_set_pending (tracker, FALSE);
      if (bz_transaction_entry_tracker_get_status (tracker) == BZ_TRANSACTION_ENTRY_STATUS_CANCELLED)
        continue;
      bz_transaction_entry_tracker_set_status (tracker, BZ_TRANSACTION_ENTRY_STATUS_DONE);
    }
}

static gboolean
find_payload_eq_func (BzTransactionTask             *task,
                      gpointer                       none,
                      BzBackendTransactionOpPayload *payload)
{
  return bz_transaction_task_get_op (task) == payload;
}

static gboolean
find_entry_eq_func (BzTransactionEntryTracker *tracker,
                    gpointer                   none,
                    BzEntry                   *entry)
{
  return bz_transaction_entry_tracker_get_entry (tracker) == entry;
}

/* This kinda sucks  */

static gboolean
find_and_maybe_transfer (GListStore    *from,
                         GListStore    *to,
                         gpointer       lookup,
                         GEqualFuncFull eql,
                         gpointer      *out)
{
  guint    op_pos            = 0;
  gboolean found             = FALSE;
  g_autoptr (GObject) object = NULL;

  found = g_list_store_find_with_equal_func_full (from, NULL, eql, lookup, &op_pos);
  if (!found)
    return FALSE;

  object = g_list_model_get_item (G_LIST_MODEL (from), op_pos);

  if (to != NULL)
    {
      /* transfer if `to` is provided */
      g_list_store_remove (from, op_pos);
      g_list_store_append (to, object);
    }

  if (out != NULL)
    *out = g_steal_pointer (&object);

  return TRUE;
}

static void
tracker_update (BzTransactionPrivate                  *priv,
                BzBackendTransactionOpPayload         *payload,
                BzBackendTransactionOpProgressPayload *progress_payload,
                gboolean                               transfer,
                gboolean                               set_done)
{
  BzEntry *entry                                = NULL;
  g_autoptr (BzTransactionEntryTracker) tracker = NULL;
  gboolean result                               = FALSE;

  entry  = bz_backend_transaction_op_payload_get_entry (payload);
  result = find_and_maybe_transfer (
      priv->trackers,
      NULL,
      entry,
      (GEqualFuncFull) find_entry_eq_func,
      (gpointer *) &tracker);

  if (result)
    {
      if (transfer)
        {
          GListModel *from = NULL;
          GListModel *to   = NULL;

          if (set_done)
            {
              bz_transaction_entry_tracker_set_active (tracker, FALSE);
              bz_transaction_entry_tracker_set_status (tracker, BZ_TRANSACTION_ENTRY_STATUS_DONE);
              bz_transaction_entry_tracker_set_pending (tracker, FALSE);
            }

          from   = bz_transaction_entry_tracker_get_current_ops (tracker);
          to     = bz_transaction_entry_tracker_get_finished_ops (tracker);
          result = find_and_maybe_transfer (
              G_LIST_STORE (from),
              G_LIST_STORE (to),
              payload,
              (GEqualFuncFull) find_payload_eq_func,
              NULL);

          g_object_notify (G_OBJECT (tracker), "current-ops");
          g_object_notify (G_OBJECT (tracker), "finished-ops");
        }
      else
        {
          if (progress_payload != NULL)
            {
              double progress = 0.0;
              double scaled   = 0.0;

              progress = bz_backend_transaction_op_progress_payload_get_total_progress (progress_payload);
              scaled   = progress >= 1.0 ? 1.0 : progress * 0.9;

              g_object_set (tracker,
                            "progress", scaled,
                            "pending", bz_backend_transaction_op_progress_payload_get_is_estimating (progress_payload),
                            NULL);
            }
          bz_transaction_entry_tracker_set_active (tracker, TRUE);
          bz_transaction_entry_tracker_set_status (tracker, BZ_TRANSACTION_ENTRY_STATUS_ONGOING);
        }
    }
}
