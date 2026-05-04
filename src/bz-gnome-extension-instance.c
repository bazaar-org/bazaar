/* bz-gnome-extension-instance.c
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

#define G_LOG_DOMAIN  "BAZAAR::GNOME-EXT-INSTANCE"
#define BAZAAR_MODULE "gnome-ext"

#include "config.h"

#include <appstream.h>
#include <gio/gio.h>
#include <xmlb.h>

#include "bz-appstream-parser.h"
#include "bz-backend-notification.h"
#include "bz-backend-transaction-op-payload.h"
#include "bz-backend-transaction-op-progress-payload.h"
#include "bz-env.h"
#include "bz-global-net.h"
#include "bz-gnome-extension-entry.h"
#include "bz-gnome-extension-info.h"
#include "bz-gnome-extension-instance.h"
#include "bz-util.h"

#define APPSTREAM_URL          "https://usebazaar.org/gnome-extensions.xml.gz"
#define SHELL_EXTENSIONS_BUS   "org.gnome.Shell.Extensions"
#define SHELL_EXTENSIONS_PATH  "/org/gnome/Shell/Extensions"
#define SHELL_EXTENSIONS_IFACE "org.gnome.Shell.Extensions"

#pragma GCC diagnostic ignored "-Wunused-result"

struct _BzGnomeExtensionInstance
{
  GObject parent_instance;

  DexScheduler *scheduler;

  GDBusProxy *shell_proxy;

  GMutex     notif_mutex;
  GPtrArray *notif_channels;
  DexFuture *notif_send;
};

static void backend_iface_init (BzBackendInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (
    BzGnomeExtensionInstance,
    bz_gnome_extension_instance,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (BZ_TYPE_BACKEND, backend_iface_init))

BZ_DEFINE_DATA (
    gnome_ext_init,
    GnomeExtInit,
    { BzGnomeExtensionInstance *self; },
    BZ_RELEASE_DATA (self, g_object_unref))

BZ_DEFINE_DATA (
    gnome_ext_gather,
    GnomeExtGather,
    {
      GWeakRef     *self;
      GCancellable *cancellable;
    },
    BZ_RELEASE_DATA (self, bz_weak_release);
    BZ_RELEASE_DATA (cancellable, g_object_unref))

BZ_DEFINE_DATA (
    gnome_ext_installs,
    GnomeExtInstalls,
    {
      GWeakRef     *self;
      GCancellable *cancellable;
    },
    BZ_RELEASE_DATA (self, bz_weak_release);
    BZ_RELEASE_DATA (cancellable, g_object_unref))

BZ_DEFINE_DATA (
    gnome_ext_transaction,
    GnomeExtTransaction,
    {
      GWeakRef     *self;
      GCancellable *cancellable;
      GPtrArray    *installs;
      GPtrArray    *removals;
      DexChannel   *channel;
    },
    BZ_RELEASE_DATA (self, bz_weak_release);
    BZ_RELEASE_DATA (cancellable, g_object_unref);
    BZ_RELEASE_DATA (installs, g_ptr_array_unref);
    BZ_RELEASE_DATA (removals, g_ptr_array_unref);
    BZ_RELEASE_DATA (channel, dex_unref))

static GDBusProxy *get_shell_extensions_proxy (GCancellable *cancellable,
                                               GError      **error);

static void send_notif (BzGnomeExtensionInstance *self,
                        DexChannel               *channel,
                        BzBackendNotification    *notif,
                        gboolean                  lock);

static void send_notif_all (BzGnomeExtensionInstance *self,
                            BzBackendNotification    *notif,
                            gboolean                  lock);

static void on_shell_signal (BzGnomeExtensionInstance *self,
                             const char               *sender_name,
                             const char               *signal_name,
                             GVariant                 *parameters,
                             GDBusProxy               *proxy);

static DexFuture *init_fiber (GnomeExtInitData *data);
static DexFuture *gather_fiber (GnomeExtGatherData *data);
static DexFuture *retrieve_installs_fiber (GnomeExtInstallsData *data);
static DexFuture *transaction_fiber (GnomeExtTransactionData *data);

static void
bz_gnome_extension_instance_dispose (GObject *object)
{
  BzGnomeExtensionInstance *self = BZ_GNOME_EXTENSION_INSTANCE (object);

  dex_clear (&self->scheduler);
  g_clear_object (&self->shell_proxy);
  g_clear_pointer (&self->notif_channels, g_ptr_array_unref);
  dex_clear (&self->notif_send);
  g_mutex_clear (&self->notif_mutex);

  G_OBJECT_CLASS (bz_gnome_extension_instance_parent_class)->dispose (object);
}

static void
bz_gnome_extension_instance_class_init (BzGnomeExtensionInstanceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  object_class->dispose      = bz_gnome_extension_instance_dispose;
}

static void
bz_gnome_extension_instance_init (BzGnomeExtensionInstance *self)
{
  self->scheduler      = dex_thread_pool_scheduler_new ();
  self->notif_channels = g_ptr_array_new_with_free_func (dex_unref);
  g_mutex_init (&self->notif_mutex);
}

static DexChannel *
bz_gnome_extension_instance_create_notification_channel (BzBackend *backend)
{
  BzGnomeExtensionInstance *self = BZ_GNOME_EXTENSION_INSTANCE (backend);
  g_autoptr (DexChannel) channel = NULL;

  channel = dex_channel_new (0);

  g_mutex_lock (&self->notif_mutex);
  g_ptr_array_add (self->notif_channels, dex_ref (channel));
  g_mutex_unlock (&self->notif_mutex);

  return g_steal_pointer (&channel);
}

static DexFuture *
bz_gnome_extension_instance_retrieve_remote_entries (BzBackend    *backend,
                                                     GCancellable *cancellable)
{
  BzGnomeExtensionInstance *self      = BZ_GNOME_EXTENSION_INSTANCE (backend);
  g_autoptr (GnomeExtGatherData) data = NULL;

  data              = gnome_ext_gather_data_new ();
  data->self        = bz_track_weak (self);
  data->cancellable = bz_object_maybe_ref (cancellable);

  return dex_scheduler_spawn (
      self->scheduler,
      bz_get_dex_stack_size (),
      (DexFiberFunc) gather_fiber,
      gnome_ext_gather_data_ref (data),
      gnome_ext_gather_data_unref);
}

static DexFuture *
bz_gnome_extension_instance_retrieve_install_ids (BzBackend    *backend,
                                                  GCancellable *cancellable)
{
  BzGnomeExtensionInstance *self        = BZ_GNOME_EXTENSION_INSTANCE (backend);
  g_autoptr (GnomeExtInstallsData) data = NULL;

  data              = gnome_ext_installs_data_new ();
  data->self        = bz_track_weak (self);
  data->cancellable = bz_object_maybe_ref (cancellable);

  return dex_scheduler_spawn (
      self->scheduler,
      bz_get_dex_stack_size (),
      (DexFiberFunc) retrieve_installs_fiber,
      gnome_ext_installs_data_ref (data),
      gnome_ext_installs_data_unref);
}

static DexFuture *
bz_gnome_extension_instance_schedule_transaction (BzBackend    *backend,
                                                  BzEntry     **installs,
                                                  guint         n_installs,
                                                  BzEntry     **updates,
                                                  guint         n_updates,
                                                  BzEntry     **removals,
                                                  guint         n_removals,
                                                  DexChannel   *channel,
                                                  GCancellable *cancellable)
{
  BzGnomeExtensionInstance *self           = BZ_GNOME_EXTENSION_INSTANCE (backend);
  g_autoptr (GnomeExtTransactionData) data = NULL;

  data              = gnome_ext_transaction_data_new ();
  data->self        = bz_track_weak (self);
  data->cancellable = bz_object_maybe_ref (cancellable);
  data->channel     = bz_dex_maybe_ref (channel);

  data->installs = g_ptr_array_new_with_free_func (g_object_unref);
  for (guint i = 0; i < n_installs; i++)
    {
      dex_return_error_if_fail (BZ_IS_GNOME_EXTENSION_ENTRY (installs[i]));
      g_ptr_array_add (data->installs, g_object_ref (installs[i]));
    }
  for (guint i = 0; i < n_updates; i++)
    {
      dex_return_error_if_fail (BZ_IS_GNOME_EXTENSION_ENTRY (updates[i]));
      g_ptr_array_add (data->installs, g_object_ref (updates[i]));
    }

  data->removals = g_ptr_array_new_with_free_func (g_object_unref);
  for (guint i = 0; i < n_removals; i++)
    {
      dex_return_error_if_fail (BZ_IS_GNOME_EXTENSION_ENTRY (removals[i]));
      g_ptr_array_add (data->removals, g_object_ref (removals[i]));
    }

  return dex_scheduler_spawn (
      self->scheduler,
      bz_get_dex_stack_size (),
      (DexFiberFunc) transaction_fiber,
      gnome_ext_transaction_data_ref (data),
      gnome_ext_transaction_data_unref);
}

static gboolean
bz_gnome_extension_instance_cancel_task_for_entry (BzBackend *backend,
                                                   BzEntry   *entry)
{
  return FALSE;
}

static void
backend_iface_init (BzBackendInterface *iface)
{
  iface->create_notification_channel = bz_gnome_extension_instance_create_notification_channel;
  iface->retrieve_remote_entries     = bz_gnome_extension_instance_retrieve_remote_entries;
  iface->retrieve_install_ids        = bz_gnome_extension_instance_retrieve_install_ids;
  iface->schedule_transaction        = bz_gnome_extension_instance_schedule_transaction;
  iface->cancel_task_for_entry       = bz_gnome_extension_instance_cancel_task_for_entry;
}

DexFuture *
bz_gnome_extension_instance_new (void)
{
  g_autoptr (GnomeExtInitData) data = NULL;

  data       = gnome_ext_init_data_new ();
  data->self = g_object_new (BZ_TYPE_GNOME_EXTENSION_INSTANCE, NULL);

  return dex_scheduler_spawn (
      data->self->scheduler,
      bz_get_dex_stack_size (),
      (DexFiberFunc) init_fiber,
      gnome_ext_init_data_ref (data),
      gnome_ext_init_data_unref);
}

static GDBusProxy *
get_shell_extensions_proxy (GCancellable *cancellable,
                            GError      **error)
{
  return g_dbus_proxy_new_for_bus_sync (
      G_BUS_TYPE_SESSION,
      G_DBUS_PROXY_FLAGS_NONE,
      NULL,
      SHELL_EXTENSIONS_BUS,
      SHELL_EXTENSIONS_PATH,
      SHELL_EXTENSIONS_IFACE,
      cancellable,
      error);
}

static void
on_shell_signal (BzGnomeExtensionInstance *self,
                 const char               *sender_name,
                 const char               *signal_name,
                 GVariant                 *parameters,
                 GDBusProxy               *proxy)
{
  g_autoptr (BzBackendNotification) notif = NULL;

  if (g_strcmp0 (signal_name, "ExtensionStateChanged") != 0)
    return;

  notif = bz_backend_notification_new ();
  bz_backend_notification_set_kind (notif, BZ_BACKEND_NOTIFICATION_KIND_EXTERNAL_CHANGE);
  send_notif_all (self, notif, TRUE);
}

static DexFuture *
init_fiber (GnomeExtInitData *data)
{
  BzGnomeExtensionInstance *self = data->self;
  g_autoptr (GError) local_error = NULL;

  self->shell_proxy = get_shell_extensions_proxy (NULL, &local_error);
  if (self->shell_proxy == NULL)
    {
      g_warning ("Failed to connect to shell extensions DBus interface: %s",
                 local_error->message);
      return dex_future_new_for_object (self);
    }

  g_signal_connect_swapped (
      self->shell_proxy, "g-signal",
      G_CALLBACK (on_shell_signal), self);

  return dex_future_new_for_object (self);
}

static DexFuture *
retrieve_installs_fiber (GnomeExtInstallsData *data)
{
  g_autoptr (BzGnomeExtensionInstance) self = NULL;
  g_autoptr (GError) local_error            = NULL;
  g_autoptr (GDBusProxy) proxy              = NULL;
  g_autoptr (GVariant) result               = NULL;
  g_autoptr (GVariant) extensions           = NULL;
  g_autoptr (GHashTable) ids                = NULL;
  GVariantIter iter                         = { 0 };
  const char  *uuid                         = NULL;
  GVariant    *info_val                     = NULL;

  bz_weak_get_or_return_reject (self, data->self);

  if (self->shell_proxy != NULL)
    proxy = g_object_ref (self->shell_proxy);
  else
    {
      proxy = get_shell_extensions_proxy (data->cancellable, &local_error);
      if (proxy == NULL)
        return dex_future_new_reject (G_IO_ERROR, G_IO_ERROR_FAILED,
                                      "Failed to get shell extensions proxy: %s",
                                      local_error->message);
    }

  result = g_dbus_proxy_call_sync (
      proxy,
      "ListExtensions",
      NULL,
      G_DBUS_CALL_FLAGS_NONE,
      -1,
      data->cancellable,
      &local_error);
  if (result == NULL)
    return dex_future_new_reject (G_IO_ERROR, G_IO_ERROR_FAILED,
                                  "Failed to call ListExtensions: %s",
                                  local_error->message);

  ids        = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
  extensions = g_variant_get_child_value (result, 0);

  g_variant_iter_init (&iter, extensions);
  while (g_variant_iter_next (&iter, "{&s@a{sv}}", &uuid, &info_val))
    {
      g_autoptr (GVariant) ext_info         = info_val;
      g_autoptr (BzGnomeExtensionInfo) obj  = NULL;
      g_autoptr (GListStore) shell_versions = NULL;
      g_autoptr (GVariant) sv_variant       = NULL;
      double      version                   = 0.0;
      double      state                     = 0.0;
      gboolean    enabled                   = FALSE;
      gboolean    has_prefs                 = FALSE;
      gboolean    has_update                = FALSE;
      gboolean    can_change                = FALSE;
      gboolean    is_system                 = FALSE;
      const char *error                     = "";
      const char *name                      = "";
      const char *path                      = "";

      obj = bz_gnome_extension_info_new ();

      g_variant_lookup (ext_info, "version", "d", &version);
      g_variant_lookup (ext_info, "state", "d", &state);
      g_variant_lookup (ext_info, "enabled", "b", &enabled);
      g_variant_lookup (ext_info, "hasPrefs", "b", &has_prefs);
      g_variant_lookup (ext_info, "hasUpdate", "b", &has_update);
      g_variant_lookup (ext_info, "canChange", "b", &can_change);
      g_variant_lookup (ext_info, "error", "&s", &error);
      g_variant_lookup (ext_info, "name", "&s", &name);

      if (g_variant_lookup (ext_info, "path", "&s", &path))
        is_system = g_str_has_prefix (path, "/usr/");

      shell_versions = g_list_store_new (GTK_TYPE_STRING_OBJECT);
      sv_variant     = g_variant_lookup_value (ext_info, "shell-version", NULL);

      if (sv_variant != NULL)
        {
          GVariantIter sv_iter = { 0 };
          GVariant    *sv_item = NULL;

          g_variant_iter_init (&sv_iter, sv_variant);
          while (g_variant_iter_next (&sv_iter, "@*", &sv_item))
            {
              g_autoptr (GVariant) sv             = sv_item;
              g_autoptr (GVariant) sv_inner       = NULL;
              g_autoptr (GtkStringObject) str_obj = NULL;
              const char *ver_str                 = NULL;

              sv_inner = g_variant_get_variant (sv);
              ver_str  = g_variant_get_string (sv_inner, NULL);
              str_obj  = gtk_string_object_new (ver_str);
              g_list_store_append (shell_versions, str_obj);
            }
        }

      g_object_set (obj,
                    "uuid", uuid,
                    "name", name,
                    "version", version,
                    "state", state,
                    "enabled", enabled,
                    "error", error,
                    "has-prefs", has_prefs,
                    "has-update", has_update,
                    "can-change", can_change,
                    "is-system", is_system,
                    "shell-versions", shell_versions,
                    NULL);

      g_hash_table_replace (ids, g_strdup (uuid), g_steal_pointer (&obj));
    }

  return dex_future_new_take_boxed (G_TYPE_HASH_TABLE, g_steal_pointer (&ids));
}

static DexFuture *
gather_fiber (GnomeExtGatherData *data)
{
  g_autoptr (BzGnomeExtensionInstance) self = NULL;
  g_autoptr (GError) local_error            = NULL;
  g_autoptr (SoupMessage) message           = NULL;
  g_autoptr (GOutputStream) output          = NULL;
  g_autoptr (GBytes) compressed             = NULL;
  g_autoptr (GBytes) xml_bytes              = NULL;
  g_autoptr (XbBuilderSource) source        = NULL;
  g_autoptr (XbBuilder) builder             = NULL;
  g_autoptr (XbSilo) silo                   = NULL;
  g_autoptr (XbNode) root                   = NULL;
  g_autoptr (GPtrArray) children            = NULL;

  bz_weak_get_or_return_reject (self, data->self);

  message = soup_message_new (SOUP_METHOD_GET, APPSTREAM_URL);
  soup_message_headers_append (
      soup_message_get_request_headers (message),
      "User-Agent", "Bazaar");

  output = g_memory_output_stream_new_resizable ();

  dex_await (
      bz_send_with_global_http_session_then_splice_into (message, output),
      &local_error);
  if (local_error != NULL)
    return dex_future_new_reject (G_IO_ERROR, G_IO_ERROR_FAILED,
                                  "Failed to fetch AppStream data from %s: %s",
                                  APPSTREAM_URL, local_error->message);

  compressed = g_memory_output_stream_steal_as_bytes (G_MEMORY_OUTPUT_STREAM (output));

  xml_bytes = bz_decompress_gz_bytes (compressed, NULL, &local_error);
  if (xml_bytes == NULL)
    return dex_future_new_reject (G_IO_ERROR, G_IO_ERROR_FAILED,
                                  "Failed to decompress AppStream data: %s",
                                  local_error->message);

  source = xb_builder_source_new ();
  if (!xb_builder_source_load_bytes (source, xml_bytes,
                                     XB_BUILDER_SOURCE_FLAG_LITERAL_TEXT,
                                     &local_error))
    return dex_future_new_reject (G_IO_ERROR, G_IO_ERROR_FAILED,
                                  "Failed to load AppStream bytes into xmlb: %s",
                                  local_error->message);

  builder = xb_builder_new ();
  xb_builder_import_source (builder, source);
  silo = xb_builder_compile (builder,
                             XB_BUILDER_COMPILE_FLAG_NONE,
                             NULL,
                             &local_error);
  if (silo == NULL)
    return dex_future_new_reject (G_IO_ERROR, G_IO_ERROR_FAILED,
                                  "Failed to compile xmlb silo: %s",
                                  local_error->message);

  root     = xb_silo_get_root (silo);
  children = xb_node_get_children (root);

  if (children == NULL || children->len == 0)
    return dex_future_new_true ();

  {
    g_autoptr (BzBackendNotification) notif = NULL;

    notif = bz_backend_notification_new ();
    bz_backend_notification_set_kind (notif, BZ_BACKEND_NOTIFICATION_KIND_TELL_INCOMING);
    bz_backend_notification_set_n_incoming (notif, (int) children->len);
    send_notif_all (self, notif, TRUE);
  }

  for (guint i = 0; i < children->len; i++)
    {
      XbNode *comp                                 = g_ptr_array_index (children, i);
      g_autoptr (AsComponent) component            = NULL;
      g_autoptr (BzGnomeExtensionEntry) entry      = NULL;
      g_autoptr (BzBackendNotification) notif      = NULL;
      g_autoptr (BzBackendNotification) skip_notif = NULL;

      component = bz_parse_component_for_node (comp, &local_error);
      if (component == NULL)
        {
          g_warning ("Failed to parse extension component: %s",
                     local_error ? local_error->message : "unknown error");
          g_clear_error (&local_error);

          skip_notif = bz_backend_notification_new ();
          bz_backend_notification_set_kind (skip_notif, BZ_BACKEND_NOTIFICATION_KIND_TELL_INCOMING);
          bz_backend_notification_set_n_incoming (skip_notif, -1);
          send_notif_all (self, skip_notif, TRUE);
          continue;
        }

      entry = bz_gnome_extension_entry_new_from_component (component, &local_error);
      if (entry == NULL)
        {
          g_warning ("Failed to create extension entry for %s: %s",
                     as_component_get_id (component),
                     local_error ? local_error->message : "unknown error");
          g_clear_error (&local_error);

          skip_notif = bz_backend_notification_new ();
          bz_backend_notification_set_kind (skip_notif, BZ_BACKEND_NOTIFICATION_KIND_TELL_INCOMING);
          bz_backend_notification_set_n_incoming (skip_notif, -1);
          send_notif_all (self, skip_notif, TRUE);
          continue;
        }

      notif = bz_backend_notification_new ();
      bz_backend_notification_set_kind (notif, BZ_BACKEND_NOTIFICATION_KIND_REPLACE_ENTRY);
      bz_backend_notification_set_entry (notif, BZ_ENTRY (entry));
      send_notif_all (self, notif, TRUE);
    }

  {
    g_autoptr (BzBackendNotification) notif = NULL;

    notif = bz_backend_notification_new ();
    bz_backend_notification_set_kind (notif, BZ_BACKEND_NOTIFICATION_KIND_REMOTE_SYNC_FINISH);
    bz_backend_notification_set_remote_name (notif, "gnome-extensions");
    send_notif_all (self, notif, TRUE);
  }

  return dex_future_new_true ();
}

static DexFuture *
transaction_fiber (GnomeExtTransactionData *data)
{
  g_autoptr (BzGnomeExtensionInstance) self = NULL;
  GCancellable *cancellable                 = data->cancellable;
  g_autoptr (GError) local_error            = NULL;
  g_autoptr (GDBusProxy) proxy              = NULL;
  g_autoptr (GHashTable) errored            = NULL;

  bz_weak_get_or_return_reject (self, data->self);

  errored = g_hash_table_new_full (
      g_direct_hash, g_direct_equal,
      g_object_unref, (GDestroyNotify) g_error_free);

  if (self->shell_proxy != NULL)
    proxy = g_object_ref (self->shell_proxy);
  else
    proxy = get_shell_extensions_proxy (cancellable, &local_error);

  if (proxy == NULL)
    {
      for (guint i = 0; i < data->installs->len; i++)
        g_hash_table_replace (errored,
                              g_object_ref (g_ptr_array_index (data->installs, i)),
                              g_error_copy (local_error));
      for (guint i = 0; i < data->removals->len; i++)
        g_hash_table_replace (errored,
                              g_object_ref (g_ptr_array_index (data->removals, i)),
                              g_error_copy (local_error));

      dex_channel_close_send (data->channel);
      return dex_future_new_take_boxed (G_TYPE_HASH_TABLE, g_steal_pointer (&errored));
    }

  for (guint i = 0; i < data->installs->len; i++)
    {
      BzGnomeExtensionEntry *entry                 = g_ptr_array_index (data->installs, i);
      const char            *uuid                  = NULL;
      const char            *unique_id             = NULL;
      g_autoptr (BzBackendTransactionOpPayload) op = NULL;
      g_autoptr (GVariant) result                  = NULL;
      g_autoptr (GError) op_error                  = NULL;

      uuid      = bz_gnome_extension_entry_get_uuid (entry);
      unique_id = bz_entry_get_unique_id (BZ_ENTRY (entry));

      if (data->channel != NULL)
        {
          op = bz_backend_transaction_op_payload_new ();
          bz_backend_transaction_op_payload_set_entry (op, BZ_ENTRY (entry));
          bz_backend_transaction_op_payload_set_name (op, uuid);
          dex_channel_send (data->channel, dex_future_new_for_object (op));
        }

      result = g_dbus_proxy_call_sync (
          proxy,
          "InstallRemoteExtension",
          g_variant_new ("(s)", uuid),
          G_DBUS_CALL_FLAGS_NONE,
          1000000000,
          cancellable,
          &op_error);

      if (op_error != NULL)
        {
          g_hash_table_replace (errored,
                                g_object_ref (entry),
                                g_steal_pointer (&op_error));
        }
      else
        {
          const char *status = NULL;

          g_variant_get (result, "(&s)", &status);

          if (g_strcmp0 (status, "successful") == 0)
            {
              g_autoptr (BzBackendNotification) notif = NULL;

              notif = bz_backend_notification_new ();
              bz_backend_notification_set_kind (notif, BZ_BACKEND_NOTIFICATION_KIND_INSTALL_DONE);
              bz_backend_notification_set_unique_id (notif, unique_id);
              send_notif_all (self, notif, TRUE);
            }
          else
            {
              g_hash_table_replace (
                  errored,
                  g_object_ref (entry),
                  g_error_new (G_IO_ERROR, G_IO_ERROR_FAILED,
                               "Shell reported installation status \"%s\" for extension %s",
                               status, uuid));
            }
        }

      if (data->channel != NULL && op != NULL)
        dex_channel_send (data->channel, dex_future_new_for_object (op));
    }

  for (guint i = 0; i < data->removals->len; i++)
    {
      BzGnomeExtensionEntry *entry                 = g_ptr_array_index (data->removals, i);
      const char            *uuid                  = NULL;
      const char            *unique_id             = NULL;
      g_autoptr (BzBackendTransactionOpPayload) op = NULL;
      g_autoptr (GVariant) result                  = NULL;
      g_autoptr (GError) op_error                  = NULL;

      uuid      = bz_gnome_extension_entry_get_uuid (entry);
      unique_id = bz_entry_get_unique_id (BZ_ENTRY (entry));

      if (data->channel != NULL)
        {
          op = bz_backend_transaction_op_payload_new ();
          bz_backend_transaction_op_payload_set_entry (op, BZ_ENTRY (entry));
          bz_backend_transaction_op_payload_set_name (op, uuid);
          dex_channel_send (data->channel, dex_future_new_for_object (op));
        }

      result = g_dbus_proxy_call_sync (
          proxy,
          "UninstallExtension",
          g_variant_new ("(s)", uuid),
          G_DBUS_CALL_FLAGS_NONE,
          -1,
          cancellable,
          &op_error);

      if (op_error != NULL)
        {
          g_hash_table_replace (errored,
                                g_object_ref (entry),
                                g_steal_pointer (&op_error));
        }
      else
        {
          gboolean ok = FALSE;

          g_variant_get (result, "(b)", &ok);

          if (ok)
            {
              g_autoptr (BzBackendNotification) notif = NULL;

              notif = bz_backend_notification_new ();
              bz_backend_notification_set_kind (notif, BZ_BACKEND_NOTIFICATION_KIND_REMOVE_DONE);
              bz_backend_notification_set_unique_id (notif, unique_id);
              send_notif_all (self, notif, TRUE);
            }
          else
            {
              g_hash_table_replace (
                  errored,
                  g_object_ref (entry),
                  g_error_new (G_IO_ERROR, G_IO_ERROR_FAILED,
                               "Shell failed to uninstall extension %s", uuid));
            }
        }

      if (data->channel != NULL && op != NULL)
        dex_channel_send (data->channel, dex_future_new_for_object (op));
    }

  dex_channel_close_send (data->channel);
  return dex_future_new_take_boxed (G_TYPE_HASH_TABLE, g_steal_pointer (&errored));
}

static void
send_notif (BzGnomeExtensionInstance *self,
            DexChannel               *channel,
            BzBackendNotification    *notif,
            gboolean                  lock)
{
  g_autoptr (GMutexLocker) locker = NULL;

  if (lock)
    locker = g_mutex_locker_new (&self->notif_mutex);

  if (self->notif_send == NULL ||
      !dex_future_is_pending (self->notif_send))
    {
      dex_clear (&self->notif_send);
      self->notif_send = dex_channel_send (
          channel,
          dex_future_new_for_object (notif));
    }
  else
    {
      g_debug ("Dropping notification of kind %d (send still pending)",
               bz_backend_notification_get_kind (notif));
    }
}

static void
send_notif_all (BzGnomeExtensionInstance *self,
                BzBackendNotification    *notif,
                gboolean                  lock)
{
  g_autoptr (GMutexLocker) locker = NULL;

  if (lock)
    locker = g_mutex_locker_new (&self->notif_mutex);

  for (guint i = 0; i < self->notif_channels->len;)
    {
      DexChannel *channel = g_ptr_array_index (self->notif_channels, i);
      if (dex_channel_can_send (channel))
        {
          send_notif (self, channel, notif, FALSE);
          i++;
        }
      else
        g_ptr_array_remove_index_fast (self->notif_channels, i);
    }
}

char *
bz_gnome_extension_instance_dup_shell_version (BzGnomeExtensionInstance *self)
{
  g_autoptr (GVariant) ver_variant = NULL;

  g_return_val_if_fail (BZ_IS_GNOME_EXTENSION_INSTANCE (self), NULL);

  if (self->shell_proxy == NULL)
    return NULL;

  ver_variant = g_dbus_proxy_get_cached_property (self->shell_proxy, "ShellVersion");
  if (ver_variant == NULL)
    return NULL;

  return g_strdup_printf ("%d", (int) g_ascii_strtod (g_variant_get_string (ver_variant, NULL), NULL));
}

void
bz_gnome_extension_instance_check_for_updates (BzGnomeExtensionInstance *self)
{
  g_return_if_fail (BZ_IS_GNOME_EXTENSION_INSTANCE (self));
  if (self->shell_proxy == NULL)
    return;
  g_dbus_proxy_call (self->shell_proxy, "CheckForUpdates", NULL,
                     G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
}
