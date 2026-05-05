/* bz-flatpak-instance.c
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

#define G_LOG_DOMAIN  "BAZAAR::FLATPAK"
#define BAZAAR_MODULE "flatpak"

#include <malloc.h>
#include <xmlb.h>

#include "config.h"

#include "bz-appstream-parser.h"
#include "bz-backend-notification.h"
#include "bz-backend-transaction-op-payload.h"
#include "bz-backend-transaction-op-progress-payload.h"
#include "bz-backend.h"
#include "bz-env.h"
#include "bz-flatpak-private.h"
#include "bz-global-net.h"
#include "bz-io.h"
#include "bz-repository.h"
#include "bz-util.h"

/* clang-format off */
G_DEFINE_QUARK (bz-flatpak-error-quark, bz_flatpak_error);
/* clang-format on */

struct _BzFlatpakInstance
{
  GObject parent_instance;

  DexScheduler *scheduler;

  FlatpakInstallation *system;
  FlatpakInstallation *system_interactive;
  GFileMonitor        *system_events;
  int                  system_mute;

  FlatpakInstallation *user;
  FlatpakInstallation *user_interactive;
  GFileMonitor        *user_events;
  int                  user_mute;

  GMutex mute_mutex;

  GMutex     notif_mutex;
  GPtrArray *notif_channels;
  DexFuture *notif_send;

  GMutex transactions_mutex;
  /* BzEntry* -> GPtrArray* -> GCancellable* */
  GHashTable *ongoing_cancellables;
};

static void
backend_iface_init (BzBackendInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (
    BzFlatpakInstance,
    bz_flatpak_instance,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (BZ_TYPE_BACKEND, backend_iface_init));

BZ_DEFINE_DATA (
    init,
    Init,
    {
      BzFlatpakInstance *self;
    },
    BZ_RELEASE_DATA (self, g_object_unref))
static DexFuture *
init_fiber (InitData *data);

BZ_DEFINE_DATA (
    check_has_flathub,
    CheckHasFlathub,
    {
      GWeakRef     *self;
      GCancellable *cancellable;
    },
    BZ_RELEASE_DATA (self, bz_weak_release);
    BZ_RELEASE_DATA (cancellable, g_object_unref));
static DexFuture *
check_has_flathub_fiber (CheckHasFlathubData *data);

BZ_DEFINE_DATA (
    ensure_flathub,
    EnsureFlathub,
    {
      GWeakRef     *self;
      GCancellable *cancellable;
    },
    BZ_RELEASE_DATA (self, bz_weak_release);
    BZ_RELEASE_DATA (cancellable, g_object_unref));
static DexFuture *
ensure_flathub_fiber (EnsureFlathubData *data);

BZ_DEFINE_DATA (
    load_local_ref,
    LoadLocalRef,
    {
      GWeakRef     *self;
      GCancellable *cancellable;
      GFile        *file;
    },
    BZ_RELEASE_DATA (self, bz_weak_release);
    BZ_RELEASE_DATA (cancellable, g_object_unref);
    BZ_RELEASE_DATA (file, g_object_unref));
static DexFuture *
load_local_ref_fiber (LoadLocalRefData *data);

BZ_DEFINE_DATA (
    gather_refs,
    GatherRefs,
    {
      GWeakRef     *self;
      GCancellable *cancellable;
      guint         total;
    },
    BZ_RELEASE_DATA (self, bz_weak_release);
    BZ_RELEASE_DATA (cancellable, g_object_unref));
static DexFuture *
retrieve_remote_refs_fiber (GatherRefsData *data);
static DexFuture *
retrieve_installs_fiber (GatherRefsData *data);
static DexFuture *
retrieve_updates_fiber (GatherRefsData *data);

BZ_DEFINE_DATA (
    list_repos,
    ListRepos,
    {
      GWeakRef     *self;
      GCancellable *cancellable;
    },
    BZ_RELEASE_DATA (self, bz_weak_release);
    BZ_RELEASE_DATA (cancellable, g_object_unref));

static DexFuture *
list_repositories_fiber (ListReposData *data);

BZ_DEFINE_DATA (
    retrieve_refs_for_remote,
    RetrieveRefsForRemote,
    {
      GatherRefsData      *parent;
      FlatpakInstallation *installation;
      FlatpakRemote       *remote;
    },
    BZ_RELEASE_DATA (parent, gather_refs_data_unref);
    BZ_RELEASE_DATA (installation, g_object_unref);
    BZ_RELEASE_DATA (remote, g_object_unref));
static DexFuture *
retrieve_refs_for_remote_fiber (RetrieveRefsForRemoteData *data);

static DexFuture *
retrieve_refs_for_enumerable_remote (BzFlatpakInstance   *self,
                                     GCancellable        *cancellable,
                                     const char          *remote_name,
                                     FlatpakInstallation *installation,
                                     FlatpakRemote       *remote);

static DexFuture *
retrieve_refs_for_noenumerable_remote (BzFlatpakInstance   *self,
                                       GCancellable        *cancellable,
                                       const char          *remote_name,
                                       FlatpakInstallation *installation,
                                       FlatpakRemote       *remote);

BZ_DEFINE_DATA (
    transaction,
    Transaction,
    {
      GWeakRef     *self;
      GMutex        mutex;
      GCancellable *cancellable;
      GPtrArray    *installs;
      GPtrArray    *updates;
      GPtrArray    *removals;
      DexChannel   *channel;
      GPtrArray    *send_futures;
      GHashTable   *ref_to_entry_hash;
      GHashTable   *op_to_progress_hash;
      guint         unidentified_op_cnt;
    },
    BZ_RELEASE_DATA (self, bz_weak_release);
    g_mutex_clear (&self->mutex);
    BZ_RELEASE_DATA (cancellable, g_object_unref);
    BZ_RELEASE_DATA (installs, g_ptr_array_unref);
    BZ_RELEASE_DATA (updates, g_ptr_array_unref);
    BZ_RELEASE_DATA (removals, g_ptr_array_unref);
    BZ_RELEASE_DATA (channel, dex_unref);
    BZ_RELEASE_DATA (send_futures, g_ptr_array_unref);
    BZ_RELEASE_DATA (ref_to_entry_hash, g_hash_table_unref);
    BZ_RELEASE_DATA (op_to_progress_hash, g_hash_table_unref));
static DexFuture *
transaction_fiber (TransactionData *data);

BZ_DEFINE_DATA (
    transaction_job,
    TransactionJob,
    {
      TransactionData    *parent;
      FlatpakTransaction *transaction;
    },
    BZ_RELEASE_DATA (parent, transaction_data_unref);
    BZ_RELEASE_DATA (transaction, g_object_unref));
static DexFuture *
transaction_job_fiber (TransactionJobData *data);

static void
transaction_new_operation (FlatpakTransaction          *object,
                           FlatpakTransactionOperation *operation,
                           FlatpakTransactionProgress  *progress,
                           TransactionData             *data);
static void
transaction_operation_done (FlatpakTransaction          *object,
                            FlatpakTransactionOperation *operation,
                            gchar                       *commit,
                            gint                         result,
                            TransactionData             *data);
static gboolean
transaction_operation_error (FlatpakTransaction          *object,
                             FlatpakTransactionOperation *operation,
                             GError                      *error,
                             gint                         details,
                             TransactionData             *data);
static gboolean
transaction_ready (FlatpakTransaction *object,
                   TransactionData    *data);

static BzFlatpakEntry *
find_entry_from_operation (TransactionData             *data,
                           FlatpakTransactionOperation *operation);

BZ_DEFINE_DATA (
    transaction_operation,
    TransactionOperation,
    {
      TransactionData               *parent;
      BzFlatpakEntry                *entry;
      BzBackendTransactionOpPayload *op;
    },
    BZ_RELEASE_DATA (parent, transaction_data_unref);
    BZ_RELEASE_DATA (entry, g_object_unref);
    BZ_RELEASE_DATA (op, g_object_unref));
static void
transaction_progress_changed (FlatpakTransactionProgress *object,
                              TransactionOperationData   *data);

BZ_DEFINE_DATA (
    transaction_operation_done,
    TransactionOperationDone,
    {
      TransactionData             *parent;
      FlatpakTransaction          *transaction;
      FlatpakTransactionOperation *operation;
    },
    BZ_RELEASE_DATA (parent, transaction_data_unref);
    BZ_RELEASE_DATA (transaction, g_object_unref);
    BZ_RELEASE_DATA (operation, g_object_unref));
static DexFuture *
transaction_operation_done_fiber (TransactionOperationDoneData *data);

static void
installation_event (BzFlatpakInstance *self,
                    GFile             *file,
                    GFile             *other_file,
                    GFileMonitorEvent  event_type,
                    GFileMonitor      *monitor);

static void
send_notif (BzFlatpakInstance     *self,
            DexChannel            *channel,
            BzBackendNotification *notif,
            gboolean               lock);

static void
send_notif_all (BzFlatpakInstance     *self,
                BzBackendNotification *notif,
                gboolean               lock);

#define SEND_AND_RETURN_ERROR(_self, _lock, _error, ...)                           \
  G_STMT_START                                                                     \
  {                                                                                \
    g_autofree char *_error_string           = NULL;                               \
    g_autoptr (BzBackendNotification) _notif = NULL;                               \
                                                                                   \
    _error_string = g_strdup_printf (__VA_ARGS__);                                 \
                                                                                   \
    _notif = bz_backend_notification_new ();                                       \
    bz_backend_notification_set_kind (_notif, BZ_BACKEND_NOTIFICATION_KIND_ERROR); \
    bz_backend_notification_set_error (_notif, _error_string);                     \
    send_notif_all ((_self), _notif, (_lock));                                     \
                                                                                   \
    return dex_future_new_for_error (                                              \
        g_error_new_literal (BZ_FLATPAK_ERROR,                                     \
                             (_error),                                             \
                             _error_string));                                      \
  }                                                                                \
  G_STMT_END

BZ_DEFINE_DATA (
    wait_notif,
    WaitNotif,
    {
      GWeakRef               self;
      DexChannel            *channel;
      BzBackendNotification *notif;
    },
    g_weak_ref_clear (&self->self);
    BZ_RELEASE_DATA (channel, dex_unref);
    BZ_RELEASE_DATA (notif, g_object_unref));
static DexFuture *
wait_notif_finally (DexFuture     *future,
                    WaitNotifData *data);

static gint
cmp_rref (FlatpakRemoteRef *a,
          FlatpakRemoteRef *b,
          GHashTable       *hash);

static XbSilo *
build_silo (XbBuilderSource *source,
            GCancellable    *cancellable,
            GError         **error);

static AsComponent *
extract_first_component_for_silo (XbSilo  *silo,
                                  GError **error);

static void
bz_flatpak_instance_dispose (GObject *object)
{
  BzFlatpakInstance *self = BZ_FLATPAK_INSTANCE (object);

  dex_clear (&self->scheduler);

  g_clear_object (&self->system);
  g_clear_object (&self->system_interactive);
  g_clear_object (&self->system_events);
  g_clear_object (&self->user);
  g_clear_object (&self->user_interactive);
  g_clear_object (&self->user_events);

  g_mutex_clear (&self->mute_mutex);

  g_clear_pointer (&self->notif_channels, g_ptr_array_unref);
  dex_clear (&self->notif_send);
  g_mutex_clear (&self->notif_mutex);

  g_clear_pointer (&self->ongoing_cancellables, g_hash_table_unref);
  g_mutex_clear (&self->transactions_mutex);

  G_OBJECT_CLASS (bz_flatpak_instance_parent_class)->dispose (object);
}

static void
bz_flatpak_instance_class_init (BzFlatpakInstanceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = bz_flatpak_instance_dispose;
}

static void
bz_flatpak_instance_init (BzFlatpakInstance *self)
{
  self->scheduler   = dex_thread_pool_scheduler_new ();
  self->system_mute = 0;
  self->user_mute   = 0;

  g_mutex_init (&self->mute_mutex);

  self->notif_channels = g_ptr_array_new_with_free_func (dex_unref);
  g_mutex_init (&self->notif_mutex);

  self->ongoing_cancellables = g_hash_table_new_full (
      g_direct_hash, g_direct_equal, g_object_unref, (GDestroyNotify) g_ptr_array_unref);
  g_mutex_init (&self->transactions_mutex);
}

static DexChannel *
bz_flatpak_instance_create_notification_channel (BzBackend *backend)
{
  BzFlatpakInstance *self        = BZ_FLATPAK_INSTANCE (backend);
  g_autoptr (DexChannel) channel = NULL;

  channel = dex_channel_new (0);

  g_mutex_lock (&self->notif_mutex);
  g_ptr_array_add (self->notif_channels, dex_ref (channel));
  g_mutex_unlock (&self->notif_mutex);

  return g_steal_pointer (&channel);
}

static DexFuture *
bz_flatpak_instance_load_local_package (BzBackend    *backend,
                                        GFile        *file,
                                        GCancellable *cancellable)
{
  BzFlatpakInstance *self           = BZ_FLATPAK_INSTANCE (backend);
  g_autoptr (LoadLocalRefData) data = NULL;

  data              = load_local_ref_data_new ();
  data->self        = bz_track_weak (self);
  data->cancellable = bz_object_maybe_ref (cancellable);
  data->file        = g_object_ref (file);

  return dex_scheduler_spawn (
      self->scheduler,
      bz_get_dex_stack_size (),
      (DexFiberFunc) load_local_ref_fiber,
      load_local_ref_data_ref (data),
      load_local_ref_data_unref);
}

static DexFuture *
bz_flatpak_instance_retrieve_remote_refs (BzBackend    *backend,
                                          GCancellable *cancellable)
{
  BzFlatpakInstance *self         = BZ_FLATPAK_INSTANCE (backend);
  g_autoptr (GatherRefsData) data = NULL;

  data              = gather_refs_data_new ();
  data->self        = bz_track_weak (self);
  data->cancellable = bz_object_maybe_ref (cancellable);
  data->total       = 0;

  return dex_scheduler_spawn (
      self->scheduler,
      bz_get_dex_stack_size (),
      (DexFiberFunc) retrieve_remote_refs_fiber,
      gather_refs_data_ref (data),
      gather_refs_data_unref);
}

static DexFuture *
bz_flatpak_instance_retrieve_install_ids (BzBackend    *backend,
                                          GCancellable *cancellable)
{
  BzFlatpakInstance *self         = BZ_FLATPAK_INSTANCE (backend);
  g_autoptr (GatherRefsData) data = NULL;

  data              = gather_refs_data_new ();
  data->self        = bz_track_weak (self);
  data->cancellable = bz_object_maybe_ref (cancellable);

  return dex_scheduler_spawn (
      self->scheduler,
      bz_get_dex_stack_size (),
      (DexFiberFunc) retrieve_installs_fiber,
      gather_refs_data_ref (data),
      gather_refs_data_unref);
}

static DexFuture *
bz_flatpak_instance_retrieve_update_ids (BzBackend    *backend,
                                         GCancellable *cancellable)
{
  BzFlatpakInstance *self         = BZ_FLATPAK_INSTANCE (backend);
  g_autoptr (GatherRefsData) data = NULL;

  data              = gather_refs_data_new ();
  data->self        = bz_track_weak (self);
  data->cancellable = bz_object_maybe_ref (cancellable);

  return dex_scheduler_spawn (
      self->scheduler,
      bz_get_dex_stack_size (),
      (DexFiberFunc) retrieve_updates_fiber,
      gather_refs_data_ref (data),
      gather_refs_data_unref);
}

static DexFuture *
bz_flatpak_instance_list_repositories (BzBackend    *backend,
                                       GCancellable *cancellable)
{
  BzFlatpakInstance *self        = BZ_FLATPAK_INSTANCE (backend);
  g_autoptr (ListReposData) data = NULL;

  data              = list_repos_data_new ();
  data->self        = bz_track_weak (self);
  data->cancellable = bz_object_maybe_ref (cancellable);

  return dex_scheduler_spawn (
      self->scheduler,
      bz_get_dex_stack_size (),
      (DexFiberFunc) list_repositories_fiber,
      list_repos_data_ref (data),
      list_repos_data_unref);
}

static DexFuture *
bz_flatpak_instance_schedule_transaction (BzBackend    *backend,
                                          BzEntry     **installs,
                                          guint         n_installs,
                                          BzEntry     **updates,
                                          guint         n_updates,
                                          BzEntry     **removals,
                                          guint         n_removals,
                                          DexChannel   *channel,
                                          GCancellable *cancellable)
{
  BzFlatpakInstance *self          = BZ_FLATPAK_INSTANCE (backend);
  BzFlatpakEntry   **installs_dup  = NULL;
  BzFlatpakEntry   **updates_dup   = NULL;
  BzFlatpakEntry   **removals_dup  = NULL;
  g_autoptr (TransactionData) data = NULL;

  for (guint i = 0; i < n_installs; i++)
    dex_return_error_if_fail (BZ_IS_FLATPAK_ENTRY (installs[i]));
  for (guint i = 0; i < n_updates; i++)
    dex_return_error_if_fail (BZ_IS_FLATPAK_ENTRY (updates[i]));
  for (guint i = 0; i < n_removals; i++)
    dex_return_error_if_fail (BZ_IS_FLATPAK_ENTRY (removals[i]));

  if (n_installs > 0)
    {
      installs_dup = g_malloc0_n (n_installs, sizeof (*installs_dup));
      for (guint i = 0; i < n_installs; i++)
        installs_dup[i] = g_object_ref (BZ_FLATPAK_ENTRY (installs[i]));
    }
  if (n_updates > 0)
    {
      updates_dup = g_malloc0_n (n_updates, sizeof (*updates_dup));
      for (guint i = 0; i < n_updates; i++)
        updates_dup[i] = g_object_ref (BZ_FLATPAK_ENTRY (updates[i]));
    }
  if (n_removals > 0)
    {
      removals_dup = g_malloc0_n (n_removals, sizeof (*removals_dup));
      for (guint i = 0; i < n_removals; i++)
        removals_dup[i] = g_object_ref (BZ_FLATPAK_ENTRY (removals[i]));
    }

  data                      = transaction_data_new ();
  data->self                = bz_track_weak (self);
  data->cancellable         = bz_object_maybe_ref (cancellable);
  data->installs            = installs_dup != NULL ? g_ptr_array_new_take ((gpointer *) installs_dup, n_installs, g_object_unref) : NULL;
  data->updates             = updates_dup != NULL ? g_ptr_array_new_take ((gpointer *) updates_dup, n_updates, g_object_unref) : NULL;
  data->removals            = removals_dup != NULL ? g_ptr_array_new_take ((gpointer *) removals_dup, n_removals, g_object_unref) : NULL;
  data->channel             = bz_dex_maybe_ref (channel);
  data->send_futures        = g_ptr_array_new_with_free_func (dex_unref);
  data->ref_to_entry_hash   = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
  data->op_to_progress_hash = g_hash_table_new_full (g_direct_hash, g_direct_equal, g_object_unref, NULL);
  g_mutex_init (&data->mutex);

  return dex_scheduler_spawn (
      self->scheduler,
      bz_get_dex_stack_size (),
      (DexFiberFunc) transaction_fiber,
      transaction_data_ref (data),
      transaction_data_unref);
}

static gboolean
bz_flatpak_instance_cancel_task_for_entry (BzBackend *backend,
                                           BzEntry   *entry)
{
  BzFlatpakInstance *self         = BZ_FLATPAK_INSTANCE (backend);
  g_autoptr (GMutexLocker) locker = NULL;
  GPtrArray *cancellables         = NULL;

  locker = g_mutex_locker_new (&self->transactions_mutex);

  cancellables = g_hash_table_lookup (self->ongoing_cancellables, entry);
  if (cancellables == NULL)
    return FALSE;

  for (guint i = 0; i < cancellables->len; i++)
    {
      GCancellable *cancellable = NULL;

      cancellable = g_ptr_array_index (cancellables, i);
      g_cancellable_cancel (cancellable);
    }

  return TRUE;
}

static void
backend_iface_init (BzBackendInterface *iface)
{
  iface->create_notification_channel = bz_flatpak_instance_create_notification_channel;
  iface->load_local_package          = bz_flatpak_instance_load_local_package;
  iface->retrieve_remote_entries     = bz_flatpak_instance_retrieve_remote_refs;
  iface->retrieve_install_ids        = bz_flatpak_instance_retrieve_install_ids;
  iface->retrieve_update_ids         = bz_flatpak_instance_retrieve_update_ids;
  iface->list_repositories           = bz_flatpak_instance_list_repositories;
  iface->schedule_transaction        = bz_flatpak_instance_schedule_transaction;
  iface->cancel_task_for_entry       = bz_flatpak_instance_cancel_task_for_entry;
}

FlatpakInstallation *
bz_flatpak_instance_get_system_installation (BzFlatpakInstance *self)
{
  g_return_val_if_fail (BZ_IS_FLATPAK_INSTANCE (self), NULL);
  return self->system;
}

FlatpakInstallation *
bz_flatpak_instance_get_user_installation (BzFlatpakInstance *self)
{
  g_return_val_if_fail (BZ_IS_FLATPAK_INSTANCE (self), NULL);
  return self->user;
}

DexFuture *
bz_flatpak_instance_new (void)
{
  g_autoptr (InitData) data = NULL;

  data       = init_data_new ();
  data->self = g_object_new (BZ_TYPE_FLATPAK_INSTANCE, NULL);

  return dex_scheduler_spawn (
      data->self->scheduler,
      bz_get_dex_stack_size (),
      (DexFiberFunc) init_fiber,
      init_data_ref (data), init_data_unref);
}

DexFuture *
bz_flatpak_instance_has_flathub (BzFlatpakInstance *self,
                                 GCancellable      *cancellable)
{
  g_autoptr (CheckHasFlathubData) data = NULL;

  dex_return_error_if_fail (BZ_IS_FLATPAK_INSTANCE (self));
  dex_return_error_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  data              = check_has_flathub_data_new ();
  data->self        = bz_track_weak (self);
  data->cancellable = bz_object_maybe_ref (cancellable);

  return dex_scheduler_spawn (
      self->scheduler,
      bz_get_dex_stack_size (),
      (DexFiberFunc) check_has_flathub_fiber,
      check_has_flathub_data_ref (data), check_has_flathub_data_unref);
}

DexFuture *
bz_flatpak_instance_ensure_has_flathub (BzFlatpakInstance *self,
                                        GCancellable      *cancellable)
{
  g_autoptr (EnsureFlathubData) data = NULL;

  dex_return_error_if_fail (BZ_IS_FLATPAK_INSTANCE (self));
  dex_return_error_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  data              = ensure_flathub_data_new ();
  data->self        = bz_track_weak (self);
  data->cancellable = bz_object_maybe_ref (cancellable);

  return dex_scheduler_spawn (
      self->scheduler,
      bz_get_dex_stack_size (),
      (DexFiberFunc) ensure_flathub_fiber,
      ensure_flathub_data_ref (data), ensure_flathub_data_unref);
}

static DexFuture *
init_fiber (InitData *data)
{
  BzFlatpakInstance *self        = data->self;
  g_autoptr (GError) local_error = NULL;
  g_autofree char *main_cache    = NULL;

  bz_discard_module_dir ();

  self->system = flatpak_installation_new_system (NULL, &local_error);
  if (self->system != NULL)
    {
      g_autoptr (GFile) path = NULL;

      flatpak_installation_set_no_interaction (self->system, TRUE);

      path                     = flatpak_installation_get_path (self->system);
      self->system_interactive = flatpak_installation_new_for_path (
          path, FALSE, NULL, NULL);
      g_assert (self->system_interactive != NULL);
      flatpak_installation_set_no_interaction (self->system_interactive, FALSE);

      self->system_events = flatpak_installation_create_monitor (
          self->system, NULL, &local_error);
      if (self->system_events != NULL)
        g_signal_connect_swapped (
            self->system_events, "changed",
            G_CALLBACK (installation_event), self);
      else
        {
          g_warning ("Failed to initialize event watch for system installation: %s",
                     local_error->message);
          g_clear_pointer (&local_error, g_error_free);
        }
    }
  else
    {
      g_warning ("Failed to initialize system installation: %s",
                 local_error->message);
      g_clear_pointer (&local_error, g_error_free);
    }

#ifdef SANDBOXED_LIBFLATPAK
  {
    g_autoptr (GFile) user_installation_path = NULL;
    const char      *home                    = g_get_home_dir ();
    g_autofree char *user_flatpak_path       = g_build_filename (home, ".local", "share", "flatpak", NULL);

    user_installation_path = g_file_new_for_path (user_flatpak_path);
    self->user             = flatpak_installation_new_for_path (
        user_installation_path,
        TRUE,
        NULL,
        &local_error);
  }
#else
  self->user = flatpak_installation_new_user (NULL, &local_error);
#endif

  if (self->user != NULL)
    {
      g_autoptr (GFile) path = NULL;

      flatpak_installation_set_no_interaction (self->user, TRUE);

      path                   = flatpak_installation_get_path (self->user);
      self->user_interactive = flatpak_installation_new_for_path (
          path, TRUE, NULL, NULL);
      g_assert (self->user_interactive != NULL);
      flatpak_installation_set_no_interaction (self->user_interactive, FALSE);

      self->user_events = flatpak_installation_create_monitor (
          self->user, NULL, &local_error);
      if (self->user_events != NULL)
        g_signal_connect_swapped (
            self->user_events, "changed",
            G_CALLBACK (installation_event), self);
      else
        {
          g_warning ("Failed to initialize event watch for user installation: %s",
                     local_error->message);
          g_clear_pointer (&local_error, g_error_free);
        }
    }
  else
    {
      g_warning ("Failed to initialize user installation: %s",
                 local_error->message);
      g_clear_pointer (&local_error, g_error_free);
    }

#ifdef SANDBOXED_LIBFLATPAK
  if (g_getenv ("FLATPAK_BINARY") == NULL)
    {
      g_autofree char *flatpak_path = NULL;
      gint             exit_status  = 0;

      g_spawn_command_line_sync ("flatpak-spawn --host sh -c 'command -v flatpak'",
                                 &flatpak_path, NULL, &exit_status, NULL);

      if (exit_status == 0 && flatpak_path != NULL)
        g_setenv ("FLATPAK_BINARY", g_strstrip (flatpak_path), FALSE);
      else
        g_warning ("Failed to resolve host flatpak binary! User refs wont be updated");
    }
#endif

  if (self->system == NULL && self->user == NULL)
    return dex_future_new_reject (
        BZ_FLATPAK_ERROR,
        BZ_FLATPAK_ERROR_CANNOT_INITIALIZE,
        "Failed to initialize any flatpak installations");

  return dex_future_new_for_object (self);
}

static DexFuture *
check_has_flathub_fiber (CheckHasFlathubData *data)
{
  g_autoptr (BzFlatpakInstance) self   = NULL;
  GCancellable *cancellable            = data->cancellable;
  g_autoptr (GError) local_error       = NULL;
  g_autoptr (GPtrArray) system_remotes = NULL;
  guint n_system_remotes               = 0;
  g_autoptr (GPtrArray) user_remotes   = NULL;
  guint n_user_remotes                 = 0;

  bz_weak_get_or_return_reject (self, data->self);

  if (self->system != NULL)
    {
      system_remotes = flatpak_installation_list_remotes (
          self->system, cancellable, &local_error);
      if (system_remotes == NULL)
        return dex_future_new_reject (
            BZ_FLATPAK_ERROR,
            BZ_FLATPAK_ERROR_CANNOT_INITIALIZE,
            "Failed to enumerate remotes for system installation: %s",
            local_error->message);
      n_system_remotes = system_remotes->len;
    }

// Downloading from user remotes in the Flatpak is unsupported.
#ifndef SANDBOXED_LIBFLATPAK
  if (self->user != NULL)
    {
      user_remotes = flatpak_installation_list_remotes (
          self->user, cancellable, &local_error);
      if (user_remotes == NULL)
        return dex_future_new_reject (
            BZ_FLATPAK_ERROR,
            BZ_FLATPAK_ERROR_CANNOT_INITIALIZE,
            "Failed to enumerate remotes for user installation: %s",
            local_error->message);
      n_user_remotes = user_remotes->len;
    }
#endif

  for (guint i = 0; i < n_system_remotes + n_user_remotes; i++)
    {
      FlatpakRemote *remote = NULL;
      const char    *name   = NULL;

      if (i < n_system_remotes)
        remote = g_ptr_array_index (system_remotes, i);
      else
        remote = g_ptr_array_index (user_remotes, i - n_system_remotes);

      if (flatpak_remote_get_disabled (remote) ||
          flatpak_remote_get_noenumerate (remote))
        continue;

      name = flatpak_remote_get_name (remote);
      if (g_strcmp0 (name, "flathub") == 0)
        return dex_future_new_true ();
    }
  return dex_future_new_false ();
}

static DexFuture *
ensure_flathub_fiber (EnsureFlathubData *data)
{
  g_autoptr (BzFlatpakInstance) self   = NULL;
  GCancellable *cancellable            = data->cancellable;
  g_autoptr (GError) local_error       = NULL;
  g_autoptr (FlatpakRemote) sys_remote = NULL;
  g_autoptr (FlatpakRemote) usr_remote = NULL;
  gboolean result                      = FALSE;

  bz_weak_get_or_return_reject (self, data->self);

#define REPO_URL "https://dl.flathub.org/repo/flathub.flatpakrepo"

  if (self->system != NULL)
    sys_remote = flatpak_installation_get_remote_by_name (
        self->system, "flathub", cancellable, NULL);
  if (self->user != NULL)
    usr_remote = flatpak_installation_get_remote_by_name (
        self->user, "flathub", cancellable, NULL);

  if (sys_remote != NULL)
    {
      flatpak_remote_set_disabled (sys_remote, FALSE);
      flatpak_remote_set_noenumerate (sys_remote, FALSE);
      flatpak_remote_set_gpg_verify (sys_remote, TRUE);

      result = flatpak_installation_modify_remote (
          self->system, sys_remote, cancellable, &local_error);
      if (!result)
        return dex_future_new_reject (
            BZ_FLATPAK_ERROR,
            BZ_FLATPAK_ERROR_REMOTE_SYNCHRONIZATION_FAILURE,
            "Failed to modify existing system flathub remote: %s",
            local_error->message);
    }
#ifndef SANDBOXED_LIBFLATPAK
  else if (self->system != NULL)
#else
  else if (self->system != NULL && usr_remote == NULL)
#endif
    {
      g_autoptr (SoupMessage) message  = NULL;
      g_autoptr (GOutputStream) output = NULL;
      g_autoptr (GBytes) bytes         = NULL;
      g_autoptr (FlatpakRemote) remote = NULL;

      message = soup_message_new (SOUP_METHOD_GET, REPO_URL);
      output  = g_memory_output_stream_new_resizable ();
      result  = dex_await (
          bz_send_with_global_http_session_then_splice_into (message, output),
          &local_error);
      if (!result)
        return dex_future_new_reject (
            BZ_FLATPAK_ERROR,
            BZ_FLATPAK_ERROR_IO_MISBEHAVIOR,
            "Failed to retrieve flatpakrepo file from %s: %s",
            REPO_URL, local_error->message);

      bytes  = g_memory_output_stream_steal_as_bytes (G_MEMORY_OUTPUT_STREAM (output));
      remote = flatpak_remote_new_from_file ("flathub", bytes, &local_error);
      if (remote == NULL)
        return dex_future_new_reject (
            BZ_FLATPAK_ERROR,
            BZ_FLATPAK_ERROR_IO_MISBEHAVIOR,
            "Failed to construct flatpak remote from flatpakrepo file %s: %s",
            REPO_URL, local_error->message);

      flatpak_remote_set_gpg_verify (remote, TRUE);

      result = flatpak_installation_add_remote (
          self->system, remote, TRUE, cancellable, &local_error);
      if (!result)
        return dex_future_new_reject (
            BZ_FLATPAK_ERROR,
            BZ_FLATPAK_ERROR_REMOTE_SYNCHRONIZATION_FAILURE,
            "Failed to add flathub to system flatpak installation: %s",
            local_error->message);
    }

#ifndef SANDBOXED_LIBFLATPAK
  if (usr_remote != NULL)
    {
      flatpak_remote_set_disabled (usr_remote, FALSE);
      flatpak_remote_set_noenumerate (usr_remote, FALSE);
      flatpak_remote_set_gpg_verify (usr_remote, TRUE);

      result = flatpak_installation_modify_remote (
          self->user, usr_remote, cancellable, &local_error);
      if (!result)
        return dex_future_new_reject (
            BZ_FLATPAK_ERROR,
            BZ_FLATPAK_ERROR_REMOTE_SYNCHRONIZATION_FAILURE,
            "Failed to modify existing user flathub remote: %s",
            local_error->message);
    }
#endif

  return dex_future_new_true ();
}

static DexFuture *
load_local_ref_fiber (LoadLocalRefData *data)
{
  g_autoptr (BzFlatpakInstance) self = NULL;
  GFile        *file                 = data->file;
  GCancellable *cancellable          = data->cancellable;
  gboolean      result               = FALSE;
  g_autoptr (GError) local_error     = NULL;
  g_autofree char *uri               = NULL;
  g_autofree char *path              = NULL;

  bz_weak_get_or_return_reject (self, data->self);

  uri  = g_file_get_uri (file);
  path = g_file_get_path (file);
  if (uri == NULL)
    uri = g_strdup_printf ("file://%s", path);

  if (g_str_has_suffix (uri, ".flatpakref"))
    {
      const char *resolved_uri      = NULL;
      g_autoptr (GKeyFile) key_file = g_key_file_new ();
      g_autofree char *name         = NULL;

      if (g_str_has_prefix (uri, "flatpak+https"))
        resolved_uri = uri + strlen ("flatpak+");
      else
        resolved_uri = uri;

      key_file = g_key_file_new ();

      if (g_str_has_prefix (resolved_uri, "http"))
        {
          g_autoptr (SoupMessage) message  = NULL;
          g_autoptr (GOutputStream) output = NULL;
          g_autoptr (GBytes) bytes         = NULL;

          message = soup_message_new (SOUP_METHOD_GET, resolved_uri);
          output  = g_memory_output_stream_new_resizable ();
          result  = dex_await (
              bz_send_with_global_http_session_then_splice_into (message, output),
              &local_error);
          if (!result)
            return dex_future_new_reject (
                BZ_FLATPAK_ERROR,
                BZ_FLATPAK_ERROR_IO_MISBEHAVIOR,
                "Failed to retrieve flatpakref file from %s: %s",
                resolved_uri, local_error->message);

          bytes  = g_memory_output_stream_steal_as_bytes (G_MEMORY_OUTPUT_STREAM (output));
          result = g_key_file_load_from_bytes (key_file, bytes, G_KEY_FILE_NONE, &local_error);
        }
      else if (path != NULL)
        result = g_key_file_load_from_file (
            key_file, path, G_KEY_FILE_NONE, &local_error);
      else
        local_error = g_error_new (
            G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
            "Cannot handle URIs of this type");

      if (!result)
        return dex_future_new_reject (
            BZ_FLATPAK_ERROR,
            BZ_FLATPAK_ERROR_IO_MISBEHAVIOR,
            "Failed to load flatpakref '%s' into a key file: %s",
            uri, local_error->message);

      name = g_key_file_get_string (key_file, "Flatpak Ref", "Name", &local_error);
      if (name == NULL)
        return dex_future_new_reject (
            BZ_FLATPAK_ERROR,
            BZ_FLATPAK_ERROR_IO_MISBEHAVIOR,
            "Failed to load locate \"Name\" key in flatpakref '%s': %s",
            uri, local_error->message);

      {
        g_autoptr (BzBackendNotification) notif = NULL;

        notif = bz_backend_notification_new ();
        bz_backend_notification_set_kind (notif, BZ_BACKEND_NOTIFICATION_KIND_PRESENT_ID);
        bz_backend_notification_set_generic_id (notif, name);

        send_notif_all (self, notif, TRUE);
      }

      return dex_future_new_take_string (g_steal_pointer (&name));
    }
  else
    /* This is a bundle ref */
    {
      g_autoptr (FlatpakBundleRef) bref        = NULL;
      const char          *name                = NULL;
      const char          *origin              = NULL;
      FlatpakInstallation *add_to_installation = NULL;
      g_autoptr (FlatpakRemote) remote         = NULL;
      g_autoptr (BzFlatpakEntry) entry         = NULL;
      g_autoptr (GBytes) appstream_gz          = NULL;
      g_autoptr (GBytes) appstream             = NULL;
      g_autoptr (AsComponent) component        = NULL;

      if (path == NULL)
        return dex_future_new_reject (
            BZ_FLATPAK_ERROR,
            BZ_FLATPAK_ERROR_IO_MISBEHAVIOR,
            "Cannot load '%s' as a flatpak bundle: URI is not a local file",
            uri);

      bref = flatpak_bundle_ref_new (file, &local_error);
      if (bref == NULL)
        return dex_future_new_reject (
            BZ_FLATPAK_ERROR,
            BZ_FLATPAK_ERROR_IO_MISBEHAVIOR,
            "Failed to load local flatpak bundle '%s': %s",
            path,
            local_error->message);

      name   = flatpak_ref_get_name (FLATPAK_REF (bref));
      origin = flatpak_bundle_ref_get_origin (bref);

      if (self->system != NULL)
        add_to_installation = self->system;
      else if (self->user != NULL)
        add_to_installation = self->user;

      /* First check if we already should have the origin remote
         installed */
      if (self->system != NULL)
        {
          g_autoptr (GPtrArray) remotes = NULL;

          remotes = flatpak_installation_list_remotes (
              self->system, NULL, NULL);
          if (remotes != NULL)
            {
              for (guint i = 0; i < remotes->len; i++)
                {
                  FlatpakRemote *existing = NULL;
                  const char    *url      = NULL;

                  existing = g_ptr_array_index (remotes, i);
                  url      = flatpak_remote_get_url (existing);

                  if (url != NULL &&
                      g_strcmp0 (url, origin) == 0)
                    {
                      remote              = g_object_ref (existing);
                      add_to_installation = NULL;
                      break;
                    }
                }
            }
        }
      if (self->user != NULL)
        {
          g_autoptr (GPtrArray) remotes = NULL;

          remotes = flatpak_installation_list_remotes (
              self->user, NULL, NULL);
          if (remotes != NULL)
            {
              for (guint i = 0; i < remotes->len; i++)
                {
                  FlatpakRemote *existing = NULL;
                  const char    *url      = NULL;

                  existing = g_ptr_array_index (remotes, i);
                  url      = flatpak_remote_get_url (existing);

                  if (url != NULL &&
                      g_strcmp0 (url, origin) == 0)
                    {
                      remote              = g_object_ref (existing);
                      add_to_installation = NULL;
                      break;
                    }
                }
            }
        }

      if (add_to_installation != NULL)
        {
          g_autoptr (FlatpakRemote) config_remote = NULL;
          g_autofree char *remote_name            = NULL;

          /* Configure and sync the new remote */
          remote_name = g_strdup_printf ("%s-bazaar-origin", name);

          config_remote = flatpak_remote_new (remote_name);
          flatpak_remote_set_url (config_remote, origin);
          flatpak_remote_set_disabled (config_remote, FALSE);
          flatpak_remote_set_noenumerate (config_remote, FALSE);
          flatpak_remote_set_gpg_verify (config_remote, TRUE);

          result = flatpak_installation_add_remote (
              add_to_installation, config_remote, FALSE, cancellable, NULL);
          if (result)
            {
              {
                g_autoptr (BzBackendNotification) notif = NULL;

                notif = bz_backend_notification_new ();
                bz_backend_notification_set_kind (notif, BZ_BACKEND_NOTIFICATION_KIND_INVALIDATE_REMOTES);

                send_notif_all (self, notif, TRUE);
              }

              remote = flatpak_installation_get_remote_by_name (
                  add_to_installation, remote_name, cancellable, NULL);
              if (remote != NULL)
                {
                  {
                    g_autoptr (BzBackendNotification) notif = NULL;

                    notif = bz_backend_notification_new ();
                    bz_backend_notification_set_kind (notif, BZ_BACKEND_NOTIFICATION_KIND_REMOTE_SYNC_START);
                    bz_backend_notification_set_remote_name (notif, remote_name);

                    send_notif_all (self, notif, TRUE);
                  }
                  result = dex_await (
                      retrieve_refs_for_enumerable_remote (
                          self, cancellable, remote_name, add_to_installation, remote),
                      NULL);
                  {
                    g_autoptr (BzBackendNotification) notif = NULL;

                    notif = bz_backend_notification_new ();
                    bz_backend_notification_set_kind (notif, BZ_BACKEND_NOTIFICATION_KIND_REMOTE_SYNC_FINISH);
                    bz_backend_notification_set_remote_name (notif, remote_name);

                    send_notif_all (self, notif, TRUE);
                  }
                }
            }
        }

      appstream_gz = flatpak_bundle_ref_get_appstream (bref);
      if (appstream_gz != NULL)
        {
          appstream = bz_decompress_gz_bytes (appstream_gz, NULL, &local_error);
          if (appstream == NULL)
            {
              g_warning ("Failed to decompress AppStream data: %s", local_error->message);
              g_clear_error (&local_error);
            }
          else
            {
              g_autoptr (XbBuilderSource) source = NULL;

              source = xb_builder_source_new ();
              if (!xb_builder_source_load_bytes (source, appstream,
                                                 XB_BUILDER_SOURCE_FLAG_LITERAL_TEXT,
                                                 &local_error))
                {
                  g_warning ("Failed to load AppStream bytes into xmlb: %s", local_error->message);
                  g_clear_error (&local_error);
                }
              else
                {
                  g_autoptr (XbSilo) silo = NULL;

                  silo = build_silo (source, NULL, &local_error);
                  if (silo == NULL)
                    {
                      g_warning ("Failed to compile xmlb silo: %s", local_error->message);
                      g_clear_error (&local_error);
                    }
                  else
                    {
                      component = extract_first_component_for_silo (silo, &local_error);
                      if (component == NULL && local_error != NULL)
                        {
                          g_warning ("Failed to parse component: %s", local_error->message);
                          g_clear_error (&local_error);
                        }
                    }
                }
            }
        }

      entry = bz_flatpak_entry_new_for_ref (
          FLATPAK_REF (bref),
          remote,
          FALSE,
          component,
          NULL,
          &local_error);
      if (entry == NULL)
        return dex_future_new_reject (
            BZ_FLATPAK_ERROR,
            BZ_FLATPAK_ERROR_IO_MISBEHAVIOR,
            "Failed to parse information from flatpak bundle '%s': %s",
            path,
            local_error->message);

      return dex_future_new_for_object (entry);
    }
}

static DexFuture *
retrieve_remote_refs_fiber (GatherRefsData *data)
{
  g_autoptr (BzFlatpakInstance) self        = NULL;
  GCancellable *cancellable                 = data->cancellable;
  g_autoptr (GError) local_error            = NULL;
  g_autoptr (GPtrArray) system_remotes      = NULL;
  guint n_system_remotes                    = 0;
  g_autoptr (GPtrArray) user_remotes        = NULL;
  guint n_user_remotes                      = 0;
  g_autoptr (GHashTable) blocked_names_hash = NULL;
  g_autoptr (GPtrArray) jobs                = NULL;
  g_autoptr (GPtrArray) job_names           = NULL;
  g_autoptr (DexFuture) future              = NULL;
  gboolean result                           = FALSE;
  g_autoptr (GString) error_string          = NULL;

  bz_weak_get_or_return_reject (self, data->self);

  if (self->system != NULL)
    {
      system_remotes = flatpak_installation_list_remotes (
          self->system, cancellable, &local_error);
      if (system_remotes == NULL)
        SEND_AND_RETURN_ERROR (
            self, TRUE,
            BZ_FLATPAK_ERROR_CANNOT_INITIALIZE,
            "Failed to enumerate remotes for system installation: %s",
            local_error->message);
      n_system_remotes = system_remotes->len;
    }

  if (self->user != NULL)
    {
      user_remotes = flatpak_installation_list_remotes (
          self->user, cancellable, &local_error);
      if (user_remotes == NULL)
        SEND_AND_RETURN_ERROR (
            self, TRUE,
            BZ_FLATPAK_ERROR_CANNOT_INITIALIZE,
            "Failed to enumerate remotes for user installation: %s",
            local_error->message);
      n_user_remotes = user_remotes->len;
    }

  if (n_user_remotes + n_system_remotes == 0)
    return dex_future_new_true ();

  jobs      = g_ptr_array_new_with_free_func (dex_unref);
  job_names = g_ptr_array_new_with_free_func (g_free);

  for (guint i = 0; i < n_system_remotes + n_user_remotes; i++)
    {
      FlatpakInstallation *installation              = NULL;
      FlatpakRemote       *remote                    = NULL;
      const char          *name                      = NULL;
      g_autoptr (RetrieveRefsForRemoteData) job_data = NULL;
      g_autoptr (DexFuture) job_future               = NULL;

      if (i < n_system_remotes)
        {
          installation = self->system;
          remote       = g_ptr_array_index (system_remotes, i);
        }
      else
        {
          installation = self->user;
          remote       = g_ptr_array_index (user_remotes, i - n_system_remotes);
        }

      name = flatpak_remote_get_name (remote);
      {
        g_autoptr (BzBackendNotification) notif = NULL;

        notif = bz_backend_notification_new ();
        bz_backend_notification_set_kind (notif, BZ_BACKEND_NOTIFICATION_KIND_REMOTE_SYNC_START);
        bz_backend_notification_set_remote_name (notif, name);

        send_notif_all (self, notif, TRUE);
      }

      job_data               = retrieve_refs_for_remote_data_new ();
      job_data->parent       = gather_refs_data_ref (data);
      job_data->installation = g_object_ref (installation);
      job_data->remote       = g_object_ref (remote);

      job_future = dex_scheduler_spawn (
          self->scheduler,
          bz_get_dex_stack_size (),
          (DexFiberFunc) retrieve_refs_for_remote_fiber,
          retrieve_refs_for_remote_data_ref (job_data),
          retrieve_refs_for_remote_data_unref);

      g_ptr_array_add (jobs, g_steal_pointer (&job_future));
      g_ptr_array_add (job_names, g_strdup (name));
    }

  if (jobs->len == 0)
    return dex_future_new_true ();

  result = dex_await (dex_future_allv (
                          (DexFuture *const *) jobs->pdata,
                          jobs->len),
                      NULL);
  if (!result)
    error_string = g_string_new ("No remotes could be synchronized:\n\n");

  for (guint i = 0; i < jobs->len; i++)
    {
      DexFuture *job_future = NULL;
      char      *name       = NULL;

      job_future = g_ptr_array_index (jobs, i);
      name       = g_ptr_array_index (job_names, i);

      dex_future_get_value (job_future, &local_error);
      if (local_error != NULL)
        {
          if (error_string == NULL)
            error_string = g_string_new ("Some remotes couldn't be fully sychronized:\n");
          g_string_append_printf (error_string, "\n%s failed because: %s\n", name, local_error->message);
        }
      g_clear_pointer (&local_error, g_error_free);
    }

  if (result)
    {
      if (error_string != NULL)
        return dex_future_new_take_string (
            g_string_free_and_steal (g_steal_pointer (&error_string)));
      else
        return dex_future_new_true ();
    }
  else
    return dex_future_new_reject (
        BZ_FLATPAK_ERROR,
        BZ_FLATPAK_ERROR_REMOTE_SYNCHRONIZATION_FAILURE,
        "%s", error_string->str);
}

static DexFuture *
retrieve_refs_for_enumerable_remote (BzFlatpakInstance   *self,
                                     GCancellable        *cancellable,
                                     const char          *remote_name,
                                     FlatpakInstallation *installation,
                                     FlatpakRemote       *remote)
{
  g_autoptr (GError) local_error        = NULL;
  gboolean result                       = FALSE;
  g_autoptr (GFile) appstream_dir       = NULL;
  g_autofree char *appstream_dir_path   = NULL;
  g_autofree char *appstream_xml_path   = NULL;
  g_autoptr (GFile) appstream_xml       = NULL;
  g_autoptr (XbBuilderSource) source    = NULL;
  g_autoptr (XbSilo) silo               = NULL;
  g_autoptr (XbNode) root               = NULL;
  g_autoptr (GPtrArray) children        = NULL;
  g_autoptr (GHashTable) component_hash = NULL;
  g_autoptr (GPtrArray) refs            = NULL;

  g_debug ("Remote '%s' is enumerable, listing all remote refs", remote_name);

  result = flatpak_installation_update_remote_sync (
      installation,
      remote_name,
      cancellable,
      &local_error);
  if (!result)
    SEND_AND_RETURN_ERROR (
        self, TRUE,
        BZ_FLATPAK_ERROR_REMOTE_SYNCHRONIZATION_FAILURE,
        "Failed to synchronize remote '%s': %s",
        remote_name,
        local_error->message);

  result = flatpak_installation_update_appstream_full_sync (
      installation,
      remote_name,
      NULL, NULL, NULL, NULL,
      cancellable,
      &local_error);
  if (!result)
    SEND_AND_RETURN_ERROR (
        self, TRUE,
        BZ_FLATPAK_ERROR_REMOTE_SYNCHRONIZATION_FAILURE,
        "Failed to synchronize appstream data for remote '%s': %s",
        remote_name,
        local_error->message);

  appstream_dir = flatpak_remote_get_appstream_dir (remote, NULL);
  if (appstream_dir == NULL)
    SEND_AND_RETURN_ERROR (
        self, TRUE,
        BZ_FLATPAK_ERROR_IO_MISBEHAVIOR,
        "Failed to locate appstream directory for remote '%s': %s",
        remote_name,
        local_error->message);

  appstream_dir_path = g_file_get_path (appstream_dir);
  appstream_xml_path = g_build_filename (appstream_dir_path, "appstream.xml.gz", NULL);
  if (!g_file_test (appstream_xml_path, G_FILE_TEST_EXISTS))
    SEND_AND_RETURN_ERROR (
        self, TRUE,
        BZ_FLATPAK_ERROR_IO_MISBEHAVIOR,
        "Failed to verify existence of appstream "
        "bundle download at path %s for remote '%s'",
        appstream_xml_path,
        remote_name);

  appstream_xml = g_file_new_for_path (appstream_xml_path);

  source = xb_builder_source_new ();
  result = xb_builder_source_load_file (
      source,
      appstream_xml,
      XB_BUILDER_SOURCE_FLAG_WATCH_FILE |
          XB_BUILDER_SOURCE_FLAG_LITERAL_TEXT,
      cancellable,
      &local_error);
  if (!result)
    SEND_AND_RETURN_ERROR (
        self, TRUE,
        BZ_FLATPAK_ERROR_IO_MISBEHAVIOR,
        "Failed to load binary xml from appstream bundle "
        "download at path %s for remote '%s': %s",
        appstream_xml_path,
        remote_name,
        local_error->message);

  silo = build_silo (source, cancellable, &local_error);

#ifdef __GLIBC__
  /* From gnome-software/plugins/core/gs-plugin-appstream.c
   *
   * https://gitlab.gnome.org/GNOME/gnome-software/-/issues/941
   * libxmlb <= 0.3.22 makes lots of temporary heap allocations parsing large XMLs
   * trim the heap after parsing to control RSS growth. */
  malloc_trim (0);
#endif

  if (silo == NULL)
    SEND_AND_RETURN_ERROR (
        self, TRUE,
        BZ_FLATPAK_ERROR_IO_MISBEHAVIOR,
        "Failed to compile binary xml silo from appstream bundle "
        "download at path %s for remote '%s': %s",
        appstream_xml_path,
        remote_name,
        local_error->message);

  root     = xb_silo_get_root (silo);
  children = xb_node_get_children (root);

  component_hash = g_hash_table_new (g_str_hash, g_str_equal);

  for (guint i = 0; i < children->len; i++)
    {
      XbNode      *component_node = NULL;
      AsComponent *component      = NULL;
      const char  *id             = NULL;

      component_node = g_ptr_array_index (children, i);
      component      = bz_parse_component_for_node (component_node, &local_error);

      if (component == NULL)
        {
          SEND_AND_RETURN_ERROR (
              self, TRUE,
              BZ_FLATPAK_ERROR_APPSTREAM_FAILURE,
              "Failed to parse appstream component from appstream bundle silo "
              "originating from download at path %s for remote '%s': %s",
              appstream_xml_path,
              remote_name,
              local_error->message);
        }

      id = as_component_get_id (component);
      g_hash_table_replace (component_hash, (gpointer) id, component);
    }

  refs = flatpak_installation_list_remote_refs_sync (
      installation, remote_name, cancellable, &local_error);
  if (refs == NULL)
    SEND_AND_RETURN_ERROR (
        self, TRUE,
        BZ_FLATPAK_ERROR_REMOTE_SYNCHRONIZATION_FAILURE,
        "Failed to enumerate refs for remote '%s': %s",
        remote_name,
        local_error->message);

  {
    g_autoptr (BzBackendNotification) notif = NULL;

    notif = bz_backend_notification_new ();
    bz_backend_notification_set_kind (notif, BZ_BACKEND_NOTIFICATION_KIND_TELL_INCOMING);
    bz_backend_notification_set_n_incoming (notif, refs->len);

    send_notif_all (self, notif, TRUE);
  }

  /* Ensure the receiving side of the channel gets
   * runtimes first, then addons, then applications
   */
  g_ptr_array_sort_values_with_data (
      refs, (GCompareDataFunc) cmp_rref, component_hash);

  for (guint i = 0; i < refs->len; i++)
    {
      FlatpakRemoteRef *rref           = NULL;
      const char       *name           = NULL;
      AsComponent      *component      = NULL;
      g_autoptr (BzFlatpakEntry) entry = NULL;

      rref      = g_ptr_array_index (refs, i);
      name      = flatpak_ref_get_name (FLATPAK_REF (rref));
      component = g_hash_table_lookup (component_hash, name);
      if (component == NULL)
        {
          g_autofree char *desktop_id = NULL;

          desktop_id = g_strdup_printf ("%s.desktop", name);
          component  = g_hash_table_lookup (component_hash, desktop_id);
        }

      entry = bz_flatpak_entry_new_for_ref (
          FLATPAK_REF (rref),
          remote,
          installation == self->user,
          component,
          appstream_dir_path,
          NULL);

      if (entry != NULL)
        {
          g_autoptr (BzBackendNotification) notif = NULL;

          notif = bz_backend_notification_new ();
          bz_backend_notification_set_kind (notif, BZ_BACKEND_NOTIFICATION_KIND_REPLACE_ENTRY);
          bz_backend_notification_set_entry (notif, BZ_ENTRY (entry));

          send_notif_all (self, notif, TRUE);
        }
      else
        {
          g_autoptr (BzBackendNotification) notif = NULL;

          notif = bz_backend_notification_new ();
          bz_backend_notification_set_kind (notif, BZ_BACKEND_NOTIFICATION_KIND_TELL_INCOMING);
          bz_backend_notification_set_n_incoming (notif, -1);

          send_notif_all (self, notif, TRUE);
        }
    }

  return dex_future_new_true ();
}

static DexFuture *
retrieve_refs_for_noenumerable_remote (BzFlatpakInstance   *self,
                                       GCancellable        *cancellable,
                                       const char          *remote_name,
                                       FlatpakInstallation *installation,
                                       FlatpakRemote       *remote)
{
  g_autoptr (GError) local_error       = NULL;
  g_autoptr (GPtrArray) installed_apps = NULL;
  guint matched                        = 0;

  installed_apps = flatpak_installation_list_installed_refs_by_kind (
      installation,
      FLATPAK_REF_KIND_APP,
      cancellable,
      &local_error);

  if (installed_apps == NULL)
    SEND_AND_RETURN_ERROR (
        self, TRUE,
        BZ_FLATPAK_ERROR_LOCAL_SYNCHRONIZATION_FAILURE,
        "Failed to enumerate installed apps for non-enumerable remote '%s': %s",
        remote_name,
        local_error->message);

  g_debug ("Found %u total installed apps, filtering for remote '%s'",
           installed_apps->len, remote_name);

  for (guint i = 0; i < installed_apps->len; i++)
    {
      FlatpakInstalledRef *iref         = NULL;
      const char          *ref_origin   = NULL;
      g_autoptr (AsComponent) component = NULL;
      g_autoptr (BzFlatpakEntry) entry  = NULL;
      g_autoptr (GBytes) appstream_gz   = NULL;

      iref       = g_ptr_array_index (installed_apps, i);
      ref_origin = flatpak_installed_ref_get_origin (iref);

      if (g_strcmp0 (ref_origin, remote_name) != 0)
        continue;

      matched++;

      appstream_gz = flatpak_installed_ref_load_appdata (iref, cancellable, NULL);
      if (appstream_gz != NULL)
        {
          g_autoptr (GBytes) appstream       = NULL;
          g_autoptr (XbBuilderSource) source = NULL;
          g_autoptr (XbSilo) silo            = NULL;
          g_autoptr (GError) appstream_error = NULL;

          appstream = bz_decompress_gz_bytes (appstream_gz, cancellable, &appstream_error);
          if (appstream == NULL)
            {
              g_info ("Could not decompress appstream for installed ref: %s",
                      appstream_error ? appstream_error->message : "unknown error");
              goto create_entry;
            }

          source = xb_builder_source_new ();
          if (!xb_builder_source_load_bytes (source, appstream,
                                             XB_BUILDER_SOURCE_FLAG_LITERAL_TEXT,
                                             &appstream_error))
            {
              g_info ("Could not load appstream bytes: %s",
                      appstream_error ? appstream_error->message : "unknown error");
              goto create_entry;
            }

          silo = build_silo (source, cancellable, &appstream_error);
          if (silo == NULL)
            {
              g_info ("Could not build silo from appstream: %s",
                      appstream_error ? appstream_error->message : "unknown error");
              goto create_entry;
            }

          component = extract_first_component_for_silo (silo, &appstream_error);
          if (component == NULL)
            {
              g_info ("Could not parse appstream component: %s",
                      appstream_error ? appstream_error->message : "unknown error");
            }
        }

    create_entry:
      entry = bz_flatpak_entry_new_for_ref (
          FLATPAK_REF (iref),
          remote,
          installation == self->user,
          component,
          NULL,
          NULL);

      if (entry != NULL)
        {
          g_autoptr (BzBackendNotification) notif = NULL;

          notif = bz_backend_notification_new ();
          bz_backend_notification_set_kind (notif, BZ_BACKEND_NOTIFICATION_KIND_REPLACE_ENTRY);
          bz_backend_notification_set_entry (notif, BZ_ENTRY (entry));

          send_notif_all (self, notif, TRUE);
        }
    }

  g_debug ("Found %u installed apps from non-enumerable remote '%s'", matched, remote_name);

  {
    g_autoptr (BzBackendNotification) notif = NULL;

    notif = bz_backend_notification_new ();
    bz_backend_notification_set_kind (notif, BZ_BACKEND_NOTIFICATION_KIND_TELL_INCOMING);
    bz_backend_notification_set_n_incoming (notif, matched);

    send_notif_all (self, notif, TRUE);
  }

  return dex_future_new_true ();
}

static DexFuture *
retrieve_refs_for_remote_fiber (RetrieveRefsForRemoteData *data)
{
  FlatpakInstallation *installation  = data->installation;
  FlatpakRemote       *remote        = data->remote;
  g_autoptr (BzFlatpakInstance) self = NULL;
  const char *remote_name            = NULL;
  gboolean    is_noenumerate         = FALSE;
  g_autoptr (DexFuture) ret          = NULL;

  bz_weak_get_or_return_reject (self, data->parent->self);

  remote_name    = flatpak_remote_get_name (remote);
  is_noenumerate = flatpak_remote_get_noenumerate (remote);

  /* the fedora flatpak repos cause too many issues */
  if (strstr (remote_name, "fedora") != NULL)
    is_noenumerate = TRUE;

#ifdef SANDBOXED_LIBFLATPAK
  if (is_noenumerate || installation == self->user)
#else
  if (is_noenumerate)
#endif
    ret = retrieve_refs_for_noenumerable_remote (
        self, data->parent->cancellable,
        remote_name, installation, remote);
  else
    ret = retrieve_refs_for_enumerable_remote (
        self, data->parent->cancellable,
        remote_name, installation, remote);

  {
    g_autoptr (BzBackendNotification) notif = NULL;

    notif = bz_backend_notification_new ();
    bz_backend_notification_set_kind (notif, BZ_BACKEND_NOTIFICATION_KIND_REMOTE_SYNC_FINISH);
    bz_backend_notification_set_remote_name (notif, remote_name);

    send_notif_all (self, notif, TRUE);
  }

  return g_steal_pointer (&ret);
}

static DexFuture *
retrieve_installs_fiber (GatherRefsData *data)
{
  g_autoptr (BzFlatpakInstance) self = NULL;
  GCancellable *cancellable          = data->cancellable;
  g_autoptr (GError) local_error     = NULL;
  g_autoptr (GPtrArray) system_refs  = NULL;
  guint n_system_refs                = 0;
  g_autoptr (GPtrArray) user_refs    = NULL;
  guint n_user_refs                  = 0;
  g_autoptr (GHashTable) ids         = NULL;

  bz_weak_get_or_return_reject (self, data->self);

  if (self->system != NULL)
    {
      flatpak_installation_drop_caches (
          self->system, cancellable, NULL);
      system_refs = flatpak_installation_list_installed_refs (
          self->system, cancellable, &local_error);
      if (system_refs == NULL)
        SEND_AND_RETURN_ERROR (
            self, TRUE,
            BZ_FLATPAK_ERROR_LOCAL_SYNCHRONIZATION_FAILURE,
            "Failed to discover installed refs for system installation: %s",
            local_error->message);
      n_system_refs = system_refs->len;
    }

  if (self->user != NULL)
    {
      flatpak_installation_drop_caches (
          self->user, cancellable, NULL);
      user_refs = flatpak_installation_list_installed_refs (
          self->user, cancellable, &local_error);
      if (user_refs == NULL)
        SEND_AND_RETURN_ERROR (
            self, TRUE,
            BZ_FLATPAK_ERROR_LOCAL_SYNCHRONIZATION_FAILURE,
            "Failed to discover installed refs for user installation: %s",
            local_error->message);
      n_user_refs = user_refs->len;
    }

  ids = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

  for (guint i = 0; i < n_system_refs + n_user_refs; i++)
    {
      gboolean             user      = FALSE;
      FlatpakInstalledRef *iref      = NULL;
      const char          *version   = NULL;
      g_autofree char     *unique_id = NULL;

      if (i < n_system_refs)
        {
          user = FALSE;
          iref = g_ptr_array_index (system_refs, i);
        }
      else
        {
          user = TRUE;
          iref = g_ptr_array_index (user_refs, i - n_system_refs);
        }

      version = flatpak_installed_ref_get_appdata_version (iref);

      unique_id = bz_flatpak_ref_format_unique (FLATPAK_REF (iref), user);

      g_hash_table_replace (ids,
                            g_steal_pointer (&unique_id),
                            g_strdup (version != NULL ? version : ""));
    }

  return dex_future_new_take_boxed (
      G_TYPE_HASH_TABLE, g_steal_pointer (&ids));
}

static gboolean
should_skip_extension_ref (FlatpakInstalledRef *iref)
{
  const gchar *ref_name = flatpak_ref_get_name (FLATPAK_REF (iref));

  /* These get updated with their parents and look really bad in the UI */
  return g_str_has_suffix (ref_name, ".Locale") ||
         g_str_has_suffix (ref_name, ".Debug") ||
         g_str_has_suffix (ref_name, ".Sources");
}

static DexFuture *
retrieve_updates_fiber (GatherRefsData *data)
{
  g_autoptr (BzFlatpakInstance) self = NULL;
  GCancellable *cancellable          = data->cancellable;
  g_autoptr (GError) local_error     = NULL;
  g_autoptr (GPtrArray) system_refs  = NULL;
  guint n_sys_refs                   = 0;
  g_autoptr (GPtrArray) user_refs    = NULL;
  guint n_user_refs                  = 0;
  g_autoptr (GPtrArray) ids          = NULL;

  bz_weak_get_or_return_reject (self, data->self);

  if (self->system != NULL)
    {
      system_refs = flatpak_installation_list_installed_refs_for_update (
          self->system, cancellable, &local_error);
      if (system_refs == NULL)
        SEND_AND_RETURN_ERROR (
            self, TRUE,
            BZ_FLATPAK_ERROR_REMOTE_SYNCHRONIZATION_FAILURE,
            "Failed to discover update-elligible refs for system installation: %s",
            local_error->message);
      n_sys_refs = system_refs->len;
    }

#ifndef SANDBOXED_LIBFLATPAK
  if (self->user != NULL)
#else
  if (self->user != NULL && g_getenv ("FLATPAK_BINARY") != NULL)
#endif
    {
      user_refs = flatpak_installation_list_installed_refs_for_update (
          self->user, cancellable, &local_error);
      if (user_refs == NULL)
        SEND_AND_RETURN_ERROR (
            self, TRUE,
            BZ_FLATPAK_ERROR_REMOTE_SYNCHRONIZATION_FAILURE,
            "Failed to discover update-elligible refs for user installation: %s",
            local_error->message);
      n_user_refs = user_refs->len;
    }

  ids = g_ptr_array_new_with_free_func (g_free);

  for (guint i = 0; i < n_sys_refs + n_user_refs; i++)
    {
      gboolean             user = FALSE;
      FlatpakInstalledRef *iref = NULL;

      if (i < n_sys_refs)
        {
          user = FALSE;
          iref = g_ptr_array_index (system_refs, i);
        }
      else
        {
          user = TRUE;
          iref = g_ptr_array_index (user_refs, i - n_sys_refs);
        }

      if (should_skip_extension_ref (iref))
        continue;

      g_ptr_array_add (ids,
                       bz_flatpak_ref_format_unique (FLATPAK_REF (iref), user));
    }

  return dex_future_new_take_boxed (
      G_TYPE_PTR_ARRAY, g_steal_pointer (&ids));
}

static DexFuture *
list_repositories_fiber (ListReposData *data)
{
  g_autoptr (BzFlatpakInstance) self = NULL;
  GCancellable *cancellable          = NULL;
  g_autoptr (GError) local_error     = NULL;
  g_autoptr (GPtrArray) system_repos = NULL;
  g_autoptr (GPtrArray) user_repos   = NULL;
  g_autoptr (GListStore) repos       = NULL;

  cancellable = data->cancellable;

  bz_weak_get_or_return_reject (self, data->self);

  repos = g_list_store_new (BZ_TYPE_REPOSITORY);

  if (self->system != NULL)
    {
      system_repos = flatpak_installation_list_remotes (
          self->system, cancellable, &local_error);
      if (system_repos == NULL)
        SEND_AND_RETURN_ERROR (
            self, TRUE,
            BZ_FLATPAK_ERROR_CANNOT_INITIALIZE,
            "Failed to enumerate remotes for system installation: %s",
            local_error->message);

      for (guint i = 0; i < system_repos->len; i++)
        {
          FlatpakRemote *remote         = NULL;
          g_autoptr (BzRepository) repo = NULL;

          remote = g_ptr_array_index (system_repos, i);
          repo   = g_object_new (BZ_TYPE_REPOSITORY,
                                 "name", flatpak_remote_get_name (remote),
                                 "title", flatpak_remote_get_title (remote),
                                 "url", flatpak_remote_get_url (remote),
                                 "is-user", FALSE,
                                 NULL);

          g_list_store_append (repos, repo);
        }
    }

  if (self->user != NULL)
    {
      user_repos = flatpak_installation_list_remotes (
          self->user, cancellable, &local_error);
      if (user_repos == NULL)
        SEND_AND_RETURN_ERROR (
            self, TRUE,
            BZ_FLATPAK_ERROR_CANNOT_INITIALIZE,
            "Failed to enumerate remotes for user installation: %s",
            local_error->message);

      for (guint i = 0; i < user_repos->len; i++)
        {
          FlatpakRemote *remote         = NULL;
          g_autoptr (BzRepository) repo = NULL;

          remote = g_ptr_array_index (user_repos, i);
          repo   = g_object_new (BZ_TYPE_REPOSITORY,
                                 "name", flatpak_remote_get_name (remote),
                                 "title", flatpak_remote_get_title (remote),
                                 "url", flatpak_remote_get_url (remote),
                                 "is-user", TRUE,
                                 NULL);

          g_list_store_append (repos, repo);
        }
    }

  return dex_future_new_for_object (g_steal_pointer (&repos));
}

static DexFuture *
transaction_fiber (TransactionData *data)
{
  g_autoptr (BzFlatpakInstance) self = NULL;
  GCancellable *cancellable          = data->cancellable;
  GPtrArray    *installations        = data->installs;
  GPtrArray    *updates              = data->updates;
  GPtrArray    *removals             = data->removals;
  DexChannel   *channel              = data->channel;
  g_autoptr (GError) local_error     = NULL;
  gboolean result                    = FALSE;
  g_autoptr (GPtrArray) transactions = NULL;
  g_autoptr (GPtrArray) entries      = NULL;
  g_autoptr (GPtrArray) jobs         = NULL;
  g_autoptr (GHashTable) errored     = NULL;

  bz_weak_get_or_return_reject (self, data->self);

  transactions = g_ptr_array_new_with_free_func (g_object_unref);
  entries      = g_ptr_array_new_with_free_func (g_object_unref);

  if (installations != NULL)
    {
      for (guint i = 0; i < installations->len; i++)
        {
          BzFlatpakEntry  *entry                     = NULL;
          const char      *bundle_path               = NULL;
          FlatpakRef      *ref                       = NULL;
          gboolean         is_user                   = FALSE;
          g_autofree char *ref_fmt                   = NULL;
          g_autoptr (FlatpakTransaction) transaction = NULL;

          entry       = g_ptr_array_index (installations, i);
          bundle_path = bz_flatpak_entry_get_bundle_path (entry);
          ref         = bz_flatpak_entry_get_ref (entry);
          is_user     = bz_flatpak_entry_is_user (BZ_FLATPAK_ENTRY (entry));
          ref_fmt     = flatpak_ref_format_ref (ref);

          if ((is_user && self->user == NULL) ||
              (!is_user && self->system == NULL))
            {
              dex_channel_close_send (channel);
              return dex_future_new_reject (
                  BZ_FLATPAK_ERROR,
                  BZ_FLATPAK_ERROR_TRANSACTION_FAILURE,
                  "Failed to append the update of %s to transaction "
                  "because its installation couldn't be found",
                  ref_fmt);
            }

          transaction = flatpak_transaction_new_for_installation (
              is_user
                  ? self->user_interactive
                  : self->system_interactive,
              cancellable, &local_error);
          if (transaction == NULL)
            {
              dex_channel_close_send (channel);
              return dex_future_new_reject (
                  BZ_FLATPAK_ERROR,
                  BZ_FLATPAK_ERROR_TRANSACTION_FAILURE,
                  "Failed to initialize potential transaction for installation: %s",
                  local_error->message);
            }

          if (bundle_path != NULL)
            /* Prioritize bundle installation */
            {
              g_autoptr (GFile) file = NULL;

              file   = g_file_new_for_path (bundle_path);
              result = flatpak_transaction_add_install_bundle (
                  transaction,
                  file,
                  NULL,
                  &local_error);
            }
          else
            result = flatpak_transaction_add_install (
                transaction,
                bz_entry_get_remote_repo_name (BZ_ENTRY (entry)),
                ref_fmt,
                NULL,
                &local_error);
          if (!result)
            {
              dex_channel_close_send (channel);
              return dex_future_new_reject (
                  BZ_FLATPAK_ERROR,
                  BZ_FLATPAK_ERROR_TRANSACTION_FAILURE,
                  "Failed to append the installation of %s to transaction: %s",
                  ref_fmt,
                  local_error->message);
            }

          g_ptr_array_add (transactions, g_steal_pointer (&transaction));
          g_ptr_array_add (entries, g_object_ref (entry));
          g_hash_table_replace (data->ref_to_entry_hash,
                                g_steal_pointer (&ref_fmt),
                                g_object_ref (entry));
        }
    }

  if (updates != NULL)
    {
      g_autoptr (FlatpakTransaction) user_transaction = NULL;
      g_autoptr (FlatpakTransaction) sys_transaction  = NULL;

      for (guint i = 0; i < updates->len; i++)
        {
          BzFlatpakEntry  *entry   = NULL;
          FlatpakRef      *ref     = NULL;
          gboolean         is_user = FALSE;
          g_autofree char *ref_fmt = NULL;

          entry   = g_ptr_array_index (updates, i);
          ref     = bz_flatpak_entry_get_ref (entry);
          is_user = bz_flatpak_entry_is_user (BZ_FLATPAK_ENTRY (entry));
          ref_fmt = flatpak_ref_format_ref (ref);

          if ((is_user && self->user == NULL) ||
              (!is_user && self->system == NULL))
            {
              dex_channel_close_send (channel);
              return dex_future_new_reject (
                  BZ_FLATPAK_ERROR,
                  BZ_FLATPAK_ERROR_TRANSACTION_FAILURE,
                  "Failed to append the update of %s to transaction "
                  "because its installation couldn't be found",
                  ref_fmt);
            }

          if (is_user && user_transaction == NULL)
            user_transaction = flatpak_transaction_new_for_installation (
                self->user_interactive,
                cancellable,
                &local_error);
          else if (!is_user && sys_transaction == NULL)
            sys_transaction = flatpak_transaction_new_for_installation (
                self->system_interactive,
                cancellable,
                &local_error);
          if ((is_user && user_transaction == NULL) ||
              (!is_user && sys_transaction == NULL))
            {
              dex_channel_close_send (channel);
              return dex_future_new_reject (
                  BZ_FLATPAK_ERROR,
                  BZ_FLATPAK_ERROR_TRANSACTION_FAILURE,
                  "Failed to initialize potential transaction for installation: %s",
                  local_error->message);
            }

          /* Put updates in one transaction to prevent dependency
             race-conditions, since the update list is most likely coming from
             this instance */
          result = flatpak_transaction_add_update (
              is_user
                  ? user_transaction
                  : sys_transaction,
              ref_fmt,
              NULL,
              NULL,
              &local_error);
          if (!result)
            {
              dex_channel_close_send (channel);
              return dex_future_new_reject (
                  BZ_FLATPAK_ERROR,
                  BZ_FLATPAK_ERROR_TRANSACTION_FAILURE,
                  "Failed to append the update of %s to transaction: %s",
                  ref_fmt,
                  local_error->message);
            }

          g_ptr_array_add (entries, g_object_ref (entry));
          g_hash_table_replace (data->ref_to_entry_hash,
                                g_steal_pointer (&ref_fmt),
                                g_object_ref (entry));
        }

      if (user_transaction != NULL)
        g_ptr_array_add (transactions, g_steal_pointer (&user_transaction));
      if (sys_transaction != NULL)
        g_ptr_array_add (transactions, g_steal_pointer (&sys_transaction));
    }

  if (removals != NULL)
    {
      for (guint i = 0; i < removals->len; i++)
        {
          BzFlatpakEntry  *entry                     = NULL;
          FlatpakRef      *ref                       = NULL;
          gboolean         is_user                   = FALSE;
          g_autofree char *ref_fmt                   = NULL;
          g_autoptr (FlatpakTransaction) transaction = NULL;

          entry   = g_ptr_array_index (removals, i);
          ref     = bz_flatpak_entry_get_ref (entry);
          is_user = bz_flatpak_entry_is_user (BZ_FLATPAK_ENTRY (entry));
          ref_fmt = flatpak_ref_format_ref (ref);

          if ((is_user && self->user == NULL) ||
              (!is_user && self->system == NULL))
            {
              dex_channel_close_send (channel);
              return dex_future_new_reject (
                  BZ_FLATPAK_ERROR,
                  BZ_FLATPAK_ERROR_TRANSACTION_FAILURE,
                  "Failed to append the removal of %s to transaction "
                  "because its installation couldn't be found",
                  ref_fmt);
            }

          transaction = flatpak_transaction_new_for_installation (
              is_user
                  ? self->user_interactive
                  : self->system_interactive,
              cancellable, &local_error);
          if (transaction == NULL)
            {
              dex_channel_close_send (channel);
              return dex_future_new_reject (
                  BZ_FLATPAK_ERROR,
                  BZ_FLATPAK_ERROR_TRANSACTION_FAILURE,
                  "Failed to initialize potential transaction for installation: %s",
                  local_error->message);
            }

          result = flatpak_transaction_add_uninstall (
              transaction,
              ref_fmt,
              &local_error);
          if (!result)
            {
              dex_channel_close_send (channel);
              return dex_future_new_reject (
                  BZ_FLATPAK_ERROR,
                  BZ_FLATPAK_ERROR_TRANSACTION_FAILURE,
                  "Failed to append the removal of %s to transaction: %s",
                  ref_fmt,
                  local_error->message);
            }

          g_ptr_array_add (transactions, g_steal_pointer (&transaction));
          g_ptr_array_add (entries, g_object_ref (entry));
          g_hash_table_replace (data->ref_to_entry_hash,
                                g_steal_pointer (&ref_fmt),
                                g_object_ref (entry));
        }
    }

  g_mutex_lock (&self->transactions_mutex);

#define REGISTER_CANCELLABLES(entry)                                                           \
  G_STMT_START                                                                                 \
  {                                                                                            \
    GPtrArray *cancellables = NULL;                                                            \
                                                                                               \
    cancellables = g_hash_table_lookup (self->ongoing_cancellables, entry);                    \
    if (cancellables != NULL)                                                                  \
      g_ptr_array_add (cancellables, g_object_ref (cancellable));                              \
    else                                                                                       \
      {                                                                                        \
        cancellables = g_ptr_array_new_with_free_func (g_object_unref);                        \
        g_ptr_array_add (cancellables, g_object_ref (cancellable));                            \
        g_hash_table_replace (self->ongoing_cancellables, g_object_ref (entry), cancellables); \
      }                                                                                        \
  }                                                                                            \
  G_STMT_END

  if (installations != NULL)
    {
      for (guint i = 0; i < installations->len; i++)
        {
          BzEntry *entry = NULL;

          entry = g_ptr_array_index (installations, i);
          REGISTER_CANCELLABLES (entry);
        }
    }
  if (removals != NULL)
    {
      for (guint i = 0; i < removals->len; i++)
        {
          BzEntry *entry = NULL;

          entry = g_ptr_array_index (removals, i);
          REGISTER_CANCELLABLES (entry);
        }
    }
  if (updates != NULL)
    {
      for (guint i = 0; i < updates->len; i++)
        {
          BzEntry *entry = NULL;

          entry = g_ptr_array_index (updates, i);
          REGISTER_CANCELLABLES (entry);
        }
    }

#undef REGISTER_CANCELLABLES
  g_mutex_unlock (&self->transactions_mutex);

  jobs = g_ptr_array_new_with_free_func (dex_unref);
  for (guint i = 0; i < transactions->len; i++)
    {
      FlatpakTransaction *transaction         = NULL;
      g_autoptr (TransactionJobData) job_data = NULL;

      transaction = g_ptr_array_index (transactions, i);

      job_data              = transaction_job_data_new ();
      job_data->parent      = transaction_data_ref (data);
      job_data->transaction = g_object_ref (transaction);

      g_ptr_array_add (
          jobs,
          dex_scheduler_spawn (
              self->scheduler,
              bz_get_dex_stack_size (),
              (DexFiberFunc) transaction_job_fiber,
              transaction_job_data_ref (job_data),
              transaction_job_data_unref));
    }

  dex_await (dex_future_all_racev (
                 (DexFuture *const *) jobs->pdata,
                 jobs->len),
             NULL);

  g_mutex_lock (&self->transactions_mutex);

#define UNREGISTER_CANCELLABLES(entry)                                      \
  G_STMT_START                                                              \
  {                                                                         \
    GPtrArray *cancellables = NULL;                                         \
                                                                            \
    cancellables = g_hash_table_lookup (self->ongoing_cancellables, entry); \
    if (cancellables != NULL)                                               \
      {                                                                     \
        g_ptr_array_remove (cancellables, cancellable);                     \
        if (cancellables->len == 0)                                         \
          g_hash_table_remove (self->ongoing_cancellables, entry);          \
      }                                                                     \
  }                                                                         \
  G_STMT_END

  if (installations != NULL)
    {
      for (guint i = 0; i < installations->len; i++)
        {
          BzEntry *entry = NULL;

          entry = g_ptr_array_index (installations, i);
          UNREGISTER_CANCELLABLES (entry);
        }
    }
  if (removals != NULL)
    {
      for (guint i = 0; i < removals->len; i++)
        {
          BzEntry *entry = NULL;

          entry = g_ptr_array_index (removals, i);
          UNREGISTER_CANCELLABLES (entry);
        }
    }
  if (updates != NULL)
    {
      for (guint i = 0; i < updates->len; i++)
        {
          BzEntry *entry = NULL;

          entry = g_ptr_array_index (updates, i);
          UNREGISTER_CANCELLABLES (entry);
        }
    }

#undef UNREGISTER_CANCELLABLES
  g_mutex_unlock (&self->transactions_mutex);

  if (data->send_futures->len > 0)
    dex_await (dex_future_allv (
                   (DexFuture *const *) data->send_futures->pdata,
                   data->send_futures->len),
               NULL);

  errored = g_hash_table_new_full (
      g_direct_hash, g_direct_equal,
      g_object_unref, (GDestroyNotify) g_error_free);
  for (guint i = 0; i < jobs->len; i++)
    {
      DexFuture *job   = NULL;
      BzEntry   *entry = NULL;

      job   = g_ptr_array_index (jobs, i);
      entry = g_ptr_array_index (entries, i);

      dex_future_get_value (job, &local_error);
      if (local_error != NULL)
        g_hash_table_replace (
            errored,
            g_object_ref (entry),
            g_steal_pointer (&local_error));
    }

  dex_channel_close_send (channel);
  return dex_future_new_take_boxed (G_TYPE_HASH_TABLE, g_steal_pointer (&errored));
}

static DexFuture *
transaction_job_fiber (TransactionJobData *data)
{
  TransactionData    *parent      = data->parent;
  FlatpakTransaction *transaction = data->transaction;
  GCancellable       *cancellable = parent->cancellable;
  g_autoptr (GError) local_error  = NULL;
  gboolean result                 = FALSE;

  g_signal_connect (transaction, "new-operation", G_CALLBACK (transaction_new_operation), parent);
  g_signal_connect (transaction, "operation-done", G_CALLBACK (transaction_operation_done), parent);
  g_signal_connect (transaction, "operation-error", G_CALLBACK (transaction_operation_error), parent);
  g_signal_connect (transaction, "ready", G_CALLBACK (transaction_ready), parent);

  result = flatpak_transaction_run (transaction, cancellable, &local_error);
  if (!result)
    return dex_future_new_reject (
        BZ_FLATPAK_ERROR,
        BZ_FLATPAK_ERROR_TRANSACTION_FAILURE,
        "Failed to run flatpak transaction on installation: %s",
        local_error->message);

  return dex_future_new_true ();
}

static void
transaction_new_operation (FlatpakTransaction          *transaction,
                           FlatpakTransactionOperation *operation,
                           FlatpakTransactionProgress  *progress,
                           TransactionData             *data)
{
  g_autoptr (BzFlatpakInstance) self                  = NULL;
  FlatpakTransactionOperationType kind                = 0;
  BzFlatpakEntry                 *entry               = NULL;
  g_autoptr (BzBackendTransactionOpPayload) payload   = NULL;
  g_autoptr (TransactionOperationData) operation_data = NULL;

  bz_weak_get_or_return (self, data->self);

  kind = flatpak_transaction_operation_get_operation_type (operation);
  if (kind == FLATPAK_TRANSACTION_OPERATION_INSTALL ||
      kind == FLATPAK_TRANSACTION_OPERATION_UPDATE ||
      kind == FLATPAK_TRANSACTION_OPERATION_INSTALL_BUNDLE ||
      kind == FLATPAK_TRANSACTION_OPERATION_UNINSTALL)
    {
      g_mutex_lock (&self->mute_mutex);
      if (self->user_interactive ==
          flatpak_transaction_get_installation (transaction))
        self->user_mute++;
      else
        self->system_mute++;
      g_mutex_unlock (&self->mute_mutex);
    }

  if (data->channel == NULL)
    return;

  flatpak_transaction_progress_set_update_frequency (progress, 100);
  entry = find_entry_from_operation (data, operation);

  payload = bz_backend_transaction_op_payload_new ();
  bz_backend_transaction_op_payload_set_entry (
      payload, BZ_ENTRY (entry));
  bz_backend_transaction_op_payload_set_name (
      payload, flatpak_transaction_operation_get_ref (operation));
  bz_backend_transaction_op_payload_set_download_size (
      payload, flatpak_transaction_operation_get_download_size (operation));
  bz_backend_transaction_op_payload_set_installed_size (
      payload, flatpak_transaction_operation_get_installed_size (operation));

  g_mutex_lock (&data->mutex);
  g_ptr_array_add (
      data->send_futures,
      dex_channel_send (
          data->channel,
          dex_future_new_for_object (payload)));
  data->unidentified_op_cnt--;
  g_mutex_unlock (&data->mutex);

  g_object_set_data_full (
      G_OBJECT (operation),
      "payload", g_object_ref (payload),
      g_object_unref);

  operation_data         = transaction_operation_data_new ();
  operation_data->parent = transaction_data_ref (data);
  operation_data->entry  = bz_object_maybe_ref (entry);
  operation_data->op     = g_object_ref (payload);

  g_signal_connect_data (
      progress, "changed",
      G_CALLBACK (transaction_progress_changed),
      transaction_operation_data_ref (operation_data),
      transaction_operation_data_unref_closure,
      G_CONNECT_DEFAULT);
}

static void
transaction_operation_done (FlatpakTransaction          *transaction,
                            FlatpakTransactionOperation *operation,
                            gchar                       *commit,
                            gint                         result,
                            TransactionData             *data)
{
  g_autoptr (GMutexLocker) locker                      = NULL;
  g_autoptr (BzFlatpakInstance) self                   = NULL;
  g_autoptr (BzBackendTransactionOpPayload) payload    = NULL;
  g_autoptr (TransactionOperationDoneData) future_data = NULL;
  g_autoptr (DexFuture) future                         = NULL;

  bz_weak_get_or_return (self, data->self);
  locker = g_mutex_locker_new (&data->mutex);

  g_hash_table_replace (
      data->op_to_progress_hash,
      g_object_ref (operation),
      GINT_TO_POINTER (100));

  payload = g_object_steal_data (G_OBJECT (operation), "payload");
  if (payload != NULL)
    g_ptr_array_add (
        data->send_futures,
        dex_channel_send (
            data->channel,
            dex_future_new_for_object (payload)));

  if (result == FLATPAK_TRANSACTION_RESULT_NO_CHANGE)
    return;

  future_data              = transaction_operation_done_data_new ();
  future_data->parent      = transaction_data_ref (data);
  future_data->transaction = g_object_ref (transaction);
  future_data->operation   = g_object_ref (operation);

  future = dex_scheduler_spawn (
      self->scheduler,
      bz_get_dex_stack_size (),
      (DexFiberFunc) transaction_operation_done_fiber,
      transaction_operation_done_data_ref (future_data),
      transaction_operation_done_data_unref);

  g_ptr_array_add (data->send_futures, g_steal_pointer (&future));
}

static gboolean
transaction_operation_error (FlatpakTransaction          *object,
                             FlatpakTransactionOperation *operation,
                             GError                      *error,
                             gint                         details,
                             TransactionData             *data)
{
  g_autoptr (BzBackendTransactionOpPayload) payload = NULL;

  /* `FLATPAK_TRANSACTION_ERROR_DETAILS_NON_FATAL` is the only
     possible value of `details` */

  g_warning ("Transaction failed to complete: %s", error->message);

  g_mutex_lock (&data->mutex);
  g_hash_table_replace (
      data->op_to_progress_hash,
      g_object_ref (operation),
      GINT_TO_POINTER (100));

  payload = g_object_steal_data (G_OBJECT (operation), "payload");
  if (payload != NULL)
    {
      g_object_set_data_full (
          G_OBJECT (payload), "error",
          g_strdup (error->message), g_free);
      g_ptr_array_add (
          data->send_futures,
          dex_channel_send (
              data->channel,
              dex_future_new_for_object (payload)));
    }

  g_mutex_unlock (&data->mutex);

  /* Don't recover for now */
  return FALSE;
}

static gboolean
transaction_ready (FlatpakTransaction *object,
                   TransactionData    *data)
{
  g_autolist (GObject) operations = NULL;

  operations = flatpak_transaction_get_operations (object);

  g_mutex_lock (&data->mutex);
  data->unidentified_op_cnt += g_list_length (operations);
  g_mutex_unlock (&data->mutex);

  return TRUE;
}

static BzFlatpakEntry *
find_entry_from_operation (TransactionData             *data,
                           FlatpakTransactionOperation *operation)
{
  GPtrArray      *related_to_ops = NULL;
  const char     *ref_fmt        = NULL;
  BzFlatpakEntry *entry          = NULL;

  related_to_ops = flatpak_transaction_operation_get_related_to_ops (operation);

  ref_fmt = flatpak_transaction_operation_get_ref (operation);
  entry   = g_hash_table_lookup (data->ref_to_entry_hash, ref_fmt);
  if (entry != NULL)
    return entry;

  if (related_to_ops != NULL)
    {
      for (guint i = 0; i < related_to_ops->len; i++)
        {
          FlatpakTransactionOperation *related_op = NULL;

          related_op = g_ptr_array_index (related_to_ops, i);
          entry      = find_entry_from_operation (data, related_op);
          if (entry != NULL)
            break;
        }
    }

  return entry;
}

static void
transaction_progress_changed (FlatpakTransactionProgress *progress,
                              TransactionOperationData   *data)
{
  TransactionData *parent                                   = data->parent;
  g_autoptr (BzBackendTransactionOpProgressPayload) payload = NULL;
  int            int_progress                               = 0;
  double         double_progress                            = 0.0;
  GHashTableIter iter                                       = { 0 };
  int            progress_sum                               = 0;
  guint          n_ops                                      = 0;
  double         total_progress                             = 0.0;

  g_mutex_lock (&parent->mutex);

  int_progress    = flatpak_transaction_progress_get_progress (progress);
  double_progress = (double) flatpak_transaction_progress_get_progress (progress) / 100.0;

  g_hash_table_replace (
      parent->op_to_progress_hash,
      g_object_ref (data->op),
      GINT_TO_POINTER (int_progress));

  g_hash_table_iter_init (&iter, parent->op_to_progress_hash);
  for (;;)
    {
      gpointer key = NULL;
      gpointer val = NULL;

      if (!g_hash_table_iter_next (&iter, &key, &val))
        break;

      progress_sum += GPOINTER_TO_INT (val);
      n_ops++;
    }
  total_progress = MIN ((double) progress_sum /
                            (double) ((n_ops + parent->unidentified_op_cnt) * 100),
                        1.0);

  payload = bz_backend_transaction_op_progress_payload_new ();
  bz_backend_transaction_op_progress_payload_set_op (
      payload, data->op);
  bz_backend_transaction_op_progress_payload_set_status (
      payload, flatpak_transaction_progress_get_status (progress));
  bz_backend_transaction_op_progress_payload_set_is_estimating (
      payload, flatpak_transaction_progress_get_is_estimating (progress));
  bz_backend_transaction_op_progress_payload_set_progress (
      payload, double_progress);
  bz_backend_transaction_op_progress_payload_set_total_progress (
      payload, total_progress);
  bz_backend_transaction_op_progress_payload_set_bytes_transferred (
      payload, flatpak_transaction_progress_get_bytes_transferred (progress));
  bz_backend_transaction_op_progress_payload_set_start_time (
      payload, flatpak_transaction_progress_get_start_time (progress));

  g_ptr_array_add (
      data->parent->send_futures,
      dex_channel_send (
          data->parent->channel,
          dex_future_new_for_object (payload)));

  g_mutex_unlock (&parent->mutex);
}

static DexFuture *
transaction_operation_done_fiber (TransactionOperationDoneData *data)
{
  g_autoptr (BzFlatpakInstance) self           = NULL;
  FlatpakTransaction          *transaction     = data->transaction;
  FlatpakTransactionOperation *operation       = data->operation;
  g_autoptr (GError) local_error               = NULL;
  FlatpakTransactionOperationType op_type      = 0;
  BzBackendNotificationKind       notif_kind   = 0;
  const char                     *origin       = NULL;
  const char                     *ref          = NULL;
  gboolean                        is_user      = FALSE;
  g_autofree char                *unique_id    = NULL;
  const char                     *version      = NULL;
  FlatpakInstallation            *installation = NULL;
  g_autoptr (FlatpakInstalledRef) iref         = NULL;
  g_autoptr (FlatpakRef) parsed_ref            = NULL;

  bz_weak_get_or_return_reject (self, data->parent->self);

  op_type = flatpak_transaction_operation_get_operation_type (operation);
  switch (op_type)
    {
    case FLATPAK_TRANSACTION_OPERATION_INSTALL:
    case FLATPAK_TRANSACTION_OPERATION_INSTALL_BUNDLE:
      notif_kind = BZ_BACKEND_NOTIFICATION_KIND_INSTALL_DONE;
      break;
    case FLATPAK_TRANSACTION_OPERATION_UPDATE:
      notif_kind = BZ_BACKEND_NOTIFICATION_KIND_UPDATE_DONE;
      break;
    case FLATPAK_TRANSACTION_OPERATION_UNINSTALL:
      notif_kind = BZ_BACKEND_NOTIFICATION_KIND_REMOVE_DONE;
      break;
    case FLATPAK_TRANSACTION_OPERATION_LAST_TYPE:
    default:
      g_assert_not_reached ();
    }

  installation = flatpak_transaction_get_installation (transaction);

  origin  = flatpak_transaction_operation_get_remote (operation);
  ref     = flatpak_transaction_operation_get_ref (operation);
  is_user = installation == self->user_interactive;

  unique_id = bz_flatpak_ref_parts_format_unique (origin, ref, is_user);

  if (op_type == FLATPAK_TRANSACTION_OPERATION_INSTALL_BUNDLE)
    {
      g_autoptr (FlatpakRemote) remote = NULL;

      {
        g_autoptr (BzBackendNotification) notif = NULL;

        notif = bz_backend_notification_new ();
        bz_backend_notification_set_kind (notif, BZ_BACKEND_NOTIFICATION_KIND_INVALIDATE_REMOTES);

        send_notif_all (self, notif, TRUE);
      }

      remote = flatpak_installation_get_remote_by_name (
          installation, origin, NULL, NULL);
      if (remote != NULL)
        {
          {
            g_autoptr (BzBackendNotification) notif = NULL;

            notif = bz_backend_notification_new ();
            bz_backend_notification_set_kind (notif, BZ_BACKEND_NOTIFICATION_KIND_REMOTE_SYNC_START);
            bz_backend_notification_set_remote_name (notif, origin);

            send_notif_all (self, notif, TRUE);
          }
          dex_await (
              retrieve_refs_for_noenumerable_remote (
                  self, NULL, origin, installation, remote),
              NULL);
          {
            g_autoptr (BzBackendNotification) notif = NULL;

            notif = bz_backend_notification_new ();
            bz_backend_notification_set_kind (notif, BZ_BACKEND_NOTIFICATION_KIND_REMOTE_SYNC_FINISH);
            bz_backend_notification_set_remote_name (notif, origin);

            send_notif_all (self, notif, TRUE);
          }
        }
    }

  if (notif_kind == BZ_BACKEND_NOTIFICATION_KIND_INSTALL_DONE ||
      notif_kind == BZ_BACKEND_NOTIFICATION_KIND_UPDATE_DONE)
    {
      parsed_ref = flatpak_ref_parse (ref, &local_error);
      if (parsed_ref != NULL)
        {
          iref = flatpak_installation_get_installed_ref (
              installation,
              flatpak_ref_get_kind (parsed_ref),
              flatpak_ref_get_name (parsed_ref),
              flatpak_ref_get_arch (parsed_ref),
              flatpak_ref_get_branch (parsed_ref),
              NULL,
              &local_error);

          if (iref != NULL)
            version = flatpak_installed_ref_get_appdata_version (iref);
          else if (local_error != NULL)
            {
              g_warning ("Failed to get installed ref for version: %s", local_error->message);
              g_clear_error (&local_error);
            }
        }
      else if (local_error != NULL)
        {
          g_warning ("Failed to parse ref for version: %s", local_error->message);
          g_clear_error (&local_error);
        }
    }

  {
    g_autoptr (BzBackendNotification) notif = NULL;

    notif = bz_backend_notification_new ();
    bz_backend_notification_set_kind (notif, notif_kind);
    bz_backend_notification_set_unique_id (notif, unique_id);

    if (version != NULL && *version != '\0')
      bz_backend_notification_set_version (notif, version);

    send_notif_all (self, notif, TRUE);
  }

  return dex_future_new_true ();
}

static void
installation_event (BzFlatpakInstance *self,
                    GFile             *file,
                    GFile             *other_file,
                    GFileMonitorEvent  event_type,
                    GFileMonitor      *monitor)
{
  gboolean emit                           = FALSE;
  g_autoptr (BzBackendNotification) notif = NULL;

  g_mutex_lock (&self->mute_mutex);
  if (monitor == self->user_events)
    {
      if (self->user_mute > 0)
        self->user_mute--;
      else
        emit = TRUE;
    }
  else
    {
      if (self->system_mute > 0)
        self->system_mute--;
      else
        emit = TRUE;
    }
  g_mutex_unlock (&self->mute_mutex);

  if (!emit)
    return;

  notif = bz_backend_notification_new ();
  bz_backend_notification_set_kind (notif, BZ_BACKEND_NOTIFICATION_KIND_EXTERNAL_CHANGE);
  send_notif_all (self, notif, TRUE);
}

static void
send_notif (BzFlatpakInstance     *self,
            DexChannel            *channel,
            BzBackendNotification *notif,
            gboolean               lock)
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
      g_autoptr (WaitNotifData) data = NULL;

      data = wait_notif_data_new ();
      g_weak_ref_init (&data->self, self);
      data->channel = dex_ref (channel);
      data->notif   = g_object_ref (notif);

      self->notif_send = dex_future_finally (
          g_steal_pointer (&self->notif_send),
          (DexFutureCallback) wait_notif_finally,
          wait_notif_data_ref (data),
          wait_notif_data_unref);
    }
}

static void
send_notif_all (BzFlatpakInstance     *self,
                BzBackendNotification *notif,
                gboolean               lock)
{
  g_autoptr (GMutexLocker) locker = NULL;

  if (lock)
    locker = g_mutex_locker_new (&self->notif_mutex);

  for (guint i = 0; i < self->notif_channels->len;)
    {
      DexChannel *channel = NULL;

      channel = g_ptr_array_index (self->notif_channels, i);
      if (dex_channel_can_send (channel))
        {
          send_notif (self, channel, notif, FALSE);
          i++;
        }
      else
        g_ptr_array_remove_index_fast (self->notif_channels, i);
    }
}

static DexFuture *
wait_notif_finally (DexFuture     *future,
                    WaitNotifData *data)
{
  g_autoptr (BzFlatpakInstance) self = NULL;
  g_autoptr (GMutexLocker) locker    = NULL;

  bz_weak_get_or_return_reject (self, &data->self);
  locker = g_mutex_locker_new (&self->notif_mutex);

  if (future == self->notif_send)
    dex_clear (&self->notif_send);
  send_notif (self, data->channel, data->notif, FALSE);

  return dex_future_new_true ();
}

static gint
cmp_rref (FlatpakRemoteRef *a,
          FlatpakRemoteRef *b,
          GHashTable       *hash)
{
  FlatpakRefKind  a_fkind = 0;
  FlatpakRefKind  b_fkind = 0;
  AsComponent    *a_comp  = NULL;
  AsComponent    *b_comp  = NULL;
  AsComponentKind a_kind  = AS_COMPONENT_KIND_UNKNOWN;
  AsComponentKind b_kind  = AS_COMPONENT_KIND_UNKNOWN;

  a_fkind = flatpak_ref_get_kind (FLATPAK_REF (a));
  b_fkind = flatpak_ref_get_kind (FLATPAK_REF (b));

  a_comp = g_hash_table_lookup (hash, flatpak_ref_get_name (FLATPAK_REF (a)));
  b_comp = g_hash_table_lookup (hash, flatpak_ref_get_name (FLATPAK_REF (b)));

  if (a_comp == NULL)
    return a_fkind == FLATPAK_REF_KIND_RUNTIME ? -1 : 1;
  if (b_comp == NULL)
    return b_fkind == FLATPAK_REF_KIND_RUNTIME ? 1 : -1;

  a_kind = as_component_get_kind (a_comp);
  b_kind = as_component_get_kind (b_comp);

  if (a_kind == AS_COMPONENT_KIND_RUNTIME)
    return -1;
  if (b_kind == AS_COMPONENT_KIND_RUNTIME)
    return 1;

  if (a_kind == AS_COMPONENT_KIND_ADDON)
    return -1;
  if (b_kind == AS_COMPONENT_KIND_ADDON)
    return 1;

  if (a_kind == AS_COMPONENT_KIND_DESKTOP_APP ||
      a_kind == AS_COMPONENT_KIND_CONSOLE_APP ||
      a_kind == AS_COMPONENT_KIND_WEB_APP)
    return 1;
  if (b_kind == AS_COMPONENT_KIND_DESKTOP_APP ||
      b_kind == AS_COMPONENT_KIND_CONSOLE_APP ||
      b_kind == AS_COMPONENT_KIND_WEB_APP)
    return -1;

  return 0;
}

static XbSilo *
build_silo (XbBuilderSource *source,
            GCancellable    *cancellable,
            GError         **error)
{
  g_autoptr (XbBuilder) builder = NULL;
  const gchar *const *locales   = NULL;
  g_autoptr (XbSilo) silo       = NULL;

  builder = xb_builder_new ();

  locales = g_get_language_names ();
  for (guint i = 0; locales[i] != NULL; i++)
    xb_builder_add_locale (builder, locales[i]);

  xb_builder_import_source (builder, source);
  silo = xb_builder_compile (
      builder,
      XB_BUILDER_COMPILE_FLAG_NATIVE_LANGS,
      cancellable,
      error);

  return g_steal_pointer (&silo);
}

static AsComponent *
extract_first_component_for_silo (XbSilo  *silo,
                                  GError **error)
{
  g_autoptr (XbNode) root        = NULL;
  g_autoptr (GPtrArray) children = NULL;

  root     = xb_silo_get_root (silo);
  children = xb_node_get_children (root);

  if (children == NULL || children->len == 0)
    return NULL;

  return bz_parse_component_for_node (
      g_ptr_array_index (children, 0),
      error);
}
