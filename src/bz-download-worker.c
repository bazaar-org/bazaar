/* bz-download-worker.c
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

#include "config.h"

#include "bz-download-worker.h"
#include "env.h"
#include "util.h"

struct _BzDownloadWorker
{
  GObject parent_instance;

  char *name;

  GSubprocess *subprocess;
  GHashTable  *waiting;
  GMutex       read_mutex;
  DexFuture   *task;

  BzGuard *write_gate;
  GMutex   write_mutex;
};

static void
initable_iface_init (GInitableIface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (
    BzDownloadWorker,
    bz_download_worker,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, initable_iface_init));

enum
{
  PROP_0,

  PROP_NAME,

  LAST_PROP
};
static GParamSpec *props[LAST_PROP] = { 0 };

static DexFuture *
monitor_worker_fiber (GWeakRef *wr);

BZ_DEFINE_DATA (
    invoke_worker,
    InvokeWorker,
    {
      GWeakRef   *self;
      DexPromise *promise;
      GFile      *src;
      GFile      *dest;
    },
    BZ_RELEASE_DATA (self, bz_weak_release);
    BZ_RELEASE_DATA (promise, dex_unref);
    BZ_RELEASE_DATA (src, g_object_unref);
    BZ_RELEASE_DATA (dest, g_object_unref));
static DexFuture *
invoke_worker_fiber (InvokeWorkerData *data);

static void
terminate (BzDownloadWorker *self);

static void
plumb_data_input_stream_read_line_async (GDataInputStream   *stream,
                                         GCancellable       *cancellable,
                                         GAsyncReadyCallback callback,
                                         gpointer            user_data);

static char *
plumb_data_input_stream_read_line_finish (GDataInputStream *stream,
                                          GAsyncResult     *result,
                                          gpointer          user_data);

static GMutex     default_worker_mutex = { 0 };
static GPtrArray *default_workers      = NULL;
static guint      next_default_worker  = 0;

static void
bz_download_worker_dispose (GObject *object)
{
  BzDownloadWorker *self = BZ_DOWNLOAD_WORKER (object);

  terminate (self);

  dex_clear (&self->task);
  g_clear_object (&self->subprocess);

  g_mutex_clear (&self->write_mutex);
  bz_clear_guard (&self->write_gate);

  g_mutex_clear (&self->read_mutex);
  g_clear_pointer (&self->waiting, g_hash_table_unref);
  g_clear_pointer (&self->name, g_free);

  G_OBJECT_CLASS (bz_download_worker_parent_class)->dispose (object);
}

static void
bz_download_worker_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  BzDownloadWorker *self = BZ_DOWNLOAD_WORKER (object);

  switch (prop_id)
    {
    case PROP_NAME:
      g_value_set_string (value, bz_download_worker_get_name (self));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_download_worker_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  BzDownloadWorker *self = BZ_DOWNLOAD_WORKER (object);

  switch (prop_id)
    {
    case PROP_NAME:
      bz_download_worker_set_name (self, g_value_get_string (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_download_worker_class_init (BzDownloadWorkerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = bz_download_worker_set_property;
  object_class->get_property = bz_download_worker_get_property;
  object_class->dispose      = bz_download_worker_dispose;

  props[PROP_NAME] =
      g_param_spec_string (
          "name",
          NULL, NULL, NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, LAST_PROP, props);
}

static void
bz_download_worker_init (BzDownloadWorker *self)
{
  g_mutex_init (&self->read_mutex);
  g_mutex_init (&self->write_mutex);

  self->waiting = g_hash_table_new_full (
      g_str_hash, g_str_equal, g_free, dex_unref);
}

static gboolean
bz_download_worker_initable_init (GInitable    *initable,
                                  GCancellable *cancellable,
                                  GError      **error)
{
  BzDownloadWorker *self = BZ_DOWNLOAD_WORKER (initable);

  self->subprocess = g_subprocess_new (
      G_SUBPROCESS_FLAGS_STDIN_PIPE |
          G_SUBPROCESS_FLAGS_STDOUT_PIPE,
      error,
      DL_WORKER_BIN_NAME, NULL);
  if (self->subprocess == NULL)
    return FALSE;

  self->task = dex_scheduler_spawn (
      dex_scheduler_get_default (),
      bz_get_dex_stack_size (),
      (DexFiberFunc) monitor_worker_fiber,
      bz_track_weak (self), bz_weak_release);

  return TRUE;
}

static void
initable_iface_init (GInitableIface *iface)
{
  iface->init = bz_download_worker_initable_init;
}

BzDownloadWorker *
bz_download_worker_new (const char *name,
                        GError    **error)
{
  return g_initable_new (
      BZ_TYPE_DOWNLOAD_WORKER,
      NULL, error,
      "name", name,
      NULL);
}

const char *
bz_download_worker_get_name (BzDownloadWorker *self)
{
  g_return_val_if_fail (BZ_IS_DOWNLOAD_WORKER (self), NULL);
  return self->name;
}

void
bz_download_worker_set_name (BzDownloadWorker *self,
                             const char       *name)
{
  g_return_if_fail (BZ_IS_DOWNLOAD_WORKER (self));

  g_clear_pointer (&self->name, g_free);
  if (name != NULL)
    self->name = g_strdup (name);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_NAME]);
}

DexFuture *
bz_download_worker_invoke (BzDownloadWorker *self,
                           GFile            *src,
                           GFile            *dest)
{
  g_autoptr (DexPromise) promise    = NULL;
  g_autoptr (InvokeWorkerData) data = NULL;

  dex_return_error_if_fail (BZ_IS_DOWNLOAD_WORKER (self));
  dex_return_error_if_fail (G_IS_FILE (src));
  dex_return_error_if_fail (G_IS_FILE (dest));

  promise = dex_promise_new ();

  data          = invoke_worker_data_new ();
  data->self    = bz_track_weak (self);
  data->promise = dex_ref (promise);
  data->src     = g_object_ref (src);
  data->dest    = g_object_ref (dest);

  dex_future_disown (dex_scheduler_spawn (
      dex_scheduler_get_default (),
      bz_get_dex_stack_size (),
      (DexFiberFunc) invoke_worker_fiber,
      invoke_worker_data_ref (data),
      invoke_worker_data_unref));
  return DEX_FUTURE (g_steal_pointer (&promise));
}

BzDownloadWorker *
bz_download_worker_get_default (void)
{
  g_autoptr (GMutexLocker) locker = NULL;
  BzDownloadWorker *ret           = NULL;

  locker = g_mutex_locker_new (&default_worker_mutex);

  if (default_workers == NULL)
    {
      default_workers = g_ptr_array_new_with_free_func (g_object_unref);

      for (guint i = 0; i < bz_get_n_download_workers (); i++)
        {
          g_autoptr (GError) local_error      = NULL;
          g_autoptr (BzDownloadWorker) worker = NULL;

          worker = bz_download_worker_new ("default", &local_error);
          if (worker == NULL)
            g_warning ("FATAL!!! The default download worker could not be spawned: %s",
                       local_error->message);
          g_assert (worker != NULL);

          g_ptr_array_add (default_workers, g_steal_pointer (&worker));
        }
    }

  /* Check if any of the subprocesses need to be recreated */
  for (guint i = 0; i < default_workers->len; i++)
    {
      BzDownloadWorker **loc = NULL;

      loc = (BzDownloadWorker **) &g_ptr_array_index (default_workers, i);
      if (g_subprocess_get_identifier ((*loc)->subprocess) == NULL)
        {
          g_autoptr (GError) local_error      = NULL;
          g_autoptr (BzDownloadWorker) worker = NULL;

          g_clear_object (loc);

          worker = bz_download_worker_new ("default", &local_error);
          if (worker == NULL)
            g_warning ("FATAL!!! The default download worker could not be spawned: %s",
                       local_error->message);
          g_assert (worker != NULL);

          *loc = g_steal_pointer (&worker);
        }
    }

  ret                 = g_ptr_array_index (default_workers, next_default_worker);
  next_default_worker = (next_default_worker + 1) % default_workers->len;

  return ret;
}

void
bz_reap_default_download_workers (void)
{
  g_autoptr (GMutexLocker) locker = NULL;

  locker = g_mutex_locker_new (&default_worker_mutex);
  if (default_workers == NULL)
    return;

  for (guint i = 0; i < default_workers->len; i++)
    {
      BzDownloadWorker *worker = NULL;

      worker = g_ptr_array_index (default_workers, i);
      g_subprocess_force_exit (worker->subprocess);
    }
  g_clear_pointer (&default_workers, g_ptr_array_unref);

  next_default_worker = 0;
}

static DexFuture *
monitor_worker_fiber (GWeakRef *wr)
{
  g_autoptr (BzDownloadWorker) self              = NULL;
  g_autoptr (GInputStream) input_stream          = NULL;
  g_autoptr (GDataInputStream) subprocess_stdout = NULL;

  bz_weak_get_or_return_reject (self, wr);
  input_stream      = g_object_ref (g_subprocess_get_stdout_pipe (self->subprocess));
  subprocess_stdout = g_data_input_stream_new (g_object_ref (input_stream));
  g_clear_object (&self);

  for (;;)
    {
      g_autoptr (GError) local_error = NULL;
      g_autofree char *line          = NULL;

      line = dex_await_string (
          dex_async_pair_new (
              subprocess_stdout,
              &DEX_ASYNC_PAIR_INFO_STRING (
                  plumb_data_input_stream_read_line_async,
                  plumb_data_input_stream_read_line_finish)),
          &local_error);
      if (line == NULL)
        {
          if (local_error != NULL)
            g_warning ("Could not read stdout from download worker subprocess: %s",
                       local_error->message);
          goto err;
        }

      do
        {
          g_autoptr (GVariant) variant = NULL;
          g_autofree char *dest_path   = NULL;
          gboolean         success     = FALSE;
          DexPromise      *promise     = NULL;

          if (line == NULL)
            {
              line = g_data_input_stream_read_line_utf8 (subprocess_stdout, NULL, NULL, &local_error);
              if (line == NULL)
                {
                  if (local_error != NULL)
                    g_warning ("Could not read stdout from download worker subprocess: %s",
                               local_error->message);
                  goto err;
                }
            }

          variant = g_variant_parse (G_VARIANT_TYPE ("(sb)"),
                                     line, NULL, NULL, &local_error);
          if (variant == NULL)
            {
              g_warning ("Could not interpret stdout from download worker subprocess: %s",
                         local_error->message);
              goto err;
            }
          g_variant_get (variant, "(sb)", &dest_path, &success);

          bz_weak_get_or_return_reject (self, wr);
          g_mutex_lock (&self->read_mutex);

          promise = g_hash_table_lookup (self->waiting, dest_path);
          if (promise != NULL)
            {
              if (success)
                dex_promise_resolve_boolean (promise, TRUE);
              else
                dex_promise_reject (
                    promise,
                    g_error_new (G_IO_ERROR,
                                 G_IO_ERROR_UNKNOWN,
                                 "The subprocess reported an error downloading '%s'", dest_path));
              promise = NULL;
              g_hash_table_remove (self->waiting, dest_path);
            }

          g_mutex_unlock (&self->read_mutex);
          g_clear_object (&self);

          g_clear_pointer (&line, g_free);
        }
      while (g_input_stream_has_pending (input_stream));
    }

  return dex_future_new_true ();

err:
  bz_weak_get_or_return_reject (self, wr);

  /* give up on this subprocess and wait to be disposed */
  g_mutex_lock (&self->read_mutex);
  terminate (self);
  g_mutex_unlock (&self->read_mutex);

  return dex_future_new_false ();
}

static DexFuture *
invoke_worker_fiber (InvokeWorkerData *data)
{
  DexPromise *promise                    = data->promise;
  GFile      *src                        = data->src;
  GFile      *dest                       = data->dest;
  g_autoptr (BzDownloadWorker) self      = NULL;
  g_autoptr (GError) local_error         = NULL;
  g_autoptr (BzGuard) guard              = NULL;
  g_autofree char *src_uri               = NULL;
  g_autofree char *dest_path             = NULL;
  DexPromise      *existing              = NULL;
  g_autoptr (GVariant) variant           = NULL;
  g_autoptr (GString) output             = NULL;
  g_autoptr (GOutputStream) stdin_stream = NULL;
  gint64 bytes_written                   = -1;

  src_uri   = g_file_get_uri (src);
  dest_path = g_file_get_path (dest);

  bz_weak_get_or_return_reject (self, data->self);
  g_mutex_lock (&self->read_mutex);
  existing = g_hash_table_lookup (self->waiting, dest_path);
  if (existing != NULL)
    {
      dex_promise_reject (
          existing,
          g_error_new (G_IO_ERROR,
                       G_IO_ERROR_CANCELLED,
                       "The operation was replaced"));
      existing = NULL;
    }
  g_hash_table_replace (self->waiting, g_strdup (dest_path), dex_ref (promise));
  g_mutex_unlock (&self->read_mutex);

  variant = g_variant_new ("(ss)", src_uri, dest_path);
  output  = g_string_new (NULL);
  output  = g_variant_print_string (variant, g_steal_pointer (&output), TRUE);
  g_string_append_c (output, '\n');

  stdin_stream = g_object_ref (g_subprocess_get_stdin_pipe (self->subprocess));

  BZ_BEGIN_GUARD_WITH_CONTEXT (&guard, &self->write_mutex, &self->write_gate);
  g_clear_object (&self);

  bytes_written = dex_await_int64 (
      dex_future_first (
          dex_output_stream_write (
              stdin_stream,
              output->str,
              output->len,
              G_PRIORITY_DEFAULT_IDLE),
          dex_ref (promise),
          NULL),
      &local_error);
  bz_clear_guard (&guard);

  /* Check if we've been cancelled */
  if (!dex_future_is_pending (DEX_FUTURE (promise)))
    return dex_future_new_false ();

  if (bytes_written < 0)
    {
      bz_weak_get_or_return_reject (self, data->self);
      g_mutex_lock (&self->read_mutex);

      g_hash_table_remove (self->waiting, dest_path);
      dex_promise_reject (promise, g_steal_pointer (&local_error));

      g_mutex_unlock (&self->read_mutex);
      g_clear_object (&self);
    }

  return dex_future_new_true ();
}

static void
terminate (BzDownloadWorker *self)
{
  GHashTableIter waiting_iter = { 0 };

  g_hash_table_iter_init (&waiting_iter, self->waiting);
  for (;;)
    {
      g_autofree char *dest_path     = NULL;
      g_autoptr (DexPromise) promise = NULL;

      if (!g_hash_table_iter_next (
              &waiting_iter,
              (gpointer *) &dest_path,
              (gpointer *) &promise))
        break;
      g_hash_table_iter_steal (&waiting_iter);

      dex_promise_reject (
          promise,
          g_error_new (G_IO_ERROR,
                       G_IO_ERROR_CANCELLED,
                       "The subprocess was terminated"));
    }
}

static void
plumb_data_input_stream_read_line_async (GDataInputStream   *stream,
                                         GCancellable       *cancellable,
                                         GAsyncReadyCallback callback,
                                         gpointer            user_data)
{
  g_data_input_stream_read_line_async (
      stream,
      G_PRIORITY_DEFAULT_IDLE,
      cancellable,
      callback,
      user_data);
}

static char *
plumb_data_input_stream_read_line_finish (GDataInputStream *stream,
                                          GAsyncResult     *result,
                                          gpointer          user_data)
{
  return g_data_input_stream_read_line_finish_utf8 (
      stream,
      result,
      NULL,
      user_data);
}

/* End of bz-download-worker.c */
