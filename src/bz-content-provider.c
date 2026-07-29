/* bz-content-provider.c
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

#define G_LOG_DOMAIN "BAZAAR::CONTENT-PROVIDER"

#include "config.h"

#include <gtk/gtk.h>
#include <libdex.h>

#include "bz-content-provider.h"
#include "env.h"
#include "io.h"
#include "util.h"

struct _BzContentProvider
{
  GObject parent_instance;

  GListModel *input_files;
  BzParser   *parser;

  GListStore *input_mirror;
  GHashTable *input_tracking;

  GListStore          *outputs;
  GtkFlattenListModel *impl_model;
};

static void list_model_iface_init (GListModelInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (
    BzContentProvider,
    bz_content_provider,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, list_model_iface_init))

enum
{
  PROP_0,

  PROP_INPUT_FILES,
  PROP_PARSER,
  PROP_HAS_INPUTS,

  LAST_PROP
};
static GParamSpec *props[LAST_PROP] = { 0 };

BZ_DEFINE_DATA (
    input_init,
    InputInit,
    { GFile *file; },
    BZ_RELEASE_DATA (file, g_object_unref))
static DexFuture *
input_init_fiber (InputInitData *data);

BZ_DEFINE_DATA (
    input_load,
    InputLoad,
    {
      GFile    *file;
      BzParser *parser;
    },
    BZ_RELEASE_DATA (file, g_object_unref);
    BZ_RELEASE_DATA (parser, g_object_unref))
static DexFuture *
input_load_fiber (InputLoadData *data);

BZ_DEFINE_DATA (
    input_tracking,
    InputTracking,
    {
      GMutex        mutex;
      GWeakRef      self;
      char         *path;
      GFileMonitor *monitor;
      GListStore   *output;
      DexFuture    *init;
      DexFuture    *task;
    },
    g_mutex_clear (&self->mutex);
    g_weak_ref_clear (&self->self);
    BZ_RELEASE_DATA (path, g_free);
    BZ_RELEASE_DATA (monitor, g_object_unref);
    BZ_RELEASE_DATA (output, g_object_unref);
    BZ_RELEASE_DATA (init, dex_unref);
    BZ_RELEASE_DATA (task, dex_unref))
static DexFuture *
input_init_finally (DexFuture         *future,
                    InputTrackingData *data);
static DexFuture *
input_load_finally (DexFuture         *future,
                    InputTrackingData *data);

static void
impl_model_changed (BzContentProvider *self,
                    guint              position,
                    guint              removed,
                    guint              added,
                    GListModel        *impl_model);

static void
input_files_changed (BzContentProvider *self,
                     guint              position,
                     guint              removed,
                     guint              added,
                     GListModel        *input_files);

static void
input_file_changed_on_disk (InputTrackingData *data,
                            GFile             *file,
                            GFile             *other_file,
                            GFileMonitorEvent  event_type,
                            GFileMonitor      *monitor);

static gboolean
commence_reload (InputTrackingData *data);

static void
bz_content_provider_dispose (GObject *object)
{
  BzContentProvider *self = BZ_CONTENT_PROVIDER (object);

  g_clear_object (&self->input_files);
  g_clear_object (&self->parser);
  g_clear_object (&self->input_mirror);
  g_clear_pointer (&self->input_tracking, g_hash_table_unref);
  g_clear_object (&self->outputs);
  g_clear_object (&self->impl_model);

  G_OBJECT_CLASS (bz_content_provider_parent_class)->dispose (object);
}

static void
bz_content_provider_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  BzContentProvider *self = BZ_CONTENT_PROVIDER (object);

  switch (prop_id)
    {
    case PROP_INPUT_FILES:
      g_value_set_object (value, bz_content_provider_get_input_files (self));
      break;
    case PROP_PARSER:
      g_value_set_object (value, bz_content_provider_get_parser (self));
      break;
    case PROP_HAS_INPUTS:
      g_value_set_boolean (value, bz_content_provider_get_has_inputs (self));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_content_provider_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  BzContentProvider *self = BZ_CONTENT_PROVIDER (object);

  switch (prop_id)
    {
    case PROP_INPUT_FILES:
      bz_content_provider_set_input_files (self, g_value_get_object (value));
      break;
    case PROP_PARSER:
      bz_content_provider_set_parser (self, g_value_get_object (value));
      break;
    case PROP_HAS_INPUTS:
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_content_provider_class_init (BzContentProviderClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = bz_content_provider_set_property;
  object_class->get_property = bz_content_provider_get_property;
  object_class->dispose      = bz_content_provider_dispose;

  props[PROP_INPUT_FILES] =
      g_param_spec_object (
          "input-files",
          NULL, NULL,
          G_TYPE_LIST_MODEL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_PARSER] =
      g_param_spec_object (
          "parser",
          NULL, NULL,
          BZ_TYPE_PARSER,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_HAS_INPUTS] =
      g_param_spec_boolean (
          "has-inputs",
          NULL, NULL, FALSE,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, LAST_PROP, props);
}

static void
bz_content_provider_init (BzContentProvider *self)
{
  self->input_mirror   = g_list_store_new (G_TYPE_FILE);
  self->input_tracking = g_hash_table_new_full (
      g_direct_hash, g_direct_equal,
      g_object_unref, input_tracking_data_unref);

  self->outputs    = g_list_store_new (G_TYPE_LIST_MODEL);
  self->impl_model = gtk_flatten_list_model_new (g_object_ref (G_LIST_MODEL (self->outputs)));

  g_signal_connect_swapped (
      self->impl_model,
      "items-changed",
      G_CALLBACK (impl_model_changed),
      self);
}

static GType
list_model_get_item_type (GListModel *list)
{
  return G_TYPE_OBJECT;
}

static guint
list_model_get_n_items (GListModel *list)
{
  BzContentProvider *self = BZ_CONTENT_PROVIDER (list);
  return g_list_model_get_n_items (G_LIST_MODEL (self->impl_model));
}

static gpointer
list_model_get_item (GListModel *list,
                     guint       position)
{
  BzContentProvider *self = BZ_CONTENT_PROVIDER (list);
  return g_list_model_get_item (G_LIST_MODEL (self->impl_model), position);
}

static void
list_model_iface_init (GListModelInterface *iface)
{
  iface->get_item_type = list_model_get_item_type;
  iface->get_n_items   = list_model_get_n_items;
  iface->get_item      = list_model_get_item;
}

BzContentProvider *
bz_content_provider_new (void)
{
  return g_object_new (BZ_TYPE_CONTENT_PROVIDER, NULL);
}

void
bz_content_provider_set_input_files (BzContentProvider *self,
                                     GListModel        *input_files)
{
  guint old_length = 0;

  g_return_if_fail (BZ_IS_CONTENT_PROVIDER (self));
  g_return_if_fail (input_files == NULL || G_IS_LIST_MODEL (input_files));

  if (self->input_files != NULL)
    {
      old_length = g_list_model_get_n_items (self->input_files);
      g_signal_handlers_disconnect_by_func (
          self->input_files, input_files_changed, self);
    }
  g_clear_object (&self->input_files);

  g_hash_table_remove_all (self->input_tracking);
  g_list_store_remove_all (self->input_mirror);
  g_list_store_remove_all (self->outputs);

  if (input_files != NULL)
    {
      self->input_files = g_object_ref (input_files);
      g_signal_connect_swapped (
          input_files,
          "items-changed",
          G_CALLBACK (input_files_changed),
          self);

      input_files_changed (
          self, 0, old_length,
          g_list_model_get_n_items (input_files),
          input_files);
    }

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_INPUT_FILES]);
}

GListModel *
bz_content_provider_get_input_files (BzContentProvider *self)
{
  g_return_val_if_fail (BZ_IS_CONTENT_PROVIDER (self), NULL);
  return self->input_files;
}

void
bz_content_provider_set_parser (BzContentProvider *self,
                                BzParser          *parser)
{
  GHashTableIter iter = { 0 };

  g_return_if_fail (BZ_IS_CONTENT_PROVIDER (self));
  g_return_if_fail (parser == NULL || BZ_IS_PARSER (parser));

  g_clear_object (&self->parser);
  if (parser != NULL)
    self->parser = g_object_ref (parser);

  g_hash_table_iter_init (&iter, self->input_tracking);
  for (;;)
    {
      GFile             *file = NULL;
      InputTrackingData *data = NULL;

      if (!g_hash_table_iter_next (
              &iter, (gpointer *) &file, (gpointer *) &data))
        break;

      commence_reload (data);
    }

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_PARSER]);
}

BzParser *
bz_content_provider_get_parser (BzContentProvider *self)
{
  g_return_val_if_fail (BZ_IS_CONTENT_PROVIDER (self), NULL);
  return self->parser;
}

gboolean
bz_content_provider_get_has_inputs (BzContentProvider *self)
{
  g_return_val_if_fail (BZ_IS_CONTENT_PROVIDER (self), FALSE);
  return self->input_files != NULL &&
         g_list_model_get_n_items (self->input_files) > 0 &&
         g_list_model_get_n_items (G_LIST_MODEL (self)) > 0;
}

static void
impl_model_changed (BzContentProvider *self,
                    guint              position,
                    guint              removed,
                    guint              added,
                    GListModel        *impl_model)
{
  g_list_model_items_changed (
      G_LIST_MODEL (self), position, removed, added);
}

static void
input_files_changed (BzContentProvider *self,
                     guint              position,
                     guint              removed,
                     guint              added,
                     GListModel        *input_files)
{
  gboolean                emit_has_inputs = FALSE;
  g_autofree GFile      **additions       = NULL;
  g_autofree GListStore **new_outputs     = NULL;

  emit_has_inputs = g_list_model_get_n_items (input_files) == 0 ||
                    g_list_model_get_n_items (G_LIST_MODEL (self->input_mirror)) == 0;

  if (removed > 0)
    {
      for (guint i = 0; i < removed; i++)
        {
          g_autoptr (GFile) removal = NULL;
          InputTrackingData *data   = NULL;

          removal = g_list_model_get_item (
              G_LIST_MODEL (self->input_mirror), position + i);
          data = g_hash_table_lookup (self->input_tracking, removal);
          g_assert (data != NULL);

          g_mutex_lock (&data->mutex);
          dex_clear (&data->task);
          g_mutex_unlock (&data->mutex);

          g_hash_table_remove (self->input_tracking, removal);
        }
    }

  if (added > 0)
    {
      additions = g_malloc0_n (added, sizeof (*additions));
      for (guint i = 0; i < added; i++)
        additions[i] = g_list_model_get_item (
            G_LIST_MODEL (self->input_files), position + i);

      new_outputs = g_malloc0_n (added, sizeof (*new_outputs));
      for (guint i = 0; i < added; i++)
        new_outputs[i] = g_list_store_new (G_TYPE_OBJECT);
    }

  g_list_store_splice (self->input_mirror, position, removed,
                       (gpointer *) additions, added);
  g_list_store_splice (self->outputs, position, removed,
                       (gpointer *) new_outputs, added);

  for (guint i = 0; i < added; i++)
    {
      g_autoptr (InputInitData) init_data         = NULL;
      g_autoptr (InputTrackingData) tracking_data = NULL;
      g_autoptr (DexFuture) future                = NULL;

      init_data       = input_init_data_new ();
      init_data->file = g_object_ref (additions[i]);

      future = dex_scheduler_spawn (
          bz_get_io_scheduler (),
          bz_get_dex_stack_size (),
          (DexFiberFunc) input_init_fiber,
          input_init_data_ref (init_data),
          input_init_data_unref);

      tracking_data = input_tracking_data_new ();
      g_mutex_init (&tracking_data->mutex);
      g_weak_ref_init (&tracking_data->self, self);
      tracking_data->path   = g_file_get_path (additions[i]);
      tracking_data->output = g_steal_pointer (&new_outputs[i]);

      g_mutex_lock (&tracking_data->mutex);
      future = dex_future_finally (
          future,
          (DexFutureCallback) input_init_finally,
          input_tracking_data_ref (tracking_data),
          input_tracking_data_unref);
      tracking_data->init = g_steal_pointer (&future);
      g_mutex_unlock (&tracking_data->mutex);

      g_hash_table_replace (
          self->input_tracking,
          g_steal_pointer (&additions[i]),
          input_tracking_data_ref (tracking_data));
    }

  if (emit_has_inputs)
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP_HAS_INPUTS]);
}

static void
input_file_changed_on_disk (InputTrackingData *data,
                            GFile             *file,
                            GFile             *other_file,
                            GFileMonitorEvent  event_type,
                            GFileMonitor      *monitor)
{
  if (event_type == G_FILE_MONITOR_EVENT_CHANGED ||
      event_type == G_FILE_MONITOR_EVENT_CREATED ||
      event_type == G_FILE_MONITOR_EVENT_DELETED)
    g_idle_add_full (
        G_PRIORITY_DEFAULT,
        (GSourceFunc) commence_reload,
        input_tracking_data_ref (data),
        input_tracking_data_unref);
}

static DexFuture *
input_init_fiber (InputInitData *data)
{
  GFile *file                      = data->file;
  g_autoptr (GError) local_error   = NULL;
  g_autoptr (GFileMonitor) monitor = NULL;

  monitor = g_file_monitor_file (
      file, G_FILE_MONITOR_NONE, NULL, &local_error);
  if (monitor == NULL)
    return dex_future_new_for_error (g_steal_pointer (&local_error));

  return dex_future_new_for_object (monitor);
}

static DexFuture *
input_init_finally (DexFuture         *future,
                    InputTrackingData *data)
{
  g_autoptr (BzContentProvider) self = NULL;
  g_autoptr (GError) local_error     = NULL;
  g_autoptr (GMutexLocker) locker    = NULL;
  const GValue *value                = NULL;

  locker = g_mutex_locker_new (&data->mutex);

  dex_clear (&data->init);
  bz_weak_get_or_return_reject (self, &data->self);

  g_list_store_remove_all (data->output);

  value = dex_future_get_value (future, &local_error);
  if (value != NULL)
    {
      data->monitor = g_value_dup_object (value);
      g_signal_connect_swapped (
          data->monitor,
          "changed",
          G_CALLBACK (input_file_changed_on_disk),
          data);

      g_clear_pointer (&locker, g_mutex_locker_free);
      commence_reload (data);
    }
  else
    g_warning ("Could not init object watch at path %s: %s",
               data->path, local_error->message);

  return NULL;
}

static DexFuture *
input_load_fiber (InputLoadData *data)
{
  GFile    *file                       = data->file;
  BzParser *parser                     = data->parser;
  g_autoptr (GError) local_error       = NULL;
  g_autoptr (GBytes) bytes             = NULL;
  g_autoptr (GHashTable) parse_results = NULL;
  GObject *object                      = NULL;

  bytes = dex_await_boxed (dex_file_load_contents_bytes (file), &local_error);
  if (bytes == NULL)
    return dex_future_new_for_error (g_steal_pointer (&local_error));

  parse_results = bz_parser_process_bytes (parser, bytes, &local_error);
  if (parse_results == NULL)
    return dex_future_new_for_error (g_steal_pointer (&local_error));

  object = g_value_get_object (g_hash_table_lookup (parse_results, "/"));
  if (object == NULL)
    return dex_future_new_reject (G_IO_ERROR,
                                  G_IO_ERROR_UNKNOWN,
                                  "Parser returned invalid results");

  return dex_future_new_for_object (object);
}

static DexFuture *
input_load_finally (DexFuture         *future,
                    InputTrackingData *data)
{
  g_autoptr (BzContentProvider) self = NULL;
  g_autoptr (GError) local_error     = NULL;
  g_autoptr (GMutexLocker) locker    = NULL;
  const GValue *value                = NULL;

  locker = g_mutex_locker_new (&data->mutex);
  g_list_store_remove_all (data->output);

  bz_weak_get_or_return_reject (self, &data->self);

  value = dex_future_get_value (future, &local_error);
  if (value != NULL)
    g_list_store_append (data->output, g_value_get_object (value));
  else if (local_error->domain != G_IO_ERROR)
    g_warning ("Could not load object at path %s: %s",
               data->path, local_error->message);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_HAS_INPUTS]);
  return NULL;
}

static gboolean
commence_reload (InputTrackingData *data)
{
  g_autoptr (BzContentProvider) self  = NULL;
  g_autoptr (GMutexLocker) locker     = NULL;
  g_autoptr (InputLoadData) load_data = NULL;
  g_autoptr (DexFuture) future        = NULL;

  locker = g_mutex_locker_new (&data->mutex);
  dex_clear (&data->task);

  self = g_weak_ref_get (&data->self);
  if (self == NULL)
    goto done;

  if (self->parser == NULL)
    goto done;

  load_data         = input_load_data_new ();
  load_data->file   = g_file_new_for_path (data->path);
  load_data->parser = g_object_ref (self->parser);

  future = dex_scheduler_spawn (
      bz_get_io_scheduler (),
      bz_get_dex_stack_size (),
      (DexFiberFunc) input_load_fiber,
      input_load_data_ref (load_data),
      input_load_data_unref);
  future = dex_future_finally (
      future,
      (DexFutureCallback) input_load_finally,
      input_tracking_data_ref (data),
      input_tracking_data_unref);
  data->task = g_steal_pointer (&future);

done:
  return G_SOURCE_REMOVE;
}
