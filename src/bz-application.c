/* bz-application.c
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

#define G_LOG_DOMAIN  "BAZAAR::CORE"
#define BAZAAR_MODULE "core"

#define MAX_IDS_PER_BLOCKLIST 2048

#include "config.h"

#include <glib/gi18n.h>
#include <malloc.h>

#include "bz-application-map-factory.h"
#include "bz-application.h"
#include "bz-appstream-parser.h"
#include "bz-auth-state.h"
#include "bz-backend-notification.h"
#include "bz-bundle-install-dialog.h"
#include "bz-content-provider.h"
#include "bz-donations-dialog.h"
#include "bz-download-worker.h"
#include "bz-entry-cache-manager.h"
#include "bz-entry-group.h"
#include "bz-env.h"
#include "bz-error.h"
#include "bz-favorites-page.h"
#include "bz-flathub-state.h"
#include "bz-flatpak-entry.h"
#include "bz-flatpak-instance.h"
#include "bz-gnome-extension-info.h"
#include "bz-gnome-extension-instance.h"
#include "bz-gnome-shell-search-provider.h"
#include "bz-hash-table-object.h"
#include "bz-inspector.h"
#include "bz-internal-config.h"
#include "bz-io.h"
#include "bz-login-page.h"
#include "bz-malcontent-service.h"
#include "bz-metainfo-preview.h"
#include "bz-newline-parser.h"
#include "bz-parser.h"
#include "bz-preferences-dialog.h"
#include "bz-result.h"
#include "bz-root-blocklist.h"
#include "bz-root-curated-config.h"
#include "bz-serializable.h"
#include "bz-state-info.h"
#include "bz-transaction-manager.h"
#include "bz-util.h"
#include "bz-window.h"
#include "bz-yaml-parser.h"
#include "progress-bar-designs/common.h"

struct _BzApplication
{
  AdwApplication parent_instance;

  BzApplicationMapFactory    *application_factory;
  BzApplicationMapFactory    *entry_factory;
  BzContentProvider          *blocklists_provider;
  BzContentProvider          *curated_provider;
  BzContentProvider          *txt_blocklists_provider;
  BzEntryCacheManager        *cache;
  BzFlathubState             *flathub;
  BzFlathubState             *tmp_flathub;
  BzFlatpakInstance          *flatpak;
  BzGnomeExtensionInstance   *gnome_ext;
  BzGnomeShellSearchProvider *gs_search;
  BzInternalConfig           *internal_config;
  BzMainConfig               *config;
  BzMalcontentService        *malcontent;
  BzNewlineParser            *txt_blocklist_parser;
  BzSearchEngine             *search_engine;
  BzStateInfo                *state;
  BzTransactionManager       *transactions;
  BzYamlParser               *blocklist_parser;
  BzYamlParser               *curated_parser;
  DexChannel                 *flatpak_notifs;
  DexFuture                  *notif_watch;
  DexFuture                  *sync;
  DexPromise                 *ready_to_open_files;
  GHashTable                 *eol_runtimes;
  GHashTable                 *ids_to_groups;
  GHashTable                 *ignore_eol_set;
  GHashTable                 *installed_set;
  GHashTable                 *sys_name_to_addons;
  GHashTable                 *usr_name_to_addons;
  GHashTable                 *sys_ref_to_addon_group_ids;
  GHashTable                 *usr_ref_to_addon_group_ids;
  GListStore                 *groups;
  GListStore                 *installed_apps;
  GListStore                 *search_biases_backing;
  GListStore                 *gnome_ext_infos;
  GNetworkMonitor            *network;
  GPtrArray                  *blocklist_regexes;
  GPtrArray                  *txt_blocked_id_sets;
  GSettings                  *settings;
  GTimer                     *init_timer;
  GWeakRef                    main_window;
  GtkCustomFilter            *appid_filter;
  GtkCustomFilter            *group_filter;
  GtkFilterListModel         *group_filter_model;
  GtkFlattenListModel        *search_biases;
  GtkMapListModel            *blocklists_to_files;
  GtkMapListModel            *curated_configs_to_files;
  GtkMapListModel            *txt_blocklists_to_files;
  GtkStringList              *blocklists;
  GtkStringList              *curated_configs;
  GtkStringList              *txt_blocklists;
  gboolean                    flathub_remote_initialized;
  gboolean                    running;
  guint                       periodic_timeout_source;
  int                         n_entries_incoming;
  int                         n_remotes_syncing;
};

G_DEFINE_FINAL_TYPE (BzApplication, bz_application, ADW_TYPE_APPLICATION)

BZ_DEFINE_DATA (
    blocklist_regex,
    BlocklistRegex,
    {
      int     priority;
      GRegex *block;
      GRegex *allow;
    },
    BZ_RELEASE_DATA (block, g_regex_unref);
    BZ_RELEASE_DATA (allow, g_regex_unref))

BZ_DEFINE_DATA (
    respond_to_flatpak,
    RespondToFlatpak,
    {
      GWeakRef              *self;
      BzBackendNotification *notif;
      DexChannel            *channel;
      gboolean               is_gnome_ext;
    },
    BZ_RELEASE_DATA (self, bz_weak_release);
    BZ_RELEASE_DATA (notif, g_object_unref);
    BZ_RELEASE_DATA (channel, dex_unref))

BZ_DEFINE_DATA (
    cache_write_back,
    CacheWriteBack,
    {
      GWeakRef  *self;
      GPtrArray *notify_groups;
      gboolean   update_filters;
    },
    BZ_RELEASE_DATA (self, bz_weak_release);
    BZ_RELEASE_DATA (notify_groups, g_ptr_array_unref))

BZ_DEFINE_DATA (
    open_flatpakref,
    OpenFlatpakref,
    {
      GWeakRef *self;
      GFile    *file;
    },
    BZ_RELEASE_DATA (self, bz_weak_release);
    BZ_RELEASE_DATA (file, g_object_unref))

BZ_DEFINE_DATA (
    open_appstream,
    OpenAppstream,
    {
      GWeakRef *self;
      char     *id;
    },
    BZ_RELEASE_DATA (self, g_object_unref);
    BZ_RELEASE_DATA (id, g_free))

static DexFuture *
init_fiber (GWeakRef *wr);

static DexFuture *
enumerate_disk_entries_fiber (GWeakRef *wr);

static DexFuture *
cache_flathub_fiber (GWeakRef *wr);

static DexFuture *
respond_to_flatpak_fiber (RespondToFlatpakData *data);

static DexFuture *
open_appstream_fiber (OpenAppstreamData *data);

static DexFuture *
open_flatpakref_fiber (OpenFlatpakrefData *data);

static DexFuture *
backend_sync_finally (DexFuture *future,
                      GWeakRef  *wr);

static DexFuture *
init_fiber_finally (DexFuture *future,
                    GWeakRef  *wr);

static DexFuture *
init_sync_finally (DexFuture *future,
                   GWeakRef  *wr);

static DexFuture *
flathub_update_finally (DexFuture *future,
                        GWeakRef  *wr);

static DexFuture *
cache_write_back_finally (DexFuture          *future,
                          CacheWriteBackData *data);

static DexFuture *
sync_finally (DexFuture *future,
              GWeakRef  *wr);

BZ_DEFINE_DATA (
    watch_notifs,
    WatchNotifs,
    {
      GWeakRef   *self;
      DexChannel *channel;
      gboolean    is_gnome_ext;
    },
    BZ_RELEASE_DATA (self, bz_weak_release);
    BZ_RELEASE_DATA (channel, dex_unref))

static DexFuture *
watch_backend_notifs_then_loop_cb (DexFuture       *future,
                                   WatchNotifsData *data);

static BzEntryGroup *
ensure_group_and_add (BzApplication *self,
                      const char    *id,
                      BzEntry       *entry,
                      BzEntry       *eol_runtime,
                      gboolean       ignore_eol,
                      gboolean       installed);

static void
fiber_replace_entry (BzApplication *self,
                     BzEntry       *entry);

static void
fiber_check_for_updates (BzApplication *self);

static GFile *
fiber_dup_flathub_cache_file (char   **path_out,
                              GError **error);

static gboolean
periodic_timeout_cb (BzApplication *self);

static gboolean
scheduled_timeout_cb (GWeakRef *wr);

static void
network_status_changed (BzApplication   *self,
                        GParamSpec      *pspec,
                        GNetworkMonitor *network);

static void
disable_blocklists_changed (BzApplication *self,
                            GParamSpec    *pspec,
                            BzStateInfo   *state);

static void
show_hide_app_setting_changed (BzApplication *self,
                               const char    *key,
                               GSettings     *settings);

static gboolean
window_close_request (BzApplication *self,
                      GtkWidget     *window);

static void
blocklists_changed (BzApplication *self,
                    guint          position,
                    guint          removed,
                    guint          added,
                    GListModel    *model);

static void
txt_blocklists_changed (BzApplication *self,
                        guint          position,
                        guint          removed,
                        guint          added,
                        GListModel    *model);

static void
init_service_struct (BzApplication *self,
                     GtkStringList *blocklists,
                     GtkStringList *txt_blocklists,
                     GtkStringList *curated_configs);

static GtkWindow *
new_window (BzApplication *self);

static void
open_appstream_take (BzApplication *self,
                     char          *appstream);

static void
open_flatpakref_take (BzApplication *self,
                      GFile         *file);

static void
command_line_open_location (BzApplication           *self,
                            GApplicationCommandLine *cmdline,
                            const char              *path);

static void
open_generic_id (BzApplication *self,
                 const char    *generic_id);

static DexFuture *
preview_metainfo_then (DexFuture *future,
                       GWeakRef  *wr);

static gpointer
map_strings_to_files (GtkStringObject *string,
                      gpointer         data);

static gpointer
map_generic_ids_to_groups (GtkStringObject *string,
                           BzApplication   *self);

static gpointer
map_ids_to_entries (GtkStringObject *string,
                    BzApplication   *self);

static gboolean
filter_application_ids (GtkStringObject *string,
                        BzApplication   *self);

static gboolean
filter_entry_groups (BzEntryGroup  *group,
                     BzApplication *self);

static gint
cmp_group (BzEntryGroup *a,
           BzEntryGroup *b,
           gpointer      user_data);

static gint
cmp_entry (BzEntry *a,
           BzEntry *b,
           gpointer user_data);

static gboolean
validate_group_for_ui (BzApplication *self,
                       BzEntryGroup  *group);

static DexFuture *
make_sync_future (BzApplication *self);

static void
finish_with_background_task_label (BzApplication *self);

static void
state_info_set_icon_themes (BzStateInfo *state);

static void
bz_application_dispose (GObject *object)
{
  BzApplication *self = BZ_APPLICATION (object);

  dex_clear (&self->flatpak_notifs);
  dex_clear (&self->notif_watch);
  dex_clear (&self->ready_to_open_files);
  dex_clear (&self->sync);
  g_clear_handle_id (&self->periodic_timeout_source, g_source_remove);
  g_clear_object (&self->appid_filter);
  g_clear_object (&self->application_factory);
  g_clear_object (&self->blocklist_parser);
  g_clear_object (&self->blocklists);
  g_clear_object (&self->blocklists_provider);
  g_clear_object (&self->blocklists_to_files);
  g_clear_object (&self->cache);
  g_clear_object (&self->curated_configs);
  g_clear_object (&self->curated_configs_to_files);
  g_clear_object (&self->curated_parser);
  g_clear_object (&self->curated_provider);
  g_clear_object (&self->entry_factory);
  g_clear_object (&self->flathub);
  g_clear_object (&self->flatpak);
  g_clear_object (&self->group_filter);
  g_clear_object (&self->group_filter_model);
  g_clear_object (&self->groups);
  g_clear_object (&self->gs_search);
  g_clear_object (&self->installed_apps);
  g_clear_object (&self->malcontent);
  g_clear_object (&self->internal_config);
  g_clear_object (&self->network);
  g_clear_object (&self->search_biases);
  g_clear_object (&self->search_biases_backing);
  g_clear_object (&self->gnome_ext_infos);
  g_clear_object (&self->search_engine);
  g_clear_object (&self->settings);
  g_clear_object (&self->state);
  g_clear_object (&self->tmp_flathub);
  g_clear_object (&self->transactions);
  g_clear_object (&self->txt_blocklist_parser);
  g_clear_object (&self->txt_blocklists);
  g_clear_object (&self->txt_blocklists_provider);
  g_clear_object (&self->txt_blocklists_to_files);
  g_clear_pointer (&self->blocklist_regexes, g_ptr_array_unref);
  g_clear_pointer (&self->eol_runtimes, g_hash_table_unref);
  g_clear_pointer (&self->ids_to_groups, g_hash_table_unref);
  g_clear_pointer (&self->ignore_eol_set, g_hash_table_unref);
  g_clear_pointer (&self->init_timer, g_timer_destroy);
  g_clear_pointer (&self->installed_set, g_hash_table_unref);
  g_clear_pointer (&self->sys_name_to_addons, g_hash_table_unref);
  g_clear_pointer (&self->txt_blocked_id_sets, g_ptr_array_unref);
  g_clear_pointer (&self->usr_name_to_addons, g_hash_table_unref);
  g_clear_pointer (&self->sys_ref_to_addon_group_ids, g_hash_table_unref);
  g_clear_pointer (&self->usr_ref_to_addon_group_ids, g_hash_table_unref);
  g_weak_ref_clear (&self->main_window);

  G_OBJECT_CLASS (bz_application_parent_class)->dispose (object);
}

static void
bz_application_activate (GApplication *app)
{
  BzApplication *self = BZ_APPLICATION (app);

  new_window (self);
}

static int
bz_application_command_line (GApplication            *app,
                             GApplicationCommandLine *cmdline)
{
  BzApplication *self                 = BZ_APPLICATION (app);
  g_autoptr (GError) local_error      = NULL;
  gint argc                           = 0;
  g_auto (GStrv) argv                 = NULL;
  gboolean help                       = FALSE;
  gboolean no_window                  = FALSE;
  g_auto (GStrv) blocklists_strv      = NULL;
  g_auto (GStrv) content_configs_strv = NULL;
  g_auto (GStrv) locations            = NULL;
  gboolean preview_metainfo           = FALSE;

  GOptionEntry main_entries[] = {
    { "help", 0, 0, G_OPTION_ARG_NONE, &help, "Print help" },
    { "no-window", 0, 0, G_OPTION_ARG_NONE, &no_window, "Ensure the service is running without creating a new window" },
    { "extra-blocklist", 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &blocklists_strv, "Add an extra blocklist to read from" },
    { "extra-curated-config", 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &content_configs_strv, "Add an extra yaml file with which to configure the app browser" },
    /* Here for backwards compat */
    { "extra-content-config", 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &content_configs_strv, "Add an extra yaml file with which to configure the app browser (backwards compat)" },
    { "preview-metainfo", 0, 0, G_OPTION_ARG_NONE, &preview_metainfo, "Preview a metainfo file by selecting it via file dialog" },
    { G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &locations, "flatpakref file to open" },
    { NULL }
  };

  argv = g_application_command_line_get_arguments (cmdline, &argc);
  g_debug ("Handling gapplication command line; argc=%d, argv= \\", argc);
  for (guint i = 0; i < argc; i++)
    {
      g_debug ("  [%d] %s", i, argv[i]);
    }

  if (argv != NULL && argc > 0)
    {
      g_autofree GStrv argv_shallow      = NULL;
      g_autoptr (GOptionContext) context = NULL;

      argv_shallow = g_memdup2 (argv, sizeof (*argv) * argc);

      context = g_option_context_new ("- an app center for GNOME");
      g_option_context_set_help_enabled (context, FALSE);
      g_option_context_add_main_entries (context, main_entries, NULL);
      if (!g_option_context_parse (context, &argc, &argv_shallow, &local_error))
        {
          g_application_command_line_printerr (cmdline, "%s\n", local_error->message);
          return EXIT_FAILURE;
        }

      if (help)
        {
          g_autofree char *help_text = NULL;

          if (self->running)
            g_application_command_line_printerr (cmdline, "The Bazaar service is running.\n\n");
          else
            g_application_command_line_printerr (cmdline, "The Bazaar service is not running.\n\n");

          help_text = g_option_context_get_help (context, TRUE, NULL);
          g_application_command_line_printerr (cmdline, "%s\n", help_text);
          return EXIT_SUCCESS;
        }
    }

  if (!self->running)
    {
      g_autoptr (GtkStringList) blocklists      = NULL;
      g_autoptr (GtkStringList) txt_blocklists  = NULL;
      g_autoptr (GtkStringList) content_configs = NULL;
      g_autoptr (DexFuture) init                = NULL;

      g_debug ("Starting daemon!");
      g_application_hold (G_APPLICATION (self));
      self->running = TRUE;

      blocklists      = gtk_string_list_new (NULL);
      txt_blocklists  = gtk_string_list_new (NULL);
      content_configs = gtk_string_list_new (NULL);
      init_service_struct (self, blocklists, txt_blocklists, content_configs);

#ifdef HARDCODED_BLOCKLIST
      g_debug ("Bazaar was configured with a hardcoded txt blocklist at %s, adding that now...",
               HARDCODED_BLOCKLIST);
      gtk_string_list_append (txt_blocklists, HARDCODED_BLOCKLIST);
#endif
      if (blocklists_strv != NULL)
        gtk_string_list_splice (
            txt_blocklists,
            g_list_model_get_n_items (G_LIST_MODEL (txt_blocklists)),
            0,
            (const char *const *) blocklists_strv);

#ifdef HARDCODED_CONTENT_CONFIG
      g_debug ("Bazaar was configured with a hardcoded curated content config at %s, adding that now...",
               HARDCODED_CONTENT_CONFIG);
      gtk_string_list_append (content_configs, HARDCODED_CONTENT_CONFIG);
#endif
      if (content_configs_strv != NULL)
        gtk_string_list_splice (
            content_configs,
            g_list_model_get_n_items (G_LIST_MODEL (content_configs)),
            0,
            (const char *const *) content_configs_strv);

      g_timer_start (self->init_timer);
      init = dex_scheduler_spawn (
          dex_scheduler_get_default (),
          bz_get_dex_stack_size (),
          (DexFiberFunc) init_fiber,
          bz_track_weak (self),
          bz_weak_release);
      init = dex_future_finally (
          init,
          (DexFutureCallback) init_fiber_finally,
          bz_track_weak (self),
          bz_weak_release);
      dex_future_disown (g_steal_pointer (&init));
    }

  if (!no_window && !preview_metainfo)
    new_window (self);

  if (locations != NULL && *locations != NULL)
    command_line_open_location (self, cmdline, locations[0]);

  if (preview_metainfo)
    {
      g_autoptr (DexFuture) future = NULL;

      future = bz_metainfo_preview_pick_files ();
      future = dex_future_then (
          g_steal_pointer (&future),
          (DexFutureCallback) preview_metainfo_then,
          bz_track_weak (self),
          bz_weak_release);
      dex_future_disown (g_steal_pointer (&future));
    }

  return EXIT_SUCCESS;
}

static gboolean
bz_application_local_command_line (GApplication *application,
                                   gchar      ***arguments,
                                   int          *exit_status)
{
  return FALSE;
}

static gboolean
bz_application_dbus_register (GApplication    *application,
                              GDBusConnection *connection,
                              const gchar     *object_path,
                              GError         **error)
{
  BzApplication *self = BZ_APPLICATION (application);
  return bz_gnome_shell_search_provider_set_connection (self->gs_search, connection, error);
}

static void
bz_application_dbus_unregister (GApplication    *application,
                                GDBusConnection *connection,
                                const gchar     *object_path)
{
  BzApplication *self = BZ_APPLICATION (application);
  bz_gnome_shell_search_provider_set_connection (self->gs_search, NULL, NULL);
}

static void
bz_application_class_init (BzApplicationClass *klass)
{
  GObjectClass      *object_class = G_OBJECT_CLASS (klass);
  GApplicationClass *app_class    = G_APPLICATION_CLASS (klass);

  object_class->dispose = bz_application_dispose;

  app_class->activate           = bz_application_activate;
  app_class->command_line       = bz_application_command_line;
  app_class->local_command_line = bz_application_local_command_line;
  app_class->dbus_register      = bz_application_dbus_register;
  app_class->dbus_unregister    = bz_application_dbus_unregister;

  g_type_ensure (BZ_TYPE_RESULT);
}

static void
bz_application_toggle_debug_mode_action (GSimpleAction *action,
                                         GVariant      *parameter,
                                         gpointer       user_data)
{
  BzApplication *self       = user_data;
  gboolean       debug_mode = FALSE;

  debug_mode = bz_state_info_get_debug_mode (self->state);
  bz_state_info_set_debug_mode (self->state, !debug_mode);
}

static void
bz_application_bazaar_inspector_action (GSimpleAction *action,
                                        GVariant      *parameter,
                                        gpointer       user_data)
{
  BzApplication *self      = user_data;
  BzInspector   *inspector = NULL;

  g_assert (BZ_IS_APPLICATION (self));

  inspector = bz_inspector_new ();
  bz_inspector_set_state (inspector, self->state);

  gtk_application_add_window (GTK_APPLICATION (self), GTK_WINDOW (inspector));
  gtk_window_present (GTK_WINDOW (inspector));
}

static void
bz_application_donate_action (GSimpleAction *action,
                              GVariant      *parameter,
                              gpointer       user_data)
{
  BzApplication *self   = user_data;
  GtkWindow     *window = NULL;
  AdwDialog     *dialog = NULL;

  g_assert (BZ_IS_APPLICATION (self));

  window = gtk_application_get_active_window (GTK_APPLICATION (self));
  if (window == NULL)
    window = new_window (self);

  dialog = bz_donations_dialog_new ();
  adw_dialog_present (dialog, GTK_WIDGET (window));

  bz_state_info_set_donation_prompt_dismissed (self->state, TRUE);
}

static void
bz_application_search_action (GSimpleAction *action,
                              GVariant      *parameter,
                              gpointer       user_data)
{
  BzApplication *self         = user_data;
  GtkWindow     *window       = NULL;
  const char    *initial_text = NULL;

  g_assert (BZ_IS_APPLICATION (self));

  window = gtk_application_get_active_window (GTK_APPLICATION (self));
  if (window == NULL)
    window = new_window (self);

  if (parameter != NULL)
    initial_text = g_variant_get_string (parameter, NULL);

  bz_window_search (BZ_WINDOW (window), initial_text);
}

static void
bz_application_show_app_id_action (GSimpleAction *action,
                                   GVariant      *parameter,
                                   gpointer       user_data)
{
  BzApplication *self   = user_data;
  GtkWindow     *window = NULL;
  const char    *app_id = NULL;

  g_assert (BZ_IS_APPLICATION (self));

  window = gtk_application_get_active_window (GTK_APPLICATION (self));
  if (window == NULL)
    window = new_window (self);

  if (parameter != NULL)
    {
      app_id = g_variant_get_string (parameter, NULL);
      bz_window_show_app_id (BZ_WINDOW (window), app_id);
    }
}

static void
bz_application_sync_remotes_action (GSimpleAction *action,
                                    GVariant      *parameter,
                                    gpointer       user_data)
{
  BzApplication *self = user_data;

  g_assert (BZ_IS_APPLICATION (self));

  if (self->sync != NULL &&
      dex_future_is_pending (self->sync))
    return;

  dex_clear (&self->sync);
  self->sync = make_sync_future (self);
}

static void
bz_application_about_action (GSimpleAction *action,
                             GVariant      *parameter,
                             gpointer       user_data)
{
  BzApplication *self   = user_data;
  GtkWindow     *window = NULL;
  AdwDialog     *dialog = NULL;

  const char *developers[] = {
    C_ ("About Dialog Developer Credit", "Adam Masciola <kolunmi@posteo.net>"),
    C_ ("About Dialog Developer Credit", "Alexander Vanhee"),
    /* This array MUST be NULL terminated */
    NULL
  };

  const char *special_thanks[] = {
    "arewelibadwaitayet https://arewelibadwaitayet.com/",
    /* This array MUST be NULL terminated */
    NULL
  };

  g_assert (BZ_IS_APPLICATION (self));

  window = gtk_application_get_active_window (GTK_APPLICATION (self));
  dialog = adw_about_dialog_new ();

  g_object_set (
      dialog,
      "application-name", "Bazaar",
      "application-icon", "io.github.kolunmi.Bazaar",
      "developer-name", _ ("The Bazaar Contributors"),
      "developers", developers,
      // Translators: Put one translator per line, in the form NAME <EMAIL>, YEAR1, YEAR2
      "translator-credits", _ ("translator-credits"),
      "version", PACKAGE_VCS_VERSION,
      "copyright", "© 2025-2026 The Bazaar Contributors",
      "license-type", GTK_LICENSE_GPL_3_0,
      "website", "https://github.com/bazaar-org/bazaar",
      "issue-url", "https://github.com/bazaar-org/bazaar/issues",
      NULL);

  adw_about_dialog_add_acknowledgement_section (
      ADW_ABOUT_DIALOG (dialog),
      _ ("Special Thanks"),
      special_thanks);

  adw_dialog_present (dialog, GTK_WIDGET (window));
}

static void
bz_application_preferences_action (GSimpleAction *action,
                                   GVariant      *parameter,
                                   gpointer       user_data)
{
  BzApplication *self        = user_data;
  GtkWindow     *window      = NULL;
  AdwDialog     *preferences = NULL;

  g_assert (BZ_IS_APPLICATION (self));

  window      = gtk_application_get_active_window (GTK_APPLICATION (self));
  preferences = bz_preferences_dialog_new (self->state);

  adw_dialog_present (preferences, GTK_WIDGET (window));
}

static void
bz_application_flathub_login_action (GSimpleAction *action,
                                     GVariant      *parameter,
                                     gpointer       user_data)
{
  BzApplication     *self       = user_data;
  GtkWindow         *window     = NULL;
  BzAuthState       *auth_state = NULL;
  AdwNavigationPage *login_page = NULL;

  g_assert (BZ_IS_APPLICATION (self));

  window = gtk_application_get_active_window (GTK_APPLICATION (self));

  auth_state = bz_state_info_get_auth_state (self->state);
  login_page = bz_login_page_new (auth_state);

  bz_window_push_page (BZ_WINDOW (window), login_page);
}

static void
bz_application_flathub_logout_action (GSimpleAction *action,
                                      GVariant      *parameter,
                                      gpointer       user_data)
{
  BzApplication *self       = BZ_APPLICATION (user_data);
  GtkWindow     *window     = gtk_application_get_active_window (GTK_APPLICATION (self));
  BzAuthState   *auth_state = bz_state_info_get_auth_state (self->state);

  g_assert (BZ_IS_WINDOW (window));

  bz_auth_state_clear (auth_state);

  bz_window_add_toast (
      BZ_WINDOW (window),
      adw_toast_new (_ ("Logged Out Successfully!")));
}

static void
bz_application_flathub_favorites_action (GSimpleAction *action,
                                         GVariant      *parameter,
                                         gpointer       user_data)
{
  BzApplication     *self           = user_data;
  GtkWindow         *window         = NULL;
  AdwNavigationPage *favorites_page = NULL;

  g_assert (BZ_IS_APPLICATION (self));

  window = gtk_application_get_active_window (GTK_APPLICATION (self));
  if (window == NULL)
    window = new_window (self);

  favorites_page = ADW_NAVIGATION_PAGE (bz_favorites_page_new (self->state));

  bz_window_push_page (BZ_WINDOW (window), favorites_page);
}

static void
bz_application_quit_action (GSimpleAction *action,
                            GVariant      *parameter,
                            gpointer       user_data)
{
  BzApplication *self = user_data;

  g_assert (BZ_IS_APPLICATION (self));

  g_application_quit (G_APPLICATION (self));
}

static const GActionEntry app_actions[] = {
  {     "flathub-login",     bz_application_flathub_login_action, NULL },
  {    "flathub-logout",    bz_application_flathub_logout_action, NULL },
  { "flathub-favorites", bz_application_flathub_favorites_action, NULL },
  {              "quit",              bz_application_quit_action, NULL },
  {       "preferences",       bz_application_preferences_action, NULL },
  {             "about",             bz_application_about_action, NULL },
  {      "sync-remotes",      bz_application_sync_remotes_action, NULL },
  {            "search",            bz_application_search_action,  "s" },
  {       "show-app-id",       bz_application_show_app_id_action,  "s" },
  {            "donate",            bz_application_donate_action, NULL },
  {  "bazaar-inspector",  bz_application_bazaar_inspector_action, NULL },
  { "toggle-debug-mode", bz_application_toggle_debug_mode_action, NULL },
};

static void
bz_application_init (BzApplication *self)
{
  self->running = FALSE;
  g_weak_ref_init (&self->main_window, NULL);

  self->gs_search = bz_gnome_shell_search_provider_new ();

  g_action_map_add_action_entries (
      G_ACTION_MAP (self),
      app_actions,
      G_N_ELEMENTS (app_actions),
      self);
  gtk_application_set_accels_for_action (
      GTK_APPLICATION (self),
      "app.quit",
      (const char *[]) { "<primary>q", NULL });
  gtk_application_set_accels_for_action (
      GTK_APPLICATION (self),
      "app.preferences",
      (const char *[]) { "<primary>comma", NULL });
  gtk_application_set_accels_for_action (
      GTK_APPLICATION (self),
      "app.sync-remotes",
      (const char *[]) { "<primary>r", NULL });
  gtk_application_set_accels_for_action (
      GTK_APPLICATION (self),
      "app.search('')",
      (const char *[]) { "<primary>f", NULL });
  gtk_application_set_accels_for_action (
      GTK_APPLICATION (self),
      "app.bazaar-inspector",
      (const char *[]) { "<primary><alt><shift>i", NULL });
  gtk_application_set_accels_for_action (
      GTK_APPLICATION (self),
      "app.toggle-debug-mode",
      (const char *[]) { "<primary><alt>d", NULL });
}

BzStateInfo *
bz_state_info_get_default (void)
{
  GApplication  *app  = NULL;
  BzApplication *self = NULL;

  app = g_application_get_default ();
  if G_UNLIKELY (app == NULL)
    {
      static BzStateInfo *fallback_state = NULL;

      if (g_once_init_enter_pointer (&fallback_state))
        {
          g_autoptr (BzStateInfo) tmp = NULL;

          tmp = bz_state_info_new ();
          // state_info_set_icon_themes (tmp);

          g_once_init_leave_pointer (
              &fallback_state,
              g_steal_pointer (&tmp));
        }

      return fallback_state;
    }

  self = (BzApplication *) app;
  g_assert (BZ_IS_APPLICATION (self));

  return self->state;
}

static GListStore *
gnome_ext_hash_table_to_list_store (GHashTable *ids)
{
  GListStore    *store = g_list_store_new (BZ_TYPE_GNOME_EXTENSION_INFO);
  GHashTableIter iter  = { 0 };
  const char    *uuid  = NULL;
  gpointer       val   = NULL;

  g_hash_table_iter_init (&iter, ids);
  while (g_hash_table_iter_next (&iter, (gpointer *) &uuid, &val))
    g_list_store_append (store, BZ_GNOME_EXTENSION_INFO (val));

  return store;
}

static DexFuture *
init_fiber (GWeakRef *wr)
{
  g_autoptr (BzApplication) self        = NULL;
  g_autoptr (GError) local_error        = NULL;
  g_autofree char *root_cache_dir       = NULL;
  g_autoptr (GFile) root_cache_dir_file = NULL;
  g_autoptr (GListModel) repos          = NULL;
  gboolean         has_flathub          = FALSE;
  gboolean         result               = FALSE;
  g_autofree char *flathub_cache        = NULL;
  g_autoptr (GFile) flathub_cache_file  = NULL;
  const char *current_desktop           = NULL;

  bz_weak_get_or_return_reject (self, wr);

  bz_state_info_set_online (self->state, TRUE);
  bz_state_info_set_busy (self->state, TRUE);
  bz_state_info_set_background_task_label (self->state, _ ("Performing setup…"));

  root_cache_dir      = bz_dup_root_cache_dir ();
  root_cache_dir_file = g_file_new_for_path (root_cache_dir);
  if (dex_await (dex_file_query_exists (root_cache_dir_file), NULL))
    {
      g_autofree char *cache_version_path  = NULL;
      g_autoptr (GFile) cache_version_file = NULL;
      gboolean wipe_cache                  = TRUE;

      cache_version_path = g_build_filename (root_cache_dir, "cache-version", NULL);
      cache_version_file = g_file_new_for_path (cache_version_path);
      if (dex_await (dex_file_query_exists (cache_version_file), NULL))
        {
          g_autoptr (GBytes) bytes = NULL;

          bytes = dex_await_boxed (dex_file_load_contents_bytes (cache_version_file), NULL);
          if (bytes != NULL)
            {
              g_autoptr (GVariant) variant = NULL;

              variant = g_variant_new_from_bytes (G_VARIANT_TYPE_STRING, bytes, FALSE);
              if (variant != NULL)
                {
                  const char *version = NULL;

                  version    = g_variant_get_string (variant, NULL);
                  wipe_cache = g_strcmp0 (version, PACKAGE_VERSION) != 0;
                }
            }
        }

      if (wipe_cache)
        {
          bz_state_info_set_donation_prompt_dismissed (self->state, FALSE);

          g_info ("Version incompatibility detected: clearing cache");
          dex_await (bz_reap_file_dex (root_cache_dir_file), NULL);
        }

      if (dex_await (dex_file_make_directory_with_parents (root_cache_dir_file), NULL))
        {
          g_autoptr (GVariant) variant = NULL;
          g_autoptr (GBytes) bytes     = NULL;

          variant = g_variant_new_string (PACKAGE_VERSION);
          bytes   = g_variant_get_data_as_bytes (variant);
          dex_await (dex_file_replace_contents_bytes (
                         cache_version_file, bytes, NULL, FALSE,
                         G_FILE_CREATE_REPLACE_DESTINATION),
                     NULL);
        }
    }
  else
    bz_state_info_set_donation_prompt_dismissed (self->state, TRUE);

  g_clear_object (&self->flatpak);
  self->flatpak = dex_await_object (bz_flatpak_instance_new (), &local_error);
  if (self->flatpak == NULL)
    return dex_future_new_for_error (g_steal_pointer (&local_error));
  bz_transaction_manager_set_backend (self->transactions, BZ_BACKEND (self->flatpak));
  bz_state_info_set_backend (self->state, BZ_BACKEND (self->flatpak));

  current_desktop = g_getenv ("XDG_CURRENT_DESKTOP");
  if (current_desktop != NULL && strstr (current_desktop, "GNOME") != NULL)
    {
      self->gnome_ext = dex_await_object (
          bz_gnome_extension_instance_new (),
          &local_error);
      if (self->gnome_ext == NULL)
        {
          g_warning ("Failed to initialize GNOME extensions backend: %s",
                     local_error->message);
          g_clear_error (&local_error);
        }
      else
        {
          g_autofree char *shell_version = bz_gnome_extension_instance_dup_shell_version (self->gnome_ext);
          if (shell_version != NULL)
            bz_state_info_set_shell_version (self->state, shell_version);
        }
    }

  bz_transaction_manager_set_gnome_ext_backend (self->transactions, BZ_BACKEND (self->gnome_ext));

  if (self->gnome_ext != NULL)
    {
      g_autoptr (GError) ext_error   = NULL;
      g_autoptr (GHashTable) ext_ids = NULL;

      ext_ids = dex_await_boxed (
          bz_backend_retrieve_install_ids (BZ_BACKEND (self->gnome_ext), NULL),
          &ext_error);
      if (ext_ids == NULL)
        {
          g_warning ("Unable to enumerate installed GNOME extensions: %s",
                     ext_error->message);
          g_clear_error (&ext_error);
          self->gnome_ext_infos = g_list_store_new (BZ_TYPE_GNOME_EXTENSION_INFO);
        }
      else
        self->gnome_ext_infos = gnome_ext_hash_table_to_list_store (ext_ids);

      bz_state_info_set_gnome_extension_infos (
          self->state, G_LIST_MODEL (self->gnome_ext_infos));
    }

  has_flathub = dex_await_boolean (
      bz_flatpak_instance_has_flathub (self->flatpak, NULL),
      &local_error);
  if (local_error != NULL)
    return dex_future_new_for_error (g_steal_pointer (&local_error));

  if (!has_flathub)
    {
      GtkWindow       *window   = NULL;
      g_autofree char *response = NULL;

      window = gtk_application_get_active_window (GTK_APPLICATION (self));
      if (window != NULL)
        {
          AdwDialog *alert = NULL;

          alert = adw_alert_dialog_new (NULL, NULL);

#ifdef SANDBOXED_LIBFLATPAK
          adw_alert_dialog_format_heading (
              ADW_ALERT_DIALOG (alert),
              _ ("Set Up System Flathub?"));
          adw_alert_dialog_format_body (
              ADW_ALERT_DIALOG (alert),
              _ ("The system Flathub remote is not set up. Bazaar requires "
                 "Flathub to be configured on the system Flatpak installation "
                 "to browse and install applications.\n\n"
                 "You can still use Bazaar to browse and remove already installed apps."));
#else
          adw_alert_dialog_format_heading (
              ADW_ALERT_DIALOG (alert),
              _ ("Set Up Flathub?"));
          adw_alert_dialog_format_body (
              ADW_ALERT_DIALOG (alert),
              _ ("Flathub is not set up on this system. "
                 "You will not be able to browse and install applications in Bazaar if its unavailable.\n\n"
                 "You can still use Bazaar to browse and remove already installed apps."));
#endif
          adw_alert_dialog_add_responses (
              ADW_ALERT_DIALOG (alert),
              "later", _ ("Later"),
              "add", _ ("Set Up Flathub"),
              NULL);
          adw_alert_dialog_set_response_appearance (
              ADW_ALERT_DIALOG (alert), "add", ADW_RESPONSE_SUGGESTED);
          adw_alert_dialog_set_default_response (ADW_ALERT_DIALOG (alert), "add");
          adw_alert_dialog_set_close_response (ADW_ALERT_DIALOG (alert), "later");

          adw_dialog_present (alert, GTK_WIDGET (window));
          response = dex_await_string (
              bz_make_alert_dialog_future (ADW_ALERT_DIALOG (alert)),
              NULL);
        }

      if (response != NULL &&
          g_strcmp0 (response, "add") == 0)
        {
          result = dex_await (
              bz_flatpak_instance_ensure_has_flathub (self->flatpak, NULL),
              &local_error);
          if (result)
            has_flathub = TRUE;
          else
            {
              g_warning ("Failed to install flathub: %s",
                         local_error->message);
              g_clear_error (&local_error);
            }
        }
    }
  bz_state_info_set_has_flathub (self->state, has_flathub);

  self->installed_set = dex_await_boxed (
      bz_backend_retrieve_install_ids (
          BZ_BACKEND (self->flatpak), NULL),
      &local_error);
  if (self->installed_set == NULL)
    {
      g_warning ("Unable to enumerate installed entries from flatpak backend; "
                 "no entries will appear to be installed: %s",
                 local_error->message);
      g_clear_error (&local_error);

      self->installed_set = g_hash_table_new_full (
          g_str_hash, g_str_equal, g_free, g_free);
    }

  repos = dex_await_object (
      bz_backend_list_repositories (BZ_BACKEND (self->flatpak), NULL),
      &local_error);

  if (repos != NULL)
    bz_state_info_set_repositories (self->state, repos);
  else
    {
      g_warning ("Failed to enumerate repositories: %s", local_error->message);
      g_clear_error (&local_error);
    }

  /* Revive old cache from previous Bazaar process */
  dex_await (
      dex_scheduler_spawn (
          dex_scheduler_get_default (),
          bz_get_dex_stack_size (),
          (DexFiberFunc) enumerate_disk_entries_fiber,
          bz_track_weak (self),
          bz_weak_release),
      NULL);

  flathub_cache_file = fiber_dup_flathub_cache_file (&flathub_cache, &local_error);
  if (flathub_cache_file != NULL)
    {
      if (dex_await (dex_file_query_exists (flathub_cache_file), NULL))
        {
          g_autoptr (GBytes) bytes = NULL;

          bytes = dex_await_boxed (
              dex_file_load_contents_bytes (flathub_cache_file),
              &local_error);
          if (bytes != NULL)
            {
              g_autoptr (GVariant) variant       = NULL;
              g_autoptr (BzFlathubState) flathub = NULL;

              variant = g_variant_new_from_bytes (G_VARIANT_TYPE_VARDICT, bytes, FALSE);
              if (variant == NULL)
                {
                  g_warning ("Failed to interpret cached flathub state from %s: %s",
                             flathub_cache, local_error->message);
                  g_clear_error (&local_error);
                }

              flathub = bz_flathub_state_new ();
              result  = bz_serializable_deserialize (
                  BZ_SERIALIZABLE (flathub), variant, &local_error);
              if (result)
                {
                  self->flathub = g_steal_pointer (&flathub);
                  bz_flathub_state_set_map_factory (self->flathub, self->application_factory);
                  bz_state_info_set_flathub (self->state, self->flathub);

                  bz_state_info_set_busy (self->state, FALSE);
                  dex_promise_resolve_boolean (self->ready_to_open_files, TRUE);
                }
              else
                {
                  g_warning ("Failed to deserialize cached flathub state from %s: %s",
                             flathub_cache, local_error->message);
                  g_clear_error (&local_error);
                }
            }
          else
            {
              g_warning ("Failed to decache cache flathub state from %s: %s",
                         flathub_cache, local_error->message);
              g_clear_error (&local_error);
            }
        }
    }
  else
    {
      g_warning ("Unable to ensure cache directory: %s", local_error->message);
      g_clear_error (&local_error);
    }

  return dex_future_new_true ();
}

static DexFuture *
enumerate_disk_entries_fiber (GWeakRef *wr)
{
  g_autoptr (BzApplication) self    = NULL;
  g_autoptr (GError) local_error    = NULL;
  g_autoptr (GHashTable) cached_set = NULL;
  g_autoptr (GPtrArray) futures     = NULL;
  GHashTableIter iter               = { 0 };
  g_autoptr (GPtrArray) entries     = NULL;

  bz_weak_get_or_return_reject (self, wr);

  cached_set = dex_await_boxed (
      bz_entry_cache_manager_enumerate_disk (self->cache),
      &local_error);
  if (cached_set == NULL)
    {
      g_warning ("Unable to enumerate cached entries: %s", local_error->message);
      return dex_future_new_for_error (g_steal_pointer (&local_error));
    }

  futures = g_ptr_array_new_with_free_func (dex_unref);

  g_hash_table_iter_init (&iter, cached_set);
  for (;;)
    {
      char *checksum = NULL;

      if (!g_hash_table_iter_next (
              &iter, (gpointer *) &checksum, NULL))
        break;

      g_ptr_array_add (
          futures,
          bz_entry_cache_manager_get_by_checksum (
              self->cache, checksum));
    }
  g_clear_pointer (&cached_set, g_hash_table_unref);

  if (futures->len > 0)
    dex_await (dex_future_allv (
                   (DexFuture *const *) futures->pdata,
                   futures->len),
               NULL);

  entries = g_ptr_array_new_with_free_func (g_object_unref);
  for (guint i = 0; i < futures->len; i++)
    {
      DexFuture    *future = NULL;
      const GValue *value  = NULL;

      future = g_ptr_array_index (futures, i);
      value  = dex_future_get_value (future, &local_error);
      if (value != NULL)
        {
          g_autoptr (BzEntry) entry = NULL;

          entry = g_value_dup_object (value);
          if (BZ_IS_FLATPAK_ENTRY (entry) &&
              bz_flatpak_entry_get_bundle_path (BZ_FLATPAK_ENTRY (entry)) != NULL)
            /* refrain from restoring bundle entries */
            continue;

          g_ptr_array_add (entries, g_steal_pointer (&entry));
        }
      else
        {
          g_warning ("Unable to retrieve cached entry: %s", local_error->message);
          g_clear_error (&local_error);
        }
    }

  g_ptr_array_sort_values_with_data (
      entries, (GCompareDataFunc) cmp_entry, NULL);
  for (guint i = 0; i < entries->len; i++)
    {
      BzEntry *entry = NULL;

      entry = g_ptr_array_index (entries, i);
      fiber_replace_entry (self, entry);
    }

  gtk_filter_changed (GTK_FILTER (self->group_filter), GTK_FILTER_CHANGE_LESS_STRICT);
  gtk_filter_changed (GTK_FILTER (self->appid_filter), GTK_FILTER_CHANGE_LESS_STRICT);

  bz_state_info_set_background_task_label (self->state, _ ("Checking for updates…"));
  fiber_check_for_updates (self);
  finish_with_background_task_label (self);

  return dex_future_new_true ();
}

static DexFuture *
cache_flathub_fiber (GWeakRef *wr)
{
  g_autoptr (BzApplication) self       = NULL;
  g_autoptr (GError) local_error       = NULL;
  gboolean         result              = FALSE;
  g_autofree char *flathub_cache       = NULL;
  g_autoptr (GFile) flathub_cache_file = NULL;

  bz_weak_get_or_return_reject (self, wr);

  flathub_cache_file = fiber_dup_flathub_cache_file (&flathub_cache, &local_error);
  if (flathub_cache_file != NULL)
    {
      g_autoptr (GVariantBuilder) builder = NULL;
      g_autoptr (GVariant) variant        = NULL;
      g_autoptr (GBytes) bytes            = NULL;

      builder = g_variant_builder_new (G_VARIANT_TYPE_VARDICT);
      bz_serializable_serialize (BZ_SERIALIZABLE (self->flathub), builder);
      variant = g_variant_builder_end (builder);
      bytes   = g_variant_get_data_as_bytes (variant);

      result = dex_await (
          dex_file_replace_contents_bytes (
              flathub_cache_file, bytes,
              NULL, FALSE,
              G_FILE_CREATE_REPLACE_DESTINATION),
          &local_error);
      if (!result)
        {
          g_warning ("Failed to cache flathub state to %s: %s",
                     flathub_cache, local_error->message);
          g_clear_error (&local_error);
        }
    }
  else
    {
      g_warning ("Unable to ensure cache directory: %s", local_error->message);
      g_clear_error (&local_error);
    }

  return dex_future_new_true ();
}

static DexFuture *
respond_to_flatpak_fiber (RespondToFlatpakData *data)
{
  g_autoptr (BzApplication) self            = NULL;
  BzBackendNotification *notif              = data->notif;
  g_autoptr (GError) local_error            = NULL;
  g_autoptr (GPtrArray) build_futures       = NULL;
  g_autoptr (GPtrArray) build_notify_groups = NULL;
  g_autoptr (DexFuture) read_future         = NULL;
  g_autoptr (DexFuture) reread_timeout      = NULL;
  gboolean update_labels                    = FALSE;
  gboolean update_filters                   = FALSE;

  bz_weak_get_or_return_reject (self, data->self);

  build_futures       = g_ptr_array_new_with_free_func (dex_unref);
  build_notify_groups = g_ptr_array_new_with_free_func (g_object_unref);

  read_future = dex_future_new_for_object (notif);

  /* `reread_timeout` defines how long we are allowed to spend adding to
     `build-futures` before we update the UI later */
  reread_timeout = dex_timeout_new_msec (100);
  for (;;)
    {
      BzBackendNotificationKind kind = 0;

      if (!dex_future_is_resolved (read_future))
        {
          g_autoptr (DexFuture) future = NULL;

          future = dex_future_all_race (
              dex_ref (reread_timeout),
              dex_ref (read_future),
              NULL);
          dex_await (g_steal_pointer (&future), NULL);
          if (!dex_future_is_pending (reread_timeout))
            break;
        }

      notif = g_value_get_object (dex_future_get_value (read_future, NULL));
      kind  = bz_backend_notification_get_kind (notif);
      switch (kind)
        {
        case BZ_BACKEND_NOTIFICATION_KIND_PRESENT_ID:
          {
            const char *id = NULL;

            id = bz_backend_notification_get_generic_id (notif);
            if (id == NULL)
              break;

            open_generic_id (self, id);
          }
          break;
        case BZ_BACKEND_NOTIFICATION_KIND_ERROR:
          {
            const char *error  = NULL;
            GtkWindow  *window = NULL;

            error = bz_backend_notification_get_error (notif);
            if (error == NULL)
              break;

            g_warning ("Received an error from the flatpak backend: %s", error);

            window = gtk_application_get_active_window (GTK_APPLICATION (self));
            if (window != NULL)
              bz_show_error_for_widget (GTK_WIDGET (window), _ ("A backend error occurred"), error);
          }
          break;
        case BZ_BACKEND_NOTIFICATION_KIND_TELL_INCOMING:
          {
            int n_incoming = 0;

            n_incoming = bz_backend_notification_get_n_incoming (notif);
            self->n_entries_incoming += n_incoming;

            update_labels = TRUE;
          }
          break;
        case BZ_BACKEND_NOTIFICATION_KIND_REPLACE_ENTRY:
          {
            BzEntry *entry = NULL;

            entry = bz_backend_notification_get_entry (notif);
            fiber_replace_entry (self, entry);

            g_ptr_array_add (build_futures, bz_entry_cache_manager_add (self->cache, entry));
            if (bz_entry_is_of_kinds (entry, BZ_ENTRY_KIND_APPLICATION))
              {
                const char   *id    = NULL;
                BzEntryGroup *group = NULL;

                update_filters = TRUE;

                id    = bz_entry_get_id (entry);
                group = g_hash_table_lookup (self->ids_to_groups, id);
                if (group != NULL)
                  g_ptr_array_add (build_notify_groups, g_object_ref (group));
              }

            self->n_entries_incoming--;
            update_labels = TRUE;
          }
          break;
        case BZ_BACKEND_NOTIFICATION_KIND_REMOTE_SYNC_START:
          {
            const char *remote_name = NULL;

            remote_name = bz_backend_notification_get_remote_name (notif);

            if (bz_state_info_get_has_flathub (self->state))
              /* We only count instances of the "flathub" remote if we have
                 flathub, since that's all we care about for the UI */
              {
                if (g_strcmp0 (remote_name, "flathub") == 0)
                  self->n_remotes_syncing++;
              }
            else
              self->n_remotes_syncing++;

            g_debug ("remote '%s' has begun synchronization; "
                     "now currently syncing %u remote(s)",
                     remote_name, self->n_remotes_syncing);
          }
          break;
        case BZ_BACKEND_NOTIFICATION_KIND_REMOTE_SYNC_FINISH:
          {
            const char *remote_name = NULL;

            remote_name = bz_backend_notification_get_remote_name (notif);

            if (bz_state_info_get_has_flathub (self->state))
              {
                if (g_strcmp0 (remote_name, "flathub") == 0)
                  {
                    self->n_remotes_syncing--;
                    if (self->n_remotes_syncing == 0)
                      self->flathub_remote_initialized = TRUE;
                  }
              }
            else
              {
                self->n_remotes_syncing--;
                if (self->n_remotes_syncing == 0)
                  bz_state_info_set_busy (self->state, FALSE);
              }

            g_debug ("remote '%s' has finished synchronization; "
                     "now currently syncing %u remote(s)",
                     remote_name, self->n_remotes_syncing);
          }
          break;
        case BZ_BACKEND_NOTIFICATION_KIND_INSTALL_DONE:
        case BZ_BACKEND_NOTIFICATION_KIND_UPDATE_DONE:
        case BZ_BACKEND_NOTIFICATION_KIND_REMOVE_DONE:
          {
            const char *unique_id     = NULL;
            g_autoptr (BzEntry) entry = NULL;

            unique_id = bz_backend_notification_get_unique_id (notif);
            entry     = dex_await_object (
                bz_entry_cache_manager_get (self->cache, unique_id),
                &local_error);
            if (entry == NULL)
              {
                g_warning ("Backend notification references an entry "
                           "which couldn't be decached: %s",
                           local_error->message);
                break;
              }

            switch (kind)
              {
              case BZ_BACKEND_NOTIFICATION_KIND_INSTALL_DONE:
                {
                  const char *version = NULL;

                  version = bz_backend_notification_get_version (notif);

                  g_hash_table_replace (self->installed_set, g_strdup (unique_id), g_strdup (version));
                  bz_entry_set_installed_version (entry, version);
                  bz_entry_set_installed (entry, TRUE);

                  if (bz_entry_is_of_kinds (entry, BZ_ENTRY_KIND_APPLICATION))
                    {
                      BzEntryGroup *group = NULL;

                      group = g_hash_table_lookup (self->ids_to_groups, bz_entry_get_id (entry));
                      if (group != NULL)
                        {
                          gboolean found    = FALSE;
                          guint    position = 0;

                          found = g_list_store_find (self->installed_apps, group, &position);
                          if (!found)
                            g_list_store_insert_sorted (self->installed_apps, group, (GCompareDataFunc) cmp_group, NULL);
                        }
                    }
                }
                break;
              case BZ_BACKEND_NOTIFICATION_KIND_UPDATE_DONE:
                {
                  const char *version = NULL;

                  version = bz_backend_notification_get_version (notif);
                  g_hash_table_replace (self->installed_set, g_strdup (unique_id), g_strdup (version));
                }
                break;
              case BZ_BACKEND_NOTIFICATION_KIND_REMOVE_DONE:
                {
                  bz_entry_set_installed_version (entry, NULL);
                  bz_entry_set_installed (entry, FALSE);
                  g_hash_table_remove (self->installed_set, unique_id);

                  if (bz_entry_is_of_kinds (entry, BZ_ENTRY_KIND_APPLICATION))
                    {
                      BzEntryGroup *group = NULL;

                      group = g_hash_table_lookup (self->ids_to_groups, bz_entry_get_id (entry));
                      if (group != NULL && !bz_entry_group_get_removable (group))
                        {
                          gboolean found    = FALSE;
                          guint    position = 0;

                          found = g_list_store_find (self->installed_apps, group, &position);
                          if (found)
                            g_list_store_remove (self->installed_apps, position);
                        }
                    }
                }
                break;
              case BZ_BACKEND_NOTIFICATION_KIND_ERROR:
              case BZ_BACKEND_NOTIFICATION_KIND_EXTERNAL_CHANGE:
              case BZ_BACKEND_NOTIFICATION_KIND_INVALIDATE_REMOTES:
              case BZ_BACKEND_NOTIFICATION_KIND_PRESENT_ID:
              case BZ_BACKEND_NOTIFICATION_KIND_REMOTE_SYNC_FINISH:
              case BZ_BACKEND_NOTIFICATION_KIND_REMOTE_SYNC_START:
              case BZ_BACKEND_NOTIFICATION_KIND_REPLACE_ENTRY:
              case BZ_BACKEND_NOTIFICATION_KIND_TELL_INCOMING:
              default:
                g_assert_not_reached ();
              };

            g_ptr_array_add (build_futures, bz_entry_cache_manager_add (self->cache, entry));
            if (bz_entry_is_of_kinds (entry, BZ_ENTRY_KIND_APPLICATION))
              {
                const char   *id    = NULL;
                BzEntryGroup *group = NULL;

                id    = bz_entry_get_id (entry);
                group = g_hash_table_lookup (self->ids_to_groups, id);
                if (group != NULL)
                  g_ptr_array_add (build_notify_groups, g_object_ref (group));
              }
          }
          break;
        case BZ_BACKEND_NOTIFICATION_KIND_INVALIDATE_REMOTES:
        case BZ_BACKEND_NOTIFICATION_KIND_EXTERNAL_CHANGE:
          {
            bz_state_info_set_background_task_label (self->state, _ ("Refreshing…"));

            if (data->is_gnome_ext)
              {
                g_autoptr (GHashTable) new_ids = NULL;
                GHashTableIter iter            = { 0 };
                const char    *uuid            = NULL;
                gpointer       val             = NULL;

                new_ids = dex_await_boxed (
                    bz_backend_retrieve_install_ids (
                        BZ_BACKEND (self->gnome_ext), NULL),
                    &local_error);
                if (new_ids == NULL)
                  {
                    g_warning ("Failed to re-enumerate GNOME extensions: %s",
                               local_error->message);
                    g_clear_error (&local_error);
                    finish_with_background_task_label (self);
                    break;
                  }

                for (guint i = g_list_model_get_n_items (G_LIST_MODEL (self->gnome_ext_infos)); i > 0; i--)
                  {
                    g_autoptr (BzGnomeExtensionInfo) info = g_list_model_get_item (
                        G_LIST_MODEL (self->gnome_ext_infos), i - 1);
                    const char *old_uuid = bz_gnome_extension_info_get_uuid (info);

                    if (!g_hash_table_contains (new_ids, old_uuid))
                      {
                        g_autoptr (BzEntry) entry = dex_await_object (
                            bz_entry_cache_manager_get (self->cache, old_uuid), NULL);

                        if (entry != NULL)
                          {
                            BzEntryGroup *group    = NULL;
                            guint         position = 0;

                            bz_entry_set_installed (entry, FALSE);
                            bz_entry_set_installed_version (entry, NULL);

                            group = g_hash_table_lookup (self->ids_to_groups, bz_entry_get_id (entry));
                            if (group != NULL &&
                                !bz_entry_group_get_removable (group) &&
                                g_list_store_find (self->installed_apps, group, &position))
                              g_list_store_remove (self->installed_apps, position);
                          }

                        g_list_store_remove (self->gnome_ext_infos, i - 1);
                      }
                  }

                g_hash_table_iter_init (&iter, new_ids);
                while (g_hash_table_iter_next (&iter, (gpointer *) &uuid, &val))
                  {
                    BzGnomeExtensionInfo *new_info = BZ_GNOME_EXTENSION_INFO (val);
                    guint                 n        = g_list_model_get_n_items (G_LIST_MODEL (self->gnome_ext_infos));
                    guint                 pos      = n;

                    for (guint i = 0; i < n; i++)
                      {
                        g_autoptr (BzGnomeExtensionInfo) info = g_list_model_get_item (
                            G_LIST_MODEL (self->gnome_ext_infos), i);
                        if (g_strcmp0 (bz_gnome_extension_info_get_uuid (info), uuid) == 0)
                          {
                            pos = i;
                            break;
                          }
                      }

                    if (pos < n)
                      {
                        g_autoptr (BzGnomeExtensionInfo) existing = g_list_model_get_item (
                            G_LIST_MODEL (self->gnome_ext_infos), pos);

                        g_object_freeze_notify (G_OBJECT (existing));
                        g_object_set (existing,
                                      "name", bz_gnome_extension_info_get_name (new_info),
                                      "version", bz_gnome_extension_info_get_version (new_info),
                                      "state", bz_gnome_extension_info_get_state (new_info),
                                      "enabled", bz_gnome_extension_info_get_enabled (new_info),
                                      "error", bz_gnome_extension_info_get_error (new_info),
                                      "has-prefs", bz_gnome_extension_info_get_has_prefs (new_info),
                                      "has-update", bz_gnome_extension_info_get_has_update (new_info),
                                      "can-change", bz_gnome_extension_info_get_can_change (new_info),
                                      "is-system", bz_gnome_extension_info_get_is_system (new_info),
                                      NULL);
                        g_object_thaw_notify (G_OBJECT (existing));
                      }
                    else
                      {
                        g_autoptr (BzEntry) entry = dex_await_object (
                            bz_entry_cache_manager_get (self->cache, uuid), NULL);

                        g_list_store_append (self->gnome_ext_infos, new_info);

                        if (entry != NULL)
                          {
                            BzEntryGroup *group    = NULL;
                            guint         position = 0;

                            bz_entry_set_installed (entry, TRUE);

                            group = g_hash_table_lookup (self->ids_to_groups, bz_entry_get_id (entry));
                            if (group != NULL &&
                                !g_list_store_find (self->installed_apps, group, &position))
                              g_list_store_insert_sorted (self->installed_apps, group,
                                                          (GCompareDataFunc) cmp_group, NULL);
                          }
                      }
                  }

                bz_state_info_set_gnome_extension_infos (
                    self->state, G_LIST_MODEL (self->gnome_ext_infos));

                finish_with_background_task_label (self);
              }
            else
              {
                g_autoptr (GListModel) repos         = NULL;
                g_autoptr (GHashTable) installed_set = NULL;
                g_autoptr (GPtrArray) diff_reads     = NULL;
                GHashTableIter old_iter              = { 0 };
                GHashTableIter new_iter              = { 0 };
                g_autoptr (GPtrArray) diff_writes    = NULL;

                repos = dex_await_object (
                    bz_backend_list_repositories (BZ_BACKEND (self->flatpak), NULL),
                    &local_error);

                if (repos != NULL)
                  bz_state_info_set_repositories (self->state, repos);
                else
                  {
                    g_warning ("Failed to enumerate repositories: %s", local_error->message);
                    g_clear_error (&local_error);
                  }

                installed_set = dex_await_boxed (
                    bz_backend_retrieve_install_ids (
                        BZ_BACKEND (self->flatpak), NULL),
                    &local_error);
                if (installed_set == NULL)
                  {
                    g_warning ("Failed to enumerate installed entries: %s",
                               local_error->message);
                    finish_with_background_task_label (self);
                    break;
                  }

                diff_reads = g_ptr_array_new_with_free_func (dex_unref);

                g_hash_table_iter_init (&old_iter, self->installed_set);
                for (;;)
                  {
                    char *unique_id = NULL;

                    if (!g_hash_table_iter_next (
                            &old_iter, (gpointer *) &unique_id, NULL))
                      break;

                    if (!g_hash_table_contains (installed_set, unique_id))
                      g_ptr_array_add (
                          diff_reads,
                          bz_entry_cache_manager_get (self->cache, unique_id));
                  }

                g_hash_table_iter_init (&new_iter, installed_set);
                for (;;)
                  {
                    char *unique_id = NULL;

                    if (!g_hash_table_iter_next (
                            &new_iter, (gpointer *) &unique_id, NULL))
                      break;

                    if (!g_hash_table_contains (self->installed_set, unique_id))
                      g_ptr_array_add (
                          diff_reads,
                          bz_entry_cache_manager_get (self->cache, unique_id));
                  }

                if (diff_reads->len > 0)
                  {
                    dex_await (dex_future_allv (
                                   (DexFuture *const *) diff_reads->pdata,
                                   diff_reads->len),
                               NULL);

                    diff_writes = g_ptr_array_new_with_free_func (dex_unref);
                    for (guint i = 0; i < diff_reads->len; i++)
                      {
                        DexFuture *future = NULL;

                        future = g_ptr_array_index (diff_reads, i);
                        if (dex_future_is_resolved (future))
                          {
                            BzEntry      *entry     = NULL;
                            const char   *id        = NULL;
                            const char   *unique_id = NULL;
                            BzEntryGroup *group     = NULL;
                            gboolean      installed = FALSE;
                            const char   *version   = NULL;

                            entry = g_value_get_object (dex_future_get_value (future, NULL));
                            id    = bz_entry_get_id (entry);
                            group = g_hash_table_lookup (self->ids_to_groups, id);
                            if (group != NULL)
                              bz_entry_group_connect_living (group, entry);

                            unique_id = bz_entry_get_unique_id (entry);
                            installed = g_hash_table_contains (installed_set, unique_id);

                            version = g_hash_table_lookup (installed_set, unique_id);
                            if (installed && version != NULL && *version != '\0')
                              bz_entry_set_installed_version (entry, version);

                            bz_entry_set_installed (entry, installed);

                            if (group != NULL)
                              {
                                gboolean found    = FALSE;
                                guint    position = 0;

                                found = g_list_store_find (
                                    self->installed_apps, group, &position);
                                if (installed && !found)
                                  g_list_store_insert_sorted (
                                      self->installed_apps, group,
                                      (GCompareDataFunc) cmp_group, NULL);
                                else if (!installed && found &&
                                         bz_entry_group_get_removable (group) == 0)
                                  g_list_store_remove (self->installed_apps, position);
                              }

                            g_ptr_array_add (
                                diff_writes,
                                bz_entry_cache_manager_add (self->cache, entry));
                          }
                      }

                    dex_await (dex_future_allv (
                                   (DexFuture *const *) diff_writes->pdata,
                                   diff_writes->len),
                               NULL);
                  }

                g_clear_pointer (&self->installed_set, g_hash_table_unref);
                self->installed_set = g_steal_pointer (&installed_set);

                fiber_check_for_updates (self);
                finish_with_background_task_label (self);
              }
          }
          break;

          break;
        default:
          g_assert_not_reached ();
        }

      dex_clear (&read_future);
      read_future = dex_channel_receive (data->channel);

      if (!dex_future_is_pending (reread_timeout))
        break;
    }

  if (build_futures->len > 0)
    {
      g_autoptr (DexFuture) future                   = NULL;
      g_autoptr (CacheWriteBackData) write_back_data = NULL;

      future = dex_future_allv (
          (DexFuture *const *) build_futures->pdata,
          build_futures->len);

      write_back_data                 = cache_write_back_data_new ();
      write_back_data->self           = bz_track_weak (self);
      write_back_data->notify_groups  = g_ptr_array_ref (build_notify_groups);
      write_back_data->update_filters = update_filters;

      future = dex_future_finally (
          future,
          (DexFutureCallback) cache_write_back_finally,
          cache_write_back_data_ref (write_back_data),
          cache_write_back_data_unref);
      dex_future_disown (g_steal_pointer (&future));
    }

  if (update_labels)
    {
      if (self->n_entries_incoming > 0)
        bz_state_info_set_background_task_label_take_printf (
            self->state, _ ("Loading %d apps…"), self->n_entries_incoming);
      else
        {
          bz_state_info_set_background_task_label (self->state, _ ("Checking for updates…"));
          fiber_check_for_updates (self);
          finish_with_background_task_label (self);
        }
    }

  if (self->n_entries_incoming == 0 &&
      self->flathub_remote_initialized)
    {
      bz_state_info_set_busy (self->state, FALSE);
      finish_with_background_task_label (self);
    }

  return g_steal_pointer (&read_future);
}

static DexFuture *
open_appstream_fiber (OpenAppstreamData *data)
{
  g_autoptr (BzApplication) self = NULL;
  char *id                       = data->id;

  bz_weak_get_or_return_reject (self, data->self);
  dex_await (dex_ref (self->ready_to_open_files), NULL);

  open_generic_id (self, id);
  return dex_future_new_true ();
}

static DexFuture *
open_flatpakref_fiber (OpenFlatpakrefData *data)
{
  g_autoptr (BzApplication) self = NULL;
  GFile *file                    = data->file;
  g_autoptr (GError) local_error = NULL;
  g_autoptr (DexFuture) future   = NULL;
  const GValue *value            = NULL;

  bz_weak_get_or_return_reject (self, data->self);
  dex_await (dex_ref (self->ready_to_open_files), NULL);

  future = bz_backend_load_local_package (BZ_BACKEND (self->flatpak), file, NULL);
  dex_await (dex_ref (future), NULL);

  value = dex_future_get_value (future, &local_error);
  if (value == NULL)
    {
      GtkWindow *window = NULL;

      window = gtk_application_get_active_window (GTK_APPLICATION (self));
      if (window == NULL)
        window = new_window (self);

      bz_show_error_for_widget (
          GTK_WIDGET (window),
          _ ("Failed to open file"),
          local_error->message);

      return dex_future_new_for_error (g_steal_pointer (&local_error));
    }

  if (G_VALUE_HOLDS_OBJECT (value))
    {
      GtkWindow             *window     = NULL;
      BzEntry               *entry      = NULL;
      BzBundleInstallDialog *install_ui = NULL;
      AdwDialog             *dialog     = NULL;

      window = gtk_application_get_active_window (GTK_APPLICATION (self));
      if (window == NULL)
        window = new_window (self);

      entry      = g_value_get_object (value);
      install_ui = g_object_new (
          BZ_TYPE_BUNDLE_INSTALL_DIALOG,
          "state", self->state,
          "entry", entry,
          NULL);

      dialog = adw_dialog_new ();
      adw_dialog_set_follows_content_size (dialog, TRUE);
      adw_dialog_set_child (dialog, GTK_WIDGET (install_ui));

      adw_dialog_present (dialog, GTK_WIDGET (window));
    }

  return dex_future_new_true ();
}

static DexFuture *
init_fiber_finally (DexFuture *future,
                    GWeakRef  *wr)
{
  g_autoptr (BzApplication) self = NULL;
  g_autoptr (GError) local_error = NULL;
  const GValue *value            = NULL;

  bz_weak_get_or_return_reject (self, wr);

  value = dex_future_get_value (future, &local_error);
  if (value != NULL)
    {
      g_autoptr (DexFuture) sync_future = NULL;

      self->flatpak_notifs = bz_backend_create_notification_channel (
          BZ_BACKEND (self->flatpak));

      {
        g_autoptr (WatchNotifsData) wd = watch_notifs_data_new ();
        wd->self                       = bz_track_weak (self);
        wd->channel                    = dex_ref (self->flatpak_notifs);

        self->notif_watch = dex_future_then_loop (
            dex_channel_receive (self->flatpak_notifs),
            (DexFutureCallback) watch_backend_notifs_then_loop_cb,
            watch_notifs_data_ref (wd),
            watch_notifs_data_unref);
      }

      if (self->gnome_ext != NULL)
        {
          g_autoptr (DexChannel) ext_channel = NULL;
          g_autoptr (WatchNotifsData) wd     = watch_notifs_data_new ();

          ext_channel = bz_backend_create_notification_channel (
              BZ_BACKEND (self->gnome_ext));

          wd->self         = bz_track_weak (self);
          wd->channel      = dex_ref (ext_channel);
          wd->is_gnome_ext = TRUE;

          dex_future_disown (dex_future_then_loop (
              dex_channel_receive (ext_channel),
              (DexFutureCallback) watch_backend_notifs_then_loop_cb,
              watch_notifs_data_ref (wd),
              watch_notifs_data_unref));

          dex_future_disown (bz_backend_retrieve_remote_entries (
              BZ_BACKEND (self->gnome_ext), NULL));
        }

      sync_future = make_sync_future (self);
      sync_future = dex_future_finally (
          sync_future,
          (DexFutureCallback) init_sync_finally,
          bz_track_weak (self),
          bz_weak_release);
      self->sync = g_steal_pointer (&sync_future);

      self->periodic_timeout_source = g_timeout_add_seconds (
          /* Check every day */
          60 * 60 * 24, (GSourceFunc) periodic_timeout_cb, self);

      bz_malcontent_service_start (self->malcontent);
    }
  else
    {
      GtkWindow *window = NULL;

      bz_state_info_set_online (self->state, FALSE);
      bz_state_info_set_busy (self->state, FALSE);
      window = gtk_application_get_active_window (GTK_APPLICATION (self));
      if (window != NULL)
        {
          g_autofree char *error_string = NULL;

          error_string = g_strdup_printf (
              "Could not initialize: %s",
              local_error->message);
          bz_show_error_for_widget (GTK_WIDGET (window), _ ("An initialization error occurred"), error_string);
        }
    }

  return dex_future_new_true ();
}

static DexFuture *
init_sync_finally (DexFuture *future,
                   GWeakRef  *wr)
{
  g_autoptr (BzApplication) self = NULL;
  g_autoptr (GError) local_error = NULL;

  bz_weak_get_or_return_reject (self, wr);

  /* Do nothing */

  return dex_future_new_true ();
}

static DexFuture *
backend_sync_finally (DexFuture *future,
                      GWeakRef  *wr)
{
  g_autoptr (BzApplication) self = NULL;

  bz_weak_get_or_return_reject (self, wr);

  if (dex_future_is_resolved (future))
    return dex_scheduler_spawn (
        dex_scheduler_get_default (),
        bz_get_dex_stack_size (),
        (DexFiberFunc) enumerate_disk_entries_fiber,
        bz_track_weak (self),
        bz_weak_release);
  else
    return dex_ref (future);
}

static DexFuture *
flathub_update_finally (DexFuture *future,
                        GWeakRef  *wr)
{
  g_autoptr (BzApplication) self = NULL;

  bz_weak_get_or_return_reject (self, wr);

  if (dex_future_is_resolved (future))
    {
      g_clear_object (&self->flathub);
      g_assert (self->tmp_flathub != NULL);
      self->flathub = g_steal_pointer (&self->tmp_flathub);
      bz_flathub_state_set_map_factory (self->flathub, self->application_factory);
      bz_state_info_set_flathub (self->state, self->flathub);

      return dex_scheduler_spawn (
          dex_scheduler_get_default (),
          bz_get_dex_stack_size (),
          (DexFiberFunc) cache_flathub_fiber,
          bz_track_weak (self), bz_weak_release);
    }
  else
    {
      g_clear_object (&self->tmp_flathub);
      return dex_ref (future);
    }
}

static DexFuture *
cache_write_back_finally (DexFuture          *future,
                          CacheWriteBackData *data)
{
  g_autoptr (BzApplication) self = NULL;
  GPtrArray *notify_groups       = data->notify_groups;
  gboolean   update_filters      = data->update_filters;

  bz_weak_get_or_return_reject (self, data->self);

  for (guint i = 0; i < notify_groups->len; i++)
    {
      BzEntryGroup *group = NULL;

      group = g_ptr_array_index (notify_groups, i);
      g_object_notify (G_OBJECT (group), "ui-entry");
    }

  if (update_filters)
    {
      gtk_filter_changed (GTK_FILTER (self->group_filter), GTK_FILTER_CHANGE_LESS_STRICT);
      gtk_filter_changed (GTK_FILTER (self->appid_filter), GTK_FILTER_CHANGE_LESS_STRICT);
    }

  return dex_future_new_true ();
}

static DexFuture *
sync_finally (DexFuture *future,
              GWeakRef  *wr)
{
  g_autoptr (BzApplication) self = NULL;

  bz_weak_get_or_return_reject (self, wr);

  bz_state_info_set_online (self->state, dex_future_is_resolved (future));
  bz_state_info_set_allow_manual_sync (self->state, TRUE);
  bz_state_info_set_busy (self->state, FALSE);
  bz_state_info_set_syncing (self->state, FALSE);
  finish_with_background_task_label (self);

  dex_promise_resolve_boolean (self->ready_to_open_files, TRUE);

  return dex_future_new_true ();
}

static DexFuture *
watch_backend_notifs_then_loop_cb (DexFuture       *future,
                                   WatchNotifsData *data)
{
  g_autoptr (BzApplication) self         = NULL;
  const GValue          *value           = NULL;
  BzBackendNotification *notif           = NULL;
  g_autoptr (RespondToFlatpakData) rdata = NULL;
  g_autoptr (DexFuture) ret_future       = NULL;

  bz_weak_get_or_return_reject (self, data->self);

  value = dex_future_get_value (future, NULL);
  g_assert (value != NULL);
  notif = g_value_get_object (value);

  rdata               = respond_to_flatpak_data_new ();
  rdata->self         = bz_track_weak (self);
  rdata->notif        = g_object_ref (notif);
  rdata->channel      = dex_ref (data->channel);
  rdata->is_gnome_ext = data->is_gnome_ext;

  ret_future = dex_scheduler_spawn (
      dex_scheduler_get_default (),
      bz_get_dex_stack_size (),
      (DexFiberFunc) respond_to_flatpak_fiber,
      respond_to_flatpak_data_ref (rdata),
      respond_to_flatpak_data_unref);
  return g_steal_pointer (&ret_future);
}

static BzEntryGroup *
ensure_group_and_add (BzApplication *self,
                      const char    *id,
                      BzEntry       *entry,
                      BzEntry       *eol_runtime,
                      gboolean       ignore_eol,
                      gboolean       installed)
{
  BzEntryGroup *group = NULL;

  group = g_hash_table_lookup (self->ids_to_groups, id);
  if (group != NULL)
    bz_entry_group_add (group, entry, eol_runtime, ignore_eol);
  else
    {
      g_autoptr (BzEntryGroup) new_group = NULL;

      g_debug ("Creating new application group for id %s", id);
      new_group = bz_entry_group_new (self->entry_factory);
      bz_entry_group_add (new_group, entry, eol_runtime, ignore_eol);

      g_list_store_append (self->groups, new_group);
      g_hash_table_replace (self->ids_to_groups, g_strdup (id), g_object_ref (new_group));

      group = new_group;
    }

  if (installed && !g_list_store_find (self->installed_apps, group, NULL))
    g_list_store_insert_sorted (
        self->installed_apps, group,
        (GCompareDataFunc) cmp_group, NULL);

  return group;
}

static void
fiber_replace_entry (BzApplication *self,
                     BzEntry       *entry)
{
  const char *id                 = NULL;
  const char *unique_id          = NULL;
  const char *unique_id_checksum = NULL;
  gboolean    user               = FALSE;
  gboolean    installed          = FALSE;
  const char *flatpak_id         = NULL;
  const char *version            = NULL;
  GHashTable *name_to_addons     = NULL;
  gboolean    is_flatpak         = FALSE;

  id                 = bz_entry_get_id (entry);
  unique_id          = bz_entry_get_unique_id (entry);
  unique_id_checksum = bz_entry_get_unique_id_checksum (entry);
  if (id == NULL ||
      unique_id == NULL ||
      unique_id_checksum == NULL)
    return;

  is_flatpak = BZ_IS_FLATPAK_ENTRY (entry);

  if (is_flatpak)
    {
      user           = bz_flatpak_entry_is_user (BZ_FLATPAK_ENTRY (entry));
      name_to_addons = user ? self->usr_name_to_addons : self->sys_name_to_addons;
      flatpak_id     = bz_flatpak_entry_get_flatpak_id (BZ_FLATPAK_ENTRY (entry));
    }
  else
    {
      name_to_addons = self->sys_name_to_addons;
    }

  installed = g_hash_table_contains (self->installed_set, unique_id);
  bz_entry_set_installed (entry, installed);

  version = g_hash_table_lookup (self->installed_set, unique_id);
  if (version != NULL && *version != '\0')
    bz_entry_set_installed_version (entry, version);

  if (flatpak_id != NULL)
    {
      GPtrArray *addons = NULL;

      addons = g_hash_table_lookup (name_to_addons, flatpak_id);
      if (addons != NULL)
        {
          g_debug ("Appending %d addons to %s", addons->len, unique_id);
          for (guint i = 0; i < addons->len; i++)
            {
              const char *addon_id = NULL;

              addon_id = g_ptr_array_index (addons, i);
              bz_entry_append_addon (entry, addon_id);
            }
          g_hash_table_remove (name_to_addons, flatpak_id);
          addons = NULL;
        }
    }

  if (bz_entry_is_of_kinds (entry, BZ_ENTRY_KIND_APPLICATION))
    {
      gboolean    ignore_eol               = FALSE;
      const char *runtime_name             = NULL;
      g_autoptr (BzEntry) eol_runtime      = NULL;
      BzEntryGroup *group                  = NULL;
      GHashTable   *ref_to_addon_group_ids = NULL;
      GPtrArray    *pending                = NULL;

      if (self->ignore_eol_set != NULL)
        ignore_eol = g_hash_table_contains (self->ignore_eol_set, id);

      if (is_flatpak)
        {
          runtime_name = bz_flatpak_entry_get_application_runtime (BZ_FLATPAK_ENTRY (entry));
          if (!ignore_eol && runtime_name != NULL)
            {
              char *runtime_checksum = NULL;

              runtime_checksum = g_hash_table_lookup (self->eol_runtimes, runtime_name);
              if (runtime_checksum != NULL)
                eol_runtime = dex_await_object (
                    bz_entry_cache_manager_get_by_checksum (
                        self->cache,
                        runtime_checksum),
                    NULL);
            }
        }

      group = ensure_group_and_add (self, id, entry, eol_runtime, ignore_eol, installed);

      ref_to_addon_group_ids =
          user
              ? self->usr_ref_to_addon_group_ids
              : self->sys_ref_to_addon_group_ids;
      pending = g_hash_table_lookup (ref_to_addon_group_ids, id);
      if (pending != NULL)
        {
          for (guint i = 0; i < pending->len; i++)
            bz_entry_group_append_addon_group_id (group, g_ptr_array_index (pending, i));
          g_hash_table_remove (ref_to_addon_group_ids, id);
        }
    }

  if (is_flatpak &&
      flatpak_id != NULL &&
      bz_entry_is_of_kinds (entry, BZ_ENTRY_KIND_RUNTIME) &&
      g_str_has_prefix (flatpak_id, "runtime/"))
    {
      const char *stripped = NULL;
      const char *eol      = NULL;

      stripped = flatpak_id + strlen ("runtime/");

      eol = bz_entry_get_eol (entry);
      if (eol != NULL)
        g_hash_table_replace (
            self->eol_runtimes,
            g_strdup (stripped),
            g_strdup (unique_id_checksum));
      else
        g_hash_table_remove (self->eol_runtimes, stripped);
    }

  if (is_flatpak &&
      bz_entry_is_of_kinds (entry, BZ_ENTRY_KIND_ADDON) &&
      strstr (id, ".Debug") == NULL &&
      strstr (id, ".Locale") == NULL)
    {
      const char *extension_of_what = NULL;

      extension_of_what = bz_flatpak_entry_get_addon_extension_of_ref (
          BZ_FLATPAK_ENTRY (entry));

      if (extension_of_what != NULL &&
          g_str_has_prefix (extension_of_what, "app/"))
        {
          g_auto (GStrv) parts    = NULL;
          BzEntryGroup *app_group = NULL;

          ensure_group_and_add (self, id, entry, NULL, FALSE, installed);

          parts = g_strsplit (extension_of_what, "/", -1);
          if (parts != NULL && parts[1] != NULL)
            {
              app_group = g_hash_table_lookup (self->ids_to_groups, parts[1]);
              if (app_group != NULL)
                bz_entry_group_append_addon_group_id (app_group, id);
              else
                {
                  GHashTable *ref_to_addon_group_ids =
                      user
                          ? self->usr_ref_to_addon_group_ids
                          : self->sys_ref_to_addon_group_ids;
                  GPtrArray *pending = NULL;

                  pending = g_hash_table_lookup (ref_to_addon_group_ids, parts[1]);
                  if (pending == NULL)
                    {
                      pending = g_ptr_array_new_with_free_func (g_free);
                      g_hash_table_replace (ref_to_addon_group_ids,
                                            g_strdup (parts[1]), pending);
                    }
                  g_ptr_array_add (pending, g_strdup (id));
                }
            }
        }

      if (extension_of_what != NULL)
        {
          GPtrArray *addons = NULL;

          /* BzFlatpakInstance ensures addons come before applications */
          addons = g_hash_table_lookup (name_to_addons, extension_of_what);
          if (addons == NULL)
            {
              addons = g_ptr_array_new_with_free_func (g_free);
              g_hash_table_replace (
                  name_to_addons,
                  g_strdup (extension_of_what), addons);
            }
          g_ptr_array_add (addons, g_strdup (unique_id));
        }
      else
        g_warning ("Entry with unique id %s is an addon but "
                   "does not seem to extend anything",
                   unique_id);
    }
}

static void
fiber_check_for_updates (BzApplication *self)
{
  g_autoptr (GError) local_error   = NULL;
  g_autoptr (GPtrArray) update_ids = NULL;
  GtkWindow *window                = NULL;

  g_debug ("Checking for updates...");
  bz_state_info_set_checking_for_updates (self->state, TRUE);

  if (self->gnome_ext != NULL)
    bz_gnome_extension_instance_check_for_updates (self->gnome_ext);

  update_ids = dex_await_boxed (
      bz_backend_retrieve_update_ids (BZ_BACKEND (self->flatpak), NULL),
      &local_error);
  window = gtk_application_get_active_window (GTK_APPLICATION (self));
  if (update_ids != NULL &&
      update_ids->len > 0)
    {
      g_autoptr (GPtrArray) futures = NULL;
      g_autoptr (GListStore) store  = NULL;

      futures = g_ptr_array_new_with_free_func (dex_unref);
      for (guint i = 0; i < update_ids->len; i++)
        {
          const char *unique_id = NULL;

          unique_id = g_ptr_array_index (update_ids, i);
          g_ptr_array_add (futures, bz_entry_cache_manager_get (self->cache, unique_id));
        }

      dex_await (
          dex_future_allv ((DexFuture *const *) futures->pdata, futures->len),
          NULL);

      store = g_list_store_new (BZ_TYPE_ENTRY);
      for (guint i = 0; i < futures->len; i++)
        {
          DexFuture    *future = NULL;
          const GValue *value  = NULL;

          future = g_ptr_array_index (futures, i);
          value  = dex_future_get_value (future, &local_error);

          if (value != NULL)
            g_list_store_append (store, g_value_get_object (value));
          else
            {
              const char *unique_id = NULL;

              unique_id = g_ptr_array_index (update_ids, i);
              g_warning ("%s could not be resolved for the update list and thus will not be included: %s",
                         unique_id, local_error->message);
              g_clear_pointer (&local_error, g_error_free);
            }
        }

      if (g_list_model_get_n_items (G_LIST_MODEL (store)) > 0)
        bz_state_info_set_available_updates (self->state, G_LIST_MODEL (store));
    }
  else if (local_error != NULL)
    {
      g_warning ("Failed to check for updates: %s", local_error->message);

      if (window != NULL)
        bz_show_error_for_widget (GTK_WIDGET (window), _ ("Failed to check for updates"), local_error->message);
    }

  bz_state_info_set_checking_for_updates (self->state, FALSE);
}

static GFile *
fiber_dup_flathub_cache_file (char   **path_out,
                              GError **error)
{
  gboolean         result           = FALSE;
  g_autofree char *module_dir       = NULL;
  g_autoptr (GFile) module_dir_file = NULL;
  g_autofree char *path             = NULL;
  g_autoptr (GFile) file            = NULL;

  module_dir      = bz_dup_module_dir ();
  module_dir_file = g_file_new_for_path (module_dir);
  result          = dex_await (
      dex_file_make_directory_with_parents (
          module_dir_file),
      error);
  if (!result)
    return NULL;

  path = g_build_filename (module_dir, "flathub-cache", NULL);
  file = g_file_new_for_path (path);

  if (path_out != NULL)
    *path_out = g_steal_pointer (&path);
  return g_steal_pointer (&file);
}

static gboolean
periodic_timeout_cb (BzApplication *self)
{
  gboolean have_connection    = FALSE;
  gboolean metered_connection = FALSE;

  if (self->sync != NULL &&
      dex_future_is_pending (self->sync))
    /* If for some reason the last update check is still happening, let it
       finish */
    goto done;

  dex_clear (&self->sync);

  have_connection    = bz_state_info_get_have_connection (self->state);
  metered_connection = bz_state_info_get_metered_connection (self->state);
  if (have_connection && !metered_connection)
    /* Do not do periodic sync on metered connections. The user will have to
       manually refresh instead. */
    self->sync = make_sync_future (self);

done:
  return G_SOURCE_CONTINUE;
}

static gboolean
scheduled_timeout_cb (GWeakRef *wr)
{
  g_autoptr (BzApplication) self = NULL;
  gboolean have_connection       = FALSE;

  /* Use weak ref here since the source tag of this callback won't be tracked by
     the main application obj */
  self = g_weak_ref_get (wr);
  if (self == NULL)
    goto done;

  dex_clear (&self->sync);
  have_connection = bz_state_info_get_have_connection (self->state);
  if (have_connection)
    self->sync = make_sync_future (self);

done:
  return G_SOURCE_REMOVE;
}

static void
network_status_changed (BzApplication   *self,
                        GParamSpec      *pspec,
                        GNetworkMonitor *network)
{
  gboolean             was_connected   = FALSE;
  gboolean             was_metered     = FALSE;
  GNetworkConnectivity connectivity    = 0;
  gboolean             have_connection = FALSE;
  gboolean             is_metered      = FALSE;

  was_connected = bz_state_info_get_have_connection (self->state);
  was_metered   = bz_state_info_get_metered_connection (self->state);

  connectivity    = g_network_monitor_get_connectivity (network);
  have_connection = connectivity == G_NETWORK_CONNECTIVITY_FULL;
  is_metered      = g_network_monitor_get_network_metered (network);

  if (!bz_state_info_get_busy (self->state) &&
      ((!was_connected &&
        have_connection &&
        !is_metered) ||
       (was_metered &&
        !is_metered)))
    /* Wait a bit to prevent flakiness */
    g_timeout_add_full (
        G_PRIORITY_DEFAULT,
        500, (GSourceFunc) scheduled_timeout_cb,
        bz_track_weak (self), bz_weak_release);

  bz_state_info_set_have_connection (self->state, have_connection);
  bz_state_info_set_metered_connection (self->state, is_metered);
}

static void
disable_blocklists_changed (BzApplication *self,
                            GParamSpec    *pspec,
                            BzStateInfo   *state)
{
  gtk_filter_changed (GTK_FILTER (self->group_filter), GTK_FILTER_CHANGE_DIFFERENT);
  gtk_filter_changed (GTK_FILTER (self->appid_filter), GTK_FILTER_CHANGE_DIFFERENT);
}

static void
show_hide_app_setting_changed (BzApplication *self,
                               const char    *key,
                               GSettings     *settings)
{
  g_object_freeze_notify (G_OBJECT (self->state));

  bz_state_info_set_hide_eol (self->state, g_settings_get_boolean (self->settings, "hide-eol"));
  bz_state_info_set_show_only_foss (self->state, g_settings_get_boolean (self->settings, "show-only-foss"));
  bz_state_info_set_show_only_flathub (self->state, g_settings_get_boolean (self->settings, "show-only-flathub"));
  bz_state_info_set_show_only_verified (self->state, g_settings_get_boolean (self->settings, "show-only-verified"));

  gtk_filter_changed (GTK_FILTER (self->group_filter), GTK_FILTER_CHANGE_DIFFERENT);
  gtk_filter_changed (GTK_FILTER (self->appid_filter), GTK_FILTER_CHANGE_DIFFERENT);

  g_object_thaw_notify (G_OBJECT (self->state));
}

static gboolean
window_close_request (BzApplication *self,
                      GtkWidget     *window)
{
  int      width             = 0;
  int      height            = 0;
  GList   *remaining_windows = NULL;
  gboolean reap_dl_workers   = TRUE;

  width  = gtk_widget_get_width (window);
  height = gtk_widget_get_height (window);

  g_settings_set (self->settings, "window-dimensions",
                  "(ii)", width, height);

  remaining_windows = gtk_application_get_windows (
      GTK_APPLICATION (self));
  for (GList *l = remaining_windows; l != NULL; l = l->next)
    {
      if (l->data != window)
        {
          reap_dl_workers = FALSE;
          break;
        }
    }
  if (reap_dl_workers)
    /* If no windows are left, kill the dl-worker subprocesses to minimize idle
       memory usage */
    bz_reap_default_download_workers ();

  /* Do not stop other handlers from being invoked for the signal */
  return FALSE;
}

static void
blocklists_changed (BzApplication *self,
                    guint          position,
                    guint          removed,
                    guint          added,
                    GListModel    *model)
{
  g_autoptr (GError) local_error = NULL;

  if (removed > 0)
    g_ptr_array_remove_range (self->blocklist_regexes, position, removed);

  for (guint i = 0; i < added; i++)
    {
      g_autoptr (BzRootBlocklist) root  = NULL;
      g_autoptr (GPtrArray) regex_datas = NULL;
      GListModel *blocklists            = NULL;

      root        = g_list_model_get_item (model, position + i);
      regex_datas = g_ptr_array_new_with_free_func (blocklist_regex_data_unref);

      blocklists = bz_root_blocklist_get_blocklists (root);
      if (blocklists != NULL)
        {
          guint n_blocklists = 0;

          n_blocklists = g_list_model_get_n_items (blocklists);
          for (guint j = 0; j < n_blocklists; j++)
            {
              g_autoptr (BzBlocklist) blocklist   = NULL;
              GListModel *conditions              = NULL;
              GListModel *allow                   = NULL;
              GListModel *allow_regex             = NULL;
              GListModel *block                   = NULL;
              GListModel *block_regex             = NULL;
              g_autoptr (BlocklistRegexData) data = NULL;

              blocklist   = g_list_model_get_item (blocklists, j);
              allow       = bz_blocklist_get_allow (blocklist);
              allow_regex = bz_blocklist_get_allow_regex (blocklist);
              block       = bz_blocklist_get_block (blocklist);
              block_regex = bz_blocklist_get_block_regex (blocklist);

              if (allow == NULL &&
                  allow_regex == NULL &&
                  block == NULL &&
                  block_regex == NULL)
                {
                  g_warning ("Blocklist file has an empty blocklist, ignoring");
                  continue;
                }

              conditions = bz_blocklist_get_conditions (blocklist);
              if (conditions != NULL)
                {
                  guint    n_conditions = 0;
                  gboolean ignore       = FALSE;

                  n_conditions = g_list_model_get_n_items (conditions);
                  for (guint k = 0; k < n_conditions; k++)
                    {
                      gboolean condition_result                        = FALSE;
                      g_autoptr (BzBlocklistCondition) condition       = NULL;
                      BzBlocklistConditionMatchEnvvar    *match_envvar = NULL;
                      BzBlocklistConditionMatchLocale    *match_locale = NULL;
                      BzBlocklistConditionPostProcessKind postprocess  = BZ_BLOCKLIST_CONDITION_POST_PROCESS_KIND_IDENTITY;

                      condition    = g_list_model_get_item (conditions, k);
                      match_envvar = bz_blocklist_condition_get_match_envvar (condition);
                      match_locale = bz_blocklist_condition_get_match_locale (condition);
                      postprocess  = bz_blocklist_condition_get_post_process (condition);

                      if (match_envvar == NULL &&
                          match_locale == NULL)
                        {
                          g_warning ("Blocklist file has an empty condition");
                          continue;
                        }

                      if (!condition_result &&
                          match_envvar != NULL)
                        {
                          const char *var   = NULL;
                          const char *regex = NULL;

                          var   = bz_blocklist_condition_match_envvar_get_var (match_envvar);
                          regex = bz_blocklist_condition_match_envvar_get_regex (match_envvar);

                          if (var != NULL &&
                              regex != NULL)
                            {
                              g_autoptr (GRegex) compiled = NULL;
                              const char *value           = NULL;

                              compiled = g_regex_new (
                                  regex,
                                  G_REGEX_ANCHORED,
                                  G_REGEX_MATCH_ANCHORED,
                                  &local_error);
                              if (compiled == NULL)
                                {
                                  g_warning ("Blocklist condition contains invalid regex: %s",
                                             local_error->message);
                                  g_clear_error (&local_error);
                                  continue;
                                }

                              value = g_getenv (var);
                              if (value != NULL &&
                                  g_regex_match (
                                      compiled, value,
                                      G_REGEX_MATCH_ANCHORED, NULL))
                                condition_result = TRUE;
                              if (postprocess == BZ_BLOCKLIST_CONDITION_POST_PROCESS_KIND_INVERT)
                                condition_result = !condition_result;
                            }
                          else
                            g_warning ("Blocklist file has a envvar condition "
                                       "missing a var and/or a regex pattern");
                        }

                      if (!condition_result &&
                          match_locale != NULL)
                        {
                          const char *regex = NULL;

                          regex = bz_blocklist_condition_match_locale_get_regex (match_locale);

                          if (regex != NULL)
                            {
                              g_autoptr (GRegex) compiled = NULL;
                              const char *const *locales  = NULL;

                              compiled = g_regex_new (
                                  regex,
                                  G_REGEX_ANCHORED,
                                  G_REGEX_MATCH_ANCHORED,
                                  &local_error);
                              if (compiled == NULL)
                                {
                                  g_warning ("Blocklist condition contains invalid regex: %s",
                                             local_error->message);
                                  g_clear_error (&local_error);
                                  continue;
                                }

                              locales = g_get_language_names ();
                              for (guint l = 0; locales[l] != NULL; l++)
                                {
                                  if (g_regex_match (
                                          compiled, locales[i],
                                          G_REGEX_MATCH_ANCHORED, NULL))
                                    condition_result = TRUE;
                                  if (condition_result)
                                    break;
                                }
                              if (postprocess == BZ_BLOCKLIST_CONDITION_POST_PROCESS_KIND_INVERT)
                                condition_result = !condition_result;
                            }
                          else
                            g_warning ("Blocklist file has a match-locale "
                                       "condition missing a regex pattern");
                        }

                      if (!condition_result)
                        {
                          ignore = TRUE;
                          break;
                        }
                    }

                  if (ignore)
                    continue;
                }

              data           = blocklist_regex_data_new ();
              data->priority = bz_blocklist_get_priority (blocklist);

#define BUILD_REGEX(_name, _builder)                                                             \
  if (_name != NULL)                                                                             \
    {                                                                                            \
      guint _n_strings = 0;                                                                      \
                                                                                                 \
      _n_strings = g_list_model_get_n_items (_name);                                             \
      for (guint _i = 0; _i < _n_strings; _i++)                                                  \
        {                                                                                        \
          g_autoptr (GtkStringObject) _object = NULL;                                            \
          const char *_string                 = NULL;                                            \
          g_autoptr (GRegex) _regex           = NULL;                                            \
                                                                                                 \
          _object = g_list_model_get_item (_name, _i);                                           \
          _string = gtk_string_object_get_string (_object);                                      \
          _regex  = g_regex_new (_string, G_REGEX_DEFAULT, G_REGEX_MATCH_DEFAULT, &local_error); \
                                                                                                 \
          if (_regex != NULL)                                                                    \
            g_strv_builder_add (_builder, _string);                                              \
          else                                                                                   \
            {                                                                                    \
              g_warning ("Blocklist file has an invalid "                                        \
                         "regular expression '%s': %s",                                          \
                         _string, local_error->message);                                         \
              g_clear_error (&local_error);                                                      \
            }                                                                                    \
        }                                                                                        \
    }

#define BUILD_REGEX_ESCAPED(_name, _builder)                                   \
  if (_name != NULL)                                                           \
    {                                                                          \
      guint _n_strings = 0;                                                    \
                                                                               \
      _n_strings = g_list_model_get_n_items (_name);                           \
      for (guint _i = 0; _i < _n_strings; _i++)                                \
        {                                                                      \
          g_autoptr (GtkStringObject) _object = NULL;                          \
          const char *_string                 = NULL;                          \
                                                                               \
          _object = g_list_model_get_item (_name, _i);                         \
          _string = gtk_string_object_get_string (_object);                    \
                                                                               \
          g_strv_builder_take (_builder, g_regex_escape_string (_string, -1)); \
        }                                                                      \
    }

#define GATHER(name)                                                    \
  if (name != NULL ||                                                   \
      name##_regex != NULL)                                             \
    {                                                                   \
      g_autoptr (GStrvBuilder) _builder = NULL;                         \
      g_auto (GStrv) _patterns          = NULL;                         \
                                                                        \
      _builder = g_strv_builder_new ();                                 \
                                                                        \
      BUILD_REGEX_ESCAPED (name, _builder)                              \
      BUILD_REGEX (name##_regex, _builder)                              \
                                                                        \
      _patterns = g_strv_builder_end (_builder);                        \
      if (_patterns != NULL)                                            \
        {                                                               \
          g_autofree char *_joined       = NULL;                        \
          g_autofree char *_regex_string = NULL;                        \
                                                                        \
          _joined       = g_strjoinv ("|", _patterns);                  \
          _regex_string = g_strdup_printf ("^(%s)$", _joined);          \
          data->name    = g_regex_new (_regex_string, G_REGEX_OPTIMIZE, \
                                       G_REGEX_MATCH_DEFAULT, NULL);    \
        }                                                               \
    }

              GATHER (allow);
              GATHER (block);

#undef GATHER
#undef BUILD_REGEX_ESCAPED
#undef BUILD_REGEX

              if (data->allow != NULL || data->block != NULL)
                g_ptr_array_add (regex_datas, g_steal_pointer (&data));
            }
        }

      g_ptr_array_insert (self->blocklist_regexes,
                          position + i,
                          g_steal_pointer (&regex_datas));
    }

  gtk_filter_changed (GTK_FILTER (self->group_filter), GTK_FILTER_CHANGE_DIFFERENT);
  gtk_filter_changed (GTK_FILTER (self->appid_filter), GTK_FILTER_CHANGE_DIFFERENT);
}

static void
txt_blocklists_changed (BzApplication *self,
                        guint          position,
                        guint          removed,
                        guint          added,
                        GListModel    *model)
{
  if (removed > 0)
    g_ptr_array_remove_range (self->txt_blocked_id_sets, position, removed);

  for (guint i = 0; i < added; i++)
    {
      g_autoptr (BzHashTableObject) obj = NULL;
      GHashTable *set                   = NULL;

      obj = g_list_model_get_item (model, position + i);
      set = bz_hash_table_object_get_hash_table (obj);

      g_ptr_array_insert (self->txt_blocked_id_sets,
                          position + i,
                          g_hash_table_ref (set));
    }

  gtk_filter_changed (GTK_FILTER (self->group_filter), GTK_FILTER_CHANGE_DIFFERENT);
  gtk_filter_changed (GTK_FILTER (self->appid_filter), GTK_FILTER_CHANGE_DIFFERENT);
}

static void
init_service_struct (BzApplication *self,
                     GtkStringList *blocklists,
                     GtkStringList *txt_blocklists,
                     GtkStringList *curated_configs)
{
  g_autoptr (GError) local_error                       = NULL;
  g_autoptr (GBytes) internal_config_bytes             = NULL;
  g_autoptr (BzYamlParser) internal_config_parser      = NULL;
  g_autoptr (GHashTable) internal_config_parse_results = NULL;
  const char *app_id                                   = NULL;
#ifdef HARDCODED_MAIN_CONFIG
  g_autoptr (GFile) config_file   = NULL;
  g_autoptr (GBytes) config_bytes = NULL;
#endif
  GtkCustomFilter *filter            = NULL;
  GNetworkMonitor *network           = NULL;
  g_autoptr (BzAuthState) auth_state = NULL;

  g_type_ensure (BZ_TYPE_INTERNAL_CONFIG);
  internal_config_bytes = g_resources_lookup_data (
      "/io/github/kolunmi/Bazaar/internal-config.yaml",
      G_RESOURCE_LOOKUP_FLAGS_NONE,
      NULL);
  g_assert (internal_config_bytes != NULL);
  internal_config_parser = bz_yaml_parser_new_for_resource_schema (
      "/io/github/kolunmi/Bazaar/internal-config-schema.xml");
  g_assert (internal_config_parser != NULL);
  internal_config_parse_results = bz_parser_process_bytes (
      BZ_PARSER (internal_config_parser), internal_config_bytes, &local_error);
  if (internal_config_parse_results == NULL)
    g_critical ("FATAL: unable to parse internal config resource: %s",
                local_error->message);
  g_assert (internal_config_parse_results != NULL);
  self->internal_config = g_value_dup_object (g_hash_table_lookup (internal_config_parse_results, "/"));

  g_type_ensure (BZ_TYPE_MAIN_CONFIG);
#ifdef HARDCODED_MAIN_CONFIG
  config_file  = g_file_new_for_path (HARDCODED_MAIN_CONFIG);
  config_bytes = g_file_load_bytes (config_file, NULL, NULL, &local_error);
  if (config_bytes != NULL)
    {
      g_autoptr (BzYamlParser) parser      = NULL;
      g_autoptr (GHashTable) parse_results = NULL;

      parser = bz_yaml_parser_new_for_resource_schema (
          "/io/github/kolunmi/Bazaar/main-config-schema.xml");

      parse_results = bz_parser_process_bytes (
          BZ_PARSER (parser), config_bytes, &local_error);
      if (parse_results != NULL)
        {
          GListModel *override_eol_markings = NULL;

          self->config = g_value_dup_object (g_hash_table_lookup (parse_results, "/"));

          override_eol_markings = bz_main_config_get_override_eol_markings (self->config);
          if (override_eol_markings != NULL)
            {
              guint n_appids = 0;

              self->ignore_eol_set = g_hash_table_new_full (
                  g_str_hash, g_str_equal, g_free, g_free);

              n_appids = g_list_model_get_n_items (override_eol_markings);
              for (guint i = 0; i < n_appids; i++)
                {
                  g_autoptr (GtkStringObject) string = NULL;
                  const char *value                  = NULL;

                  string = g_list_model_get_item (override_eol_markings, i);
                  value  = gtk_string_object_get_string (string);
                  g_hash_table_replace (self->ignore_eol_set, g_strdup (value), NULL);
                }
            }
        }
      else
        {
          g_warning ("Could not load main config at %s: %s",
                     HARDCODED_MAIN_CONFIG, local_error->message);
          g_clear_error (&local_error);
        }
    }
  g_clear_error (&local_error);
#endif

  self->init_timer          = g_timer_new ();
  self->ready_to_open_files = dex_promise_new ();

  if (self->config != NULL &&
      bz_main_config_get_yaml_blocklist_paths (self->config) != NULL)
    {
      GListModel *paths   = NULL;
      guint       n_paths = 0;

      paths   = bz_main_config_get_yaml_blocklist_paths (self->config);
      n_paths = g_list_model_get_n_items (paths);
      for (guint i = 0; i < n_paths; i++)
        {
          g_autoptr (GtkStringObject) string = NULL;

          string = g_list_model_get_item (paths, i);
          gtk_string_list_append (blocklists, gtk_string_object_get_string (string));
        }
    }
  self->blocklists          = g_object_ref (blocklists);
  self->blocklists_to_files = gtk_map_list_model_new (
      NULL, (GtkMapListModelMapFunc) map_strings_to_files, NULL, NULL);
  gtk_map_list_model_set_model (
      self->blocklists_to_files,
      G_LIST_MODEL (self->blocklists));

  if (self->config != NULL &&
      bz_main_config_get_txt_blocklist_paths (self->config) != NULL)
    {
      GListModel *paths   = NULL;
      guint       n_paths = 0;

      paths   = bz_main_config_get_txt_blocklist_paths (self->config);
      n_paths = g_list_model_get_n_items (paths);
      for (guint i = 0; i < n_paths; i++)
        {
          g_autoptr (GtkStringObject) string = NULL;

          string = g_list_model_get_item (paths, i);
          gtk_string_list_append (txt_blocklists, gtk_string_object_get_string (string));
        }
    }
  self->txt_blocklists          = g_object_ref (txt_blocklists);
  self->txt_blocklists_to_files = gtk_map_list_model_new (
      NULL, (GtkMapListModelMapFunc) map_strings_to_files, NULL, NULL);
  gtk_map_list_model_set_model (
      self->txt_blocklists_to_files,
      G_LIST_MODEL (self->txt_blocklists));

  if (self->config != NULL &&
      bz_main_config_get_curated_config_paths (self->config) != NULL)
    {
      GListModel *paths   = NULL;
      guint       n_paths = 0;

      paths   = bz_main_config_get_curated_config_paths (self->config);
      n_paths = g_list_model_get_n_items (paths);
      for (guint i = 0; i < n_paths; i++)
        {
          g_autoptr (GtkStringObject) string = NULL;

          string = g_list_model_get_item (paths, i);
          gtk_string_list_append (curated_configs, gtk_string_object_get_string (string));
        }
    }
  self->curated_configs          = g_object_ref (curated_configs);
  self->curated_configs_to_files = gtk_map_list_model_new (
      NULL, (GtkMapListModelMapFunc) map_strings_to_files, NULL, NULL);
  gtk_map_list_model_set_model (
      self->curated_configs_to_files,
      G_LIST_MODEL (self->curated_configs));

  self->search_biases         = gtk_flatten_list_model_new (NULL);
  self->search_biases_backing = g_list_store_new (G_TYPE_LIST_MODEL);
  {
    GListModel *main_config_search_biases     = NULL;
    GListModel *internal_config_search_biases = NULL;

    if (self->config != NULL)
      main_config_search_biases = bz_main_config_get_search_biases (self->config);

    internal_config_search_biases = bz_internal_config_get_search_biases (self->internal_config);

    /* Main config biases take precedence over the hardcoded ones */
    if (main_config_search_biases != NULL)
      g_list_store_append (self->search_biases_backing, main_config_search_biases);
    if (internal_config_search_biases != NULL)
      g_list_store_append (self->search_biases_backing, internal_config_search_biases);
  }
  gtk_flatten_list_model_set_model (
      self->search_biases,
      G_LIST_MODEL (self->search_biases_backing));

  g_type_ensure (BZ_TYPE_ROOT_BLOCKLIST);
  g_type_ensure (BZ_TYPE_BLOCKLIST);
  g_type_ensure (BZ_TYPE_BLOCKLIST_CONDITION);
  self->blocklist_parser = bz_yaml_parser_new_for_resource_schema (
      "/io/github/kolunmi/Bazaar/blocklist-schema.xml");

  self->txt_blocklist_parser = bz_newline_parser_new (
      TRUE, MAX_IDS_PER_BLOCKLIST);

  g_type_ensure (BZ_TYPE_ROOT_CURATED_CONFIG);
  g_type_ensure (BZ_TYPE_CURATED_ROW);
  g_type_ensure (BZ_TYPE_CURATED_SECTION);
  self->curated_parser = bz_yaml_parser_new_for_resource_schema (
      "/io/github/kolunmi/Bazaar/curated-config-schema.xml");

  self->cache = bz_entry_cache_manager_new ();

  self->state = bz_state_info_new ();
  bz_state_info_set_busy (self->state, TRUE);
  bz_state_info_set_donation_prompt_dismissed (self->state, TRUE);
  bz_state_info_set_parental_age_rating (self->state, -1);

  state_info_set_icon_themes (self->state);

  {
    g_autoptr (GError) bus_error        = NULL;
    g_autoptr (GDBusConnection) sys_bus = NULL;

    sys_bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &bus_error);
    if (sys_bus != NULL)
      self->malcontent = bz_malcontent_service_new (sys_bus, self->state);
    else
      g_warning ("Failed to connect to system bus for malcontent: %s", bus_error->message);

    if (self->malcontent != NULL)
      g_signal_connect_swapped (
          self->state,
          "notify::parental-age-rating",
          G_CALLBACK (show_hide_app_setting_changed),
          self);
  }

  g_signal_connect_swapped (
      self->state,
      "notify::disable-blocklists",
      G_CALLBACK (disable_blocklists_changed),
      self);

  auth_state = bz_auth_state_new ();
  bz_state_info_set_auth_state (self->state, auth_state);

  g_object_bind_property (
      auth_state, "authenticated",
      g_action_map_lookup_action (G_ACTION_MAP (self), "flathub-login"), "enabled",
      G_BINDING_SYNC_CREATE | G_BINDING_INVERT_BOOLEAN);

  network = g_network_monitor_get_default ();
  if (network != NULL)
    {
      GNetworkConnectivity connectivity = 0;
      gboolean             metered      = FALSE;

      connectivity = g_network_monitor_get_connectivity (network);
      bz_state_info_set_have_connection (self->state, connectivity == G_NETWORK_CONNECTIVITY_FULL);

      metered = g_network_monitor_get_network_metered (network);
      bz_state_info_set_metered_connection (self->state, metered);

      g_signal_connect_swapped (network, "notify", G_CALLBACK (network_status_changed), self);
    }
  else
    g_warning ("Unable to detect networking device! Continuing anyway...");

  app_id = g_application_get_application_id (G_APPLICATION (self));
  g_assert (app_id != NULL);
  g_debug ("Constructing gsettings for %s ...", app_id);
  self->settings = g_settings_new (app_id);

  if (g_settings_get_boolean (self->settings, "force-adwaita-icons"))
    {
      GtkSettings *gtk_settings = NULL;

      gtk_settings = gtk_settings_get_default ();
      g_object_set (
          gtk_settings,
          "gtk-icon-theme-name", "Adwaita",
          NULL);
    }

  bz_state_info_set_hide_eol (
      self->state,
      g_settings_get_boolean (self->settings, "hide-eol"));
  g_signal_connect_swapped (
      self->settings,
      "changed::hide-eol",
      G_CALLBACK (show_hide_app_setting_changed),
      self);

  bz_state_info_set_show_only_foss (
      self->state,
      g_settings_get_boolean (self->settings, "show-only-foss"));
  g_signal_connect_swapped (
      self->settings,
      "changed::show-only-foss",
      G_CALLBACK (show_hide_app_setting_changed),
      self);

  bz_state_info_set_show_only_flathub (
      self->state,
      g_settings_get_boolean (self->settings, "show-only-flathub"));
  g_signal_connect_swapped (
      self->settings,
      "changed::show-only-flathub",
      G_CALLBACK (show_hide_app_setting_changed),
      self);

  bz_state_info_set_show_only_verified (
      self->state,
      g_settings_get_boolean (self->settings, "show-only-verified"));
  g_signal_connect_swapped (
      self->settings,
      "changed::show-only-verified",
      G_CALLBACK (show_hide_app_setting_changed),
      self);

  self->blocklist_regexes = g_ptr_array_new_with_free_func (
      (GDestroyNotify) g_ptr_array_unref);
  self->blocklists_provider = bz_content_provider_new ();
  bz_content_provider_set_parser (self->blocklists_provider, BZ_PARSER (self->blocklist_parser));
  bz_content_provider_set_input_files (
      self->blocklists_provider, G_LIST_MODEL (self->blocklists_to_files));
  g_signal_connect_swapped (self->blocklists_provider, "items-changed", G_CALLBACK (blocklists_changed), self);

  self->txt_blocked_id_sets = g_ptr_array_new_with_free_func (
      (GDestroyNotify) g_hash_table_unref);
  self->txt_blocklists_provider = bz_content_provider_new ();
  bz_content_provider_set_parser (self->txt_blocklists_provider, BZ_PARSER (self->txt_blocklist_parser));
  bz_content_provider_set_input_files (
      self->txt_blocklists_provider, G_LIST_MODEL (self->txt_blocklists_to_files));
  g_signal_connect_swapped (self->txt_blocklists_provider, "items-changed", G_CALLBACK (txt_blocklists_changed), self);

  self->groups         = g_list_store_new (BZ_TYPE_ENTRY_GROUP);
  self->installed_apps = g_list_store_new (BZ_TYPE_ENTRY_GROUP);
  self->ids_to_groups  = g_hash_table_new_full (
      g_str_hash, g_str_equal, g_free, g_object_unref);
  self->eol_runtimes = g_hash_table_new_full (
      g_str_hash, g_str_equal, g_free, g_free);
  self->sys_name_to_addons = g_hash_table_new_full (
      g_str_hash, g_str_equal, g_free, (GDestroyNotify) g_ptr_array_unref);
  self->usr_name_to_addons = g_hash_table_new_full (
      g_str_hash, g_str_equal, g_free, (GDestroyNotify) g_ptr_array_unref);
  self->sys_ref_to_addon_group_ids = g_hash_table_new_full (
      g_str_hash, g_str_equal, g_free, (GDestroyNotify) g_ptr_array_unref);
  self->usr_ref_to_addon_group_ids = g_hash_table_new_full (
      g_str_hash, g_str_equal, g_free, (GDestroyNotify) g_ptr_array_unref);

  self->entry_factory = bz_application_map_factory_new (
      (GtkMapListModelMapFunc) map_ids_to_entries,
      self, NULL, NULL, NULL);

  filter = gtk_custom_filter_new (
      (GtkCustomFilterFunc) filter_application_ids, self, NULL);
  self->appid_filter        = g_object_ref_sink (g_steal_pointer (&filter));
  self->application_factory = bz_application_map_factory_new (
      (GtkMapListModelMapFunc) map_generic_ids_to_groups,
      self, NULL, NULL, GTK_FILTER (self->appid_filter));

  filter = gtk_custom_filter_new (
      (GtkCustomFilterFunc) filter_entry_groups, self, NULL);
  self->group_filter       = g_object_ref_sink (g_steal_pointer (&filter));
  self->group_filter_model = gtk_filter_list_model_new (
      g_object_ref (G_LIST_MODEL (self->groups)),
      g_object_ref (GTK_FILTER (self->group_filter)));

  self->search_engine = bz_search_engine_new ();
  bz_search_engine_set_model (self->search_engine, G_LIST_MODEL (self->group_filter_model));
  bz_search_engine_set_biases (self->search_engine, G_LIST_MODEL (self->search_biases));
  bz_gnome_shell_search_provider_set_engine (self->gs_search, self->search_engine);

  self->curated_provider = bz_content_provider_new ();
  bz_content_provider_set_input_files (
      self->curated_provider, G_LIST_MODEL (self->curated_configs_to_files));
  bz_content_provider_set_parser (self->curated_provider, BZ_PARSER (self->curated_parser));

  self->transactions = bz_transaction_manager_new ();
  bz_transaction_manager_set_config (self->transactions, self->config);

  bz_state_info_set_all_entry_groups (self->state, G_LIST_MODEL (self->groups));
  bz_state_info_set_all_installed_entry_groups (self->state, G_LIST_MODEL (self->installed_apps));
  bz_state_info_set_filtered_entry_groups (self->state, G_LIST_MODEL (self->group_filter_model));
  bz_state_info_set_application_factory (self->state, self->application_factory);
  bz_state_info_set_blocklists (self->state, G_LIST_MODEL (self->blocklists));
  bz_state_info_set_blocklists_provider (self->state, self->blocklists_provider);
  bz_state_info_set_curated_configs (self->state, G_LIST_MODEL (self->curated_configs));
  bz_state_info_set_curated_provider (self->state, self->curated_provider);
  bz_state_info_set_entry_factory (self->state, self->entry_factory);
  bz_state_info_set_main_config (self->state, self->config);
  bz_state_info_set_search_engine (self->state, self->search_engine);
  bz_state_info_set_settings (self->state, self->settings);
  bz_state_info_set_transaction_manager (self->state, self->transactions);
  bz_state_info_set_txt_blocklists (self->state, G_LIST_MODEL (self->txt_blocklists));
  bz_state_info_set_txt_blocklists_provider (self->state, self->txt_blocklists_provider);
  bz_state_info_set_cache_manager (self->state, self->cache);

  g_object_bind_property (
      self->state, "allow-manual-sync",
      g_action_map_lookup_action (G_ACTION_MAP (self), "sync-remotes"), "enabled",
      G_BINDING_SYNC_CREATE);

  gtk_style_context_add_provider_for_display (
      gdk_display_get_default (),
      bz_get_pride_style_provider (),
      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

static GtkWindow *
new_window (BzApplication *self)
{
  BzWindow *window                  = NULL;
  g_autoptr (GtkWidget) main_window = NULL;
  int width                         = 0;
  int height                        = 0;

  window = bz_window_new (self->state);
  gtk_application_add_window (GTK_APPLICATION (self), GTK_WINDOW (window));

  main_window = g_weak_ref_get (&self->main_window);
  if (main_window != NULL)
    {
      width  = gtk_widget_get_width (main_window);
      height = gtk_widget_get_height (main_window);

      g_settings_set (self->settings, "window-dimensions", "(ii)", width, height);
    }
  else
    {
      g_settings_get (self->settings, "window-dimensions", "(ii)", &width, &height);

      g_signal_connect_object (
          window, "close-request",
          G_CALLBACK (window_close_request),
          self, G_CONNECT_SWAPPED);
      g_weak_ref_init (&self->main_window, window);
    }

  gtk_window_set_default_size (GTK_WINDOW (window), width, height);
  gtk_window_present (GTK_WINDOW (window));

  return GTK_WINDOW (window);
}

static void
open_appstream_take (BzApplication *self,
                     char          *appstream)
{
  const char *id                     = NULL;
  g_autoptr (OpenAppstreamData) data = NULL;

  g_info ("Loading appstream link %s...", appstream);

  if (g_str_has_prefix (appstream, "appstream://"))
    id = appstream + strlen ("appstream://");
  else
    id = appstream + strlen ("appstream:");

  data       = open_appstream_data_new ();
  data->self = bz_track_weak (self);
  data->id   = g_strdup (id);

  dex_future_disown (dex_scheduler_spawn (
      dex_scheduler_get_default (),
      bz_get_dex_stack_size (),
      (DexFiberFunc) open_appstream_fiber,
      open_appstream_data_ref (data),
      open_appstream_data_unref));
  g_free (appstream);
}

static void
open_flatpakref_take (BzApplication *self,
                      GFile         *file)
{
  g_autoptr (GError) local_error      = NULL;
  gboolean         result             = FALSE;
  g_autofree char *path               = NULL;
  g_autoptr (OpenFlatpakrefData) data = NULL;

  path = g_file_get_path (file);
  if (path != NULL)
    /* We must do this synchronously so we don't lose access to a portal file */
    {
      g_autofree char *basename   = NULL;
      g_autofree char *module_dir = NULL;
      g_autofree char *staging    = NULL;
      g_autofree char *dest       = NULL;
      g_autoptr (GFile) copied    = NULL;

      basename   = g_file_get_basename (file);
      module_dir = bz_dup_module_dir ();
      staging    = g_build_filename (module_dir, "bundle-staging", NULL);
      g_mkdir_with_parents (staging, 0755);
      dest   = g_build_filename (staging, basename, NULL);
      copied = g_file_new_for_path (dest);

      result = g_file_copy (
          file, copied,
          G_FILE_COPY_OVERWRITE | G_FILE_COPY_NOFOLLOW_SYMLINKS,
          NULL, NULL, NULL,
          &local_error);
      if (result)
        {
          g_clear_object (&file);
          file = g_steal_pointer (&copied);
        }
      else
        {
          g_warning ("Failed to copy bundle to %s : %s",
                     dest, local_error->message);
          g_clear_error (&local_error);
          g_clear_object (&copied);
        }
    }

  data       = open_flatpakref_data_new ();
  data->self = bz_track_weak (self);
  data->file = g_steal_pointer (&file);

  dex_future_disown (dex_scheduler_spawn (
      dex_scheduler_get_default (),
      bz_get_dex_stack_size (),
      (DexFiberFunc) open_flatpakref_fiber,
      open_flatpakref_data_ref (data),
      open_flatpakref_data_unref));
}

static void
command_line_open_location (BzApplication           *self,
                            GApplicationCommandLine *cmdline,
                            const char              *location)
{
  if (g_uri_is_valid (location, G_URI_FLAGS_NONE, NULL))
    {
      if (g_str_has_prefix (location, "appstream:"))
        open_appstream_take (self, g_strdup (location));
      else
        open_flatpakref_take (self, g_file_new_for_uri (location));
    }
  else if (g_path_is_absolute (location))
    open_flatpakref_take (self, g_file_new_for_path (location));
  else
    {
      const char *cwd = NULL;

      cwd = g_application_command_line_get_cwd (cmdline);
      if (cwd != NULL)
        open_flatpakref_take (self, g_file_new_build_filename (cwd, location, NULL));
      else
        open_flatpakref_take (self, g_file_new_for_path (location));
    }
}

static void
open_generic_id (BzApplication *self,
                 const char    *generic_id)
{
  BzEntryGroup *group  = NULL;
  GtkWindow    *window = NULL;

  group  = g_hash_table_lookup (self->ids_to_groups, generic_id);
  window = gtk_application_get_active_window (GTK_APPLICATION (self));
  if (window == NULL)
    window = new_window (self);

  if (group != NULL)
    gtk_widget_activate_action (GTK_WIDGET (window), "window.show-group", "s", generic_id);
  else
    {
      g_autofree char *message = NULL;

      message = g_strdup_printf ("ID '%s' was not found", generic_id);
      bz_show_error_for_widget (GTK_WIDGET (window), _ ("Could not find app"), message);
    }
}

static DexFuture *
preview_metainfo_then (DexFuture *future,
                       GWeakRef  *wr)
{
  g_autoptr (BzApplication) self = NULL;
  g_autoptr (GError) local_error = NULL;
  const GValue         *value    = NULL;
  BzMetainfoPickResult *result   = NULL;
  g_autoptr (BzEntry) entry      = NULL;
  GtkWindow *window              = NULL;

  bz_weak_get_or_return_reject (self, wr);

  value = dex_future_get_value (future, &local_error);
  if (value == NULL)
    return dex_future_new_true ();

  result = g_value_get_boxed (value);
  window = gtk_application_get_active_window (GTK_APPLICATION (self));
  if (window == NULL)
    window = new_window (self);

  entry = bz_appstream_parser_entry_from_metainfo (
      result->metainfo_file,
      result->icon_file,
      &local_error);
  if (entry == NULL)
    {
      bz_show_error_for_widget (GTK_WIDGET (window),
                                _ ("Failed to load metainfo"),
                                local_error->message);
      return dex_future_new_true ();
    }

  bz_window_show_entry (BZ_WINDOW (window), entry);

  return dex_future_new_true ();
}

static gpointer
map_strings_to_files (GtkStringObject *string,
                      gpointer         data)
{
  const char *path   = NULL;
  GFile      *result = NULL;

  path   = gtk_string_object_get_string (string);
  result = g_file_new_for_path (path);

  g_object_unref (string);
  return result;
}

static gpointer
map_generic_ids_to_groups (GtkStringObject *string,
                           BzApplication   *self)
{
  BzEntryGroup *group = NULL;

  group = g_hash_table_lookup (
      self->ids_to_groups,
      gtk_string_object_get_string (string));

  g_object_unref (string);
  return bz_object_maybe_ref (group);
}

static gpointer
map_ids_to_entries (GtkStringObject *string,
                    BzApplication   *self)
{
  g_autoptr (GError) local_error = NULL;
  const char *id                 = NULL;
  g_autoptr (DexFuture) future   = NULL;
  g_autoptr (BzResult) result    = NULL;

  id     = gtk_string_object_get_string (string);
  future = bz_entry_cache_manager_get (self->cache, id);
  result = bz_result_new (future);

  g_object_unref (string);
  return g_steal_pointer (&result);
}

static gboolean
filter_application_ids (GtkStringObject *string,
                        BzApplication   *self)
{
  BzEntryGroup *group = NULL;

  group = g_hash_table_lookup (
      self->ids_to_groups,
      gtk_string_object_get_string (string));
  if (group != NULL)
    return validate_group_for_ui (self, group);
  else
    return FALSE;
}

static gboolean
filter_entry_groups (BzEntryGroup  *group,
                     BzApplication *self)
{
  return validate_group_for_ui (self, group);
}

static gint
cmp_group (BzEntryGroup *a,
           BzEntryGroup *b,
           gpointer      user_data)
{
  const char *title_a = NULL;
  const char *title_b = NULL;

  title_a = bz_entry_group_get_title (a);
  title_b = bz_entry_group_get_title (b);

  if (title_a == NULL)
    return 1;
  if (title_b == NULL)
    return -1;

  return strcasecmp (title_a, title_b);
}

static gint
cmp_entry (BzEntry *a,
           BzEntry *b,
           gpointer user_data)
{
  gboolean a_is_runtime = FALSE;
  gboolean b_is_runtime = FALSE;
  gboolean a_is_addon   = FALSE;
  gboolean b_is_addon   = FALSE;

  a_is_runtime = bz_entry_is_of_kinds (a, BZ_ENTRY_KIND_RUNTIME);
  b_is_runtime = bz_entry_is_of_kinds (b, BZ_ENTRY_KIND_RUNTIME);
  if (a_is_runtime && !b_is_runtime)
    return -1;
  if (!a_is_runtime && b_is_runtime)
    return 1;

  a_is_addon = bz_entry_is_of_kinds (a, BZ_ENTRY_KIND_ADDON);
  b_is_addon = bz_entry_is_of_kinds (b, BZ_ENTRY_KIND_ADDON);
  if (a_is_addon && !b_is_addon)
    return -1;
  if (!a_is_addon && b_is_addon)
    return 1;

  return 0;
}

static gboolean
validate_group_for_ui (BzApplication *self,
                       BzEntryGroup  *group)
{
  const char *id               = NULL;
  int         allowed_priority = G_MAXINT;
  int         blocked_priority = G_MAXINT;

  if (bz_state_info_get_hide_eol (self->state) &&
      bz_entry_group_get_eol (group) != NULL)
    return FALSE;
  if (bz_state_info_get_show_only_foss (self->state) &&
      !bz_entry_group_get_is_floss (group))
    return FALSE;
  if (bz_state_info_get_show_only_flathub (self->state) &&
      !bz_entry_group_get_is_flathub (group))
    return FALSE;
  if (bz_state_info_get_show_only_verified (self->state) &&
      !bz_entry_group_get_is_verified (group))
    return FALSE;

  if (self->malcontent != NULL)
    {
      int parental_age = -1;
      int app_age      = 0;

      parental_age = bz_state_info_get_parental_age_rating (self->state);
      app_age      = bz_entry_group_get_content_age_rating (group);

      if (app_age > parental_age && parental_age != -1)
        return FALSE;
    }

  if (bz_state_info_get_disable_blocklists (self->state))
    return TRUE;

  id = bz_entry_group_get_id (group);
  for (guint i = 0; i < self->txt_blocked_id_sets->len; i++)
    {
      GHashTable *set = NULL;

      set = g_ptr_array_index (self->txt_blocked_id_sets, i);
      if (g_hash_table_contains (set, id))
        return FALSE;
    }

  for (guint i = 0; i < self->blocklist_regexes->len; i++)
    {
      GPtrArray *regex_datas = NULL;

      regex_datas = g_ptr_array_index (self->blocklist_regexes, i);
      for (guint j = 0; j < regex_datas->len; j++)
        {
          BlocklistRegexData *data = NULL;

          data = g_ptr_array_index (regex_datas, j);

          if (data->allow != NULL &&
              data->priority < allowed_priority &&
              g_regex_match (data->allow, id, G_REGEX_MATCH_DEFAULT, NULL))
            allowed_priority = data->priority;
          if (data->block != NULL &&
              data->priority < blocked_priority &&
              g_regex_match (data->block, id, G_REGEX_MATCH_DEFAULT, NULL))
            blocked_priority = data->priority;
        }
    }
  return allowed_priority <= blocked_priority;
}

static DexFuture *
make_sync_future (BzApplication *self)
{
  g_autoptr (GError) local_error         = NULL;
  g_autoptr (GSubprocess) refresh_worker = NULL;
  g_autoptr (DexFuture) backend_future   = NULL;
  g_autoptr (DexFuture) flathub_future   = NULL;
  g_autoptr (DexFuture) ret_future       = NULL;

  bz_state_info_set_allow_manual_sync (self->state, FALSE);

  bz_state_info_set_syncing (self->state, TRUE);
  finish_with_background_task_label (self);

  refresh_worker = g_subprocess_new (
      G_SUBPROCESS_FLAGS_NONE,
      &local_error,
      REFRESH_WORKER_BIN_NAME,
      NULL);
  if (refresh_worker == NULL)
    g_critical ("FATAL!!! The refresh worker could not be spawned: %s",
                local_error->message);
  g_assert (refresh_worker != NULL);

  backend_future = dex_subprocess_wait_check (refresh_worker);
  backend_future = dex_future_finally (
      backend_future,
      (DexFutureCallback) backend_sync_finally,
      bz_track_weak (self), bz_weak_release);

  g_clear_object (&self->tmp_flathub);
  self->tmp_flathub = bz_flathub_state_new ();
  flathub_future    = bz_flathub_state_update_to_today (self->tmp_flathub);
  flathub_future    = dex_future_finally (
      flathub_future,
      (DexFutureCallback) flathub_update_finally,
      bz_track_weak (self), bz_weak_release);

  ret_future = dex_future_all (
      dex_ref (backend_future),
      dex_ref (flathub_future),
      NULL);
  ret_future = dex_future_finally (
      ret_future,
      (DexFutureCallback) sync_finally,
      bz_track_weak (self), bz_weak_release);
  return g_steal_pointer (&ret_future);
}

static void
finish_with_background_task_label (BzApplication *self)
{
  if (self->n_entries_incoming > 0)
    bz_state_info_set_background_task_label_take_printf (
        self->state, _ ("Loading %d apps…"), self->n_entries_incoming);
  else if (bz_state_info_get_syncing (self->state))
    bz_state_info_set_background_task_label (self->state, _ ("Refreshing…"));
  else if (bz_state_info_get_busy (self->state))
    bz_state_info_set_background_task_label (self->state, _ ("Writing to cache…"));
  else
    bz_state_info_set_background_task_label (self->state, NULL);
}

static void
state_info_set_icon_themes (BzStateInfo *state)
{
  g_autoptr (GtkIconTheme) user_theme   = NULL;
  g_autoptr (GtkIconTheme) system_theme = NULL;
  g_autofree char *user_export_dir      = NULL;

  user_theme      = gtk_icon_theme_new ();
  user_export_dir = g_build_filename (g_get_home_dir (), ".local/share/flatpak/exports/share/icons", NULL);
  gtk_icon_theme_add_search_path (user_theme, user_export_dir);
  bz_state_info_set_user_icon_theme (state, user_theme);

  system_theme = gtk_icon_theme_new ();
  gtk_icon_theme_add_search_path (system_theme, "/var/lib/flatpak/exports/share/icons");
  bz_state_info_set_system_icon_theme (state, system_theme);
}
