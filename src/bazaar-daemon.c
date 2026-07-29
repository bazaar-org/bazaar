/* bazaar-daemon.c
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

#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <systemd/sd-bus.h>
#include <systemd/sd-event.h>
#include <unistd.h>

#include "search-index.h"

#define BUS_NAME            "io.github.kolunmi.Bazaar.SearchProvider"
#define OBJECT_PATH         "/io/github/kolunmi/Bazaar/SearchProvider"
#define DAEMON_IFACE        "io.github.kolunmi.Bazaar.Daemon"
#define MAX_SEARCH_RESULTS  25
#define ACTIVATE_TIMEOUT_US 2000000

#define UPDATE_CHECK_INTERVAL_USEC (60ULL * 60ULL * 1000000ULL) /* 1 hour */
#define IDLE_EXIT_TIMEOUT_USEC     (5ULL * 1000000ULL)

#define _cleanup_(x) __attribute__ ((cleanup (x)))

#define SD_BUS_CHECK(expr) \
  do                       \
    {                      \
      r = (expr);          \
      if (r < 0)           \
        goto out;          \
    }                      \
  while (0)

static sd_event        *event               = NULL;
static sd_bus          *bus                 = NULL;
static pid_t            child_pid           = -1;
static SearchIndex     *g_index             = NULL;
static char            *g_index_path        = NULL;
static sd_event_source *update_timer_source = NULL;
static sd_event_source *idle_timer_source   = NULL;

static void  log_msg (const char *fmt, ...) __attribute__ ((format (printf, 1, 2)));
static void *malloc_or_bail (size_t n_bytes);
static char *build_index_path (void);
static void  ensure_index_loaded (void);
static int   strv_count_local (char **strv);
static void  strv_free_local (char **strv);
static void  strv_freep (char ***strv);
static void  generic_freep (void *p);
static int   auto_update_enabled (void);
static void  cancel_idle_timer (void);
static void  arm_idle_timer (void);
static int   on_idle_timeout (sd_event_source *s, uint64_t usec, void *userdata);
static int   on_child_exit (sd_event_source *s, const siginfo_t *si, void *userdata);
static void  launch_app (char **extra_args, int n_extra_args);
static void  run_update_worker_once (void);
static int   on_update_worker_exit (sd_event_source *s, const siginfo_t *si, void *userdata);
static int   on_update_timer (sd_event_source *s, uint64_t usec, void *userdata);
static int   schedule_next_update_check (void);
static int   build_and_send_search_reply (sd_bus_message *call, char **terms);
static int   method_get_result_set (sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int   method_get_result_metas (sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int   method_activate_result (sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int   method_launch_search (sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int   method_relaunch (sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int   method_quit (sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int   on_signal (sd_event_source *s, const struct signalfd_siginfo *si, void *userdata);
static int   forward_to_running_daemon (sd_bus *forward_bus, int argc, char *argv[]);

static const sd_bus_vtable search_provider_vtable[] = {
  SD_BUS_VTABLE_START (0),
  SD_BUS_METHOD ("GetInitialResultSet", "as", "as", method_get_result_set, SD_BUS_VTABLE_UNPRIVILEGED),
  SD_BUS_METHOD ("GetSubsearchResultSet", "asas", "as", method_get_result_set, SD_BUS_VTABLE_UNPRIVILEGED),
  SD_BUS_METHOD ("GetResultMetas", "as", "aa{sv}", method_get_result_metas, SD_BUS_VTABLE_UNPRIVILEGED),
  SD_BUS_METHOD ("ActivateResult", "sasu", "", method_activate_result, SD_BUS_VTABLE_UNPRIVILEGED),
  SD_BUS_METHOD ("LaunchSearch", "asu", "", method_launch_search, SD_BUS_VTABLE_UNPRIVILEGED),
  SD_BUS_VTABLE_END
};

static const sd_bus_vtable daemon_vtable[] = {
  SD_BUS_VTABLE_START (0),
  SD_BUS_METHOD ("Relaunch", "as", "", method_relaunch, SD_BUS_VTABLE_UNPRIVILEGED),
  SD_BUS_METHOD ("Quit", "", "", method_quit, SD_BUS_VTABLE_UNPRIVILEGED),
  SD_BUS_VTABLE_END
};

int
main (int argc, char *argv[])
{
  sigset_t                         mask       = { 0 };
  int                              r          = 0;
  int                              no_window  = 0;
  _cleanup_ (generic_freep) char **fwd_args   = NULL;
  int                              n_fwd_args = 0;
  int                              i          = 0;

  sigemptyset (&mask);
  sigaddset (&mask, SIGTERM);
  sigaddset (&mask, SIGINT);
  sigaddset (&mask, SIGCHLD);
  sigprocmask (SIG_BLOCK, &mask, NULL);

  r = sd_event_default (&event);
  if (r < 0)
    return 1;

  sd_event_add_signal (event, NULL, SIGTERM, on_signal, NULL);
  sd_event_add_signal (event, NULL, SIGINT, on_signal, NULL);

  r = sd_bus_open_user (&bus);
  if (r < 0)
    {
      log_msg ("Failed to connect to session bus: %s", strerror (-r));
      return 1;
    }

  r = sd_bus_request_name (bus, BUS_NAME, 0);
  if (r < 0)
    {
      if (r == -EEXIST || r == -EALREADY)
        {
          log_msg ("App already running, forwarding");
          forward_to_running_daemon (bus, argc, argv);
          sd_bus_flush_close_unref (bus);
          sd_event_unref (event);
          return 0;
        }

      log_msg ("Failed to acquire bus name %s: %s", BUS_NAME, strerror (-r));
      return 1;
    }

  r = sd_bus_add_object_vtable (bus, NULL, OBJECT_PATH,
                                "org.gnome.Shell.SearchProvider2",
                                search_provider_vtable, NULL);
  if (r < 0)
    log_msg ("Failed to register search provider object: %s", strerror (-r));

  r = sd_bus_add_object_vtable (bus, NULL, OBJECT_PATH,
                                DAEMON_IFACE,
                                daemon_vtable, NULL);
  if (r < 0)
    log_msg ("Failed to register daemon object: %s", strerror (-r));

  r = sd_bus_attach_event (bus, event, SD_EVENT_PRIORITY_NORMAL);
  if (r < 0)
    log_msg ("Failed to attach bus to event loop: %s", strerror (-r));

  ensure_index_loaded ();
  schedule_next_update_check ();

  fwd_args = malloc_or_bail (sizeof (char *) * (size_t) (argc > 0 ? argc : 1));
  for (i = 1; i < argc; i++)
    {
      if (strcmp (argv[i], "--no-window") == 0)
        {
          no_window = 1;
          continue;
        }

      fwd_args[n_fwd_args] = argv[i];
      n_fwd_args++;
    }

  if (no_window)
    log_msg ("staying headless");
  else
    launch_app (fwd_args, n_fwd_args);

  arm_idle_timer ();
  sd_event_loop (event);
  cancel_idle_timer ();

  if (update_timer_source != NULL)
    sd_event_source_unref (update_timer_source);

  search_index_close (g_index);
  free (g_index_path);

  sd_bus_flush_close_unref (bus);
  sd_event_unref (event);

  return 0;
}

static int
auto_update_enabled (void)
{
  FILE *fp      = NULL;
  char  buf[8]  = { 0 };
  int   enabled = 0;

  fp = popen ("gsettings get io.github.kolunmi.Bazaar auto-update", "r");
  if (fp == NULL)
    return 0;

  if (fgets (buf, sizeof (buf), fp) != NULL)
    enabled = (strncmp (buf, "true", 4) == 0);

  pclose (fp);

  return enabled;
}

static void
cancel_idle_timer (void)
{
  if (idle_timer_source != NULL)
    {
      sd_event_source_unref (idle_timer_source);
      idle_timer_source = NULL;
    }
}

static void
arm_idle_timer (void)
{
  uint64_t now = 0;

  cancel_idle_timer ();

  if (child_pid > 0)
    return;

  if (sd_event_now (event, CLOCK_BOOTTIME, &now) < 0)
    return;

  sd_event_add_time (event, &idle_timer_source, CLOCK_BOOTTIME,
                     now + IDLE_EXIT_TIMEOUT_USEC, 0,
                     on_idle_timeout, NULL);
}

static int
on_idle_timeout (sd_event_source *s,
                 uint64_t         usec,
                 void            *userdata)
{
  idle_timer_source = NULL;

  if (child_pid > 0)
    return 0;

  if (auto_update_enabled ())
    {
      log_msg ("Idle, but auto update is on so staying alive");
      return 0;
    }

  log_msg ("Idle timeout reached, closing down");
  sd_event_exit (event, 0);
  return 0;
}

static int
forward_to_running_daemon (sd_bus *forward_bus, int argc, char *argv[])
{
  _cleanup_ (sd_bus_error_free) sd_bus_error        error = SD_BUS_ERROR_NULL;
  _cleanup_ (sd_bus_message_unrefp) sd_bus_message *m     = NULL;
  _cleanup_ (sd_bus_message_unrefp) sd_bus_message *reply = NULL;
  int                                               r     = 0;

  r = sd_bus_message_new_method_call (forward_bus, &m, BUS_NAME, OBJECT_PATH,
                                      DAEMON_IFACE, "Relaunch");
  if (r < 0)
    return r;

  r = sd_bus_message_append_strv (m, argv + (argc > 0 ? 1 : 0));
  if (r < 0)
    return r;

  r = sd_bus_call (forward_bus, m, ACTIVATE_TIMEOUT_US, &error, &reply);
  if (r < 0)
    log_msg ("Failed to forward relaunch request: %s",
             sd_bus_error_is_set (&error) ? error.message : strerror (-r));

  return r;
}

static int
method_relaunch (sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
  _cleanup_ (strv_freep) char **terms = NULL;
  int                           r     = 0;

  r = sd_bus_message_read_strv (m, &terms);
  if (r < 0)
    return r;

  log_msg ("Relaunch requested via D-Bus");

  arm_idle_timer ();
  launch_app (terms, strv_count_local (terms));

  return sd_bus_reply_method_return (m, NULL);
}

static int
method_quit (sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
  int r = 0;

  log_msg ("Quit requested");

  r = sd_bus_reply_method_return (m, NULL);

  if (child_pid > 0)
    kill (child_pid, SIGTERM);

  sd_event_exit (event, 0);
  return r;
}

static void
log_msg (const char *fmt, ...)
{
  va_list args;

  printf ("(bazaar-daemon): ");

  va_start (args, fmt);
  vprintf (fmt, args);
  va_end (args);

  printf ("\n");
  fflush (stdout);
}

static void *
malloc_or_bail (size_t n_bytes)
{
  void *p = NULL;

  p = malloc (n_bytes);
  if (p == NULL)
    {
      perror ("malloc");
      _exit (127);
    }

  return p;
}

static char *
build_index_path (void)
{
  const char *cache_home = NULL;
  const char *home       = NULL;
  char        buf[4096]  = { 0 };

  cache_home = getenv ("XDG_CACHE_HOME");
  if (cache_home != NULL && *cache_home != '\0')
    {
      snprintf (buf, sizeof (buf),
                "%s/io.github.kolunmi.Bazaar/core/search-index",
                cache_home);
      return strdup (buf);
    }

  home = getenv ("HOME");
  if (home != NULL && *home != '\0')
    {
      snprintf (buf, sizeof (buf),
                "%s/.cache/io.github.kolunmi.Bazaar/core/search-index",
                home);
      return strdup (buf);
    }

  return strdup ("/tmp/search-index");
}

static void
ensure_index_loaded (void)
{
  if (g_index_path == NULL)
    g_index_path = build_index_path ();

  if (g_index == NULL)
    {
      g_index = search_index_open (g_index_path);
      if (g_index == NULL)
        log_msg ("Search index not yet available at %s", g_index_path);
      return;
    }

  if (search_index_reload_if_stale (&g_index))
    log_msg ("Search index reloaded ");
}

static int
strv_count_local (char **strv)
{
  int n = 0;

  if (strv == NULL)
    return 0;

  while (strv[n] != NULL)
    n++;

  return n;
}

static void
strv_free_local (char **strv)
{
  char **p = NULL;

  if (strv == NULL)
    return;

  for (p = strv; *p != NULL; p++)
    free (*p);
  free (strv);
}

static void
strv_freep (char ***strv)
{
  strv_free_local (*strv);
}

static void
generic_freep (void *p)
{
  free (*(void **) p);
}

static int
on_child_exit (sd_event_source *s,
               const siginfo_t *si,
               void            *userdata)
{
  log_msg ("Application exited (status %d)", si->si_status);

  child_pid = -1;
  sd_event_source_unref (s);
  ensure_index_loaded ();

  arm_idle_timer ();

  return 0;
}

static void
launch_app (char **extra_args,
            int    n_extra_args)
{
  pid_t                            pid  = -1;
  _cleanup_ (generic_freep) char **argv = NULL;
  int                              i    = 0;
  sigset_t                         mask = { 0 };

  argv    = malloc_or_bail (sizeof (char *) * (size_t) (n_extra_args + 2));
  argv[0] = (char *) BAZAAR_BIN_PATH;
  for (i = 0; i < n_extra_args; i++)
    argv[i + 1] = extra_args[i];
  argv[n_extra_args + 1] = NULL;

  log_msg ("Application starting");

  cancel_idle_timer ();

  pid = fork ();
  if (pid == 0)
    {
      sigemptyset (&mask);
      sigprocmask (SIG_SETMASK, &mask, NULL);

      execvp (argv[0], argv);
      perror ("execvp");
      _exit (127);
    }

  if (pid < 0)
    {
      log_msg ("Failed to spawn application: %s", strerror (errno));
      arm_idle_timer ();
      return;
    }

  if (child_pid <= 0)
    child_pid = pid;

  sd_event_add_child (event, NULL, pid, WEXITED, on_child_exit, NULL);
}

static void
run_update_worker_once (void)
{
  pid_t    pid     = -1;
  char    *argv[3] = { NULL };
  sigset_t mask    = { 0 };

  argv[0] = (char *) BAZAAR_BIN_PATH;
  argv[1] = (char *) UPDATE_WORKER_CLI_OPTION;
  argv[2] = NULL;

  pid = fork ();
  if (pid == 0)
    {
      sigemptyset (&mask);
      sigprocmask (SIG_SETMASK, &mask, NULL);

      execvp (argv[0], argv);
      perror ("execvp");
      _exit (127);
    }

  if (pid < 0)
    {
      log_msg ("Failed to spawn update worker: %s", strerror (errno));
      return;
    }

  sd_event_add_child (event, NULL, pid, WEXITED, on_update_worker_exit, NULL);
}

static int
on_update_worker_exit (sd_event_source *s,
                       const siginfo_t *si,
                       void            *userdata)
{
  sd_event_source_unref (s);
  return 0;
}

static int
on_update_timer (sd_event_source *s,
                 uint64_t         usec,
                 void            *userdata)
{
  run_update_worker_once ();
  schedule_next_update_check ();
  return 0;
}

static int
schedule_next_update_check (void)
{
  uint64_t now = 0;
  int      r   = 0;

  if (update_timer_source != NULL)
    {
      sd_event_source_unref (update_timer_source);
      update_timer_source = NULL;
    }

  r = sd_event_now (event, CLOCK_BOOTTIME, &now);
  if (r < 0)
    return r;

  r = sd_event_add_time (event, &update_timer_source, CLOCK_BOOTTIME,
                         now + UPDATE_CHECK_INTERVAL_USEC, 0,
                         on_update_timer, NULL);
  if (r < 0)
    log_msg ("failed to schedule update: %s", strerror (-r));

  return r;
}

static int
build_and_send_search_reply (sd_bus_message *call,
                             char          **terms)
{
  _cleanup_ (sd_bus_message_unrefp) sd_bus_message *reply                       = NULL;
  SearchIndexMatch                                  matches[MAX_SEARCH_RESULTS] = { 0 };
  size_t                                            n_matches                   = 0;
  size_t                                            i                           = 0;
  int                                               r                           = 0;

  ensure_index_loaded ();

  n_matches = search_index_query (
      g_index, (const char *const *) terms,
      strv_count_local (terms),
      matches, MAX_SEARCH_RESULTS);

  SD_BUS_CHECK (sd_bus_message_new_method_return (call, &reply));
  SD_BUS_CHECK (sd_bus_message_open_container (reply, 'a', "s"));

  for (i = 0; i < n_matches; i++)
    SD_BUS_CHECK (sd_bus_message_append (reply, "s", matches[i].entry->id));

  SD_BUS_CHECK (sd_bus_message_close_container (reply));

  return sd_bus_send (NULL, reply, NULL);

out:
  return r;
}

static int
method_get_result_set (sd_bus_message *m,
                       void           *userdata,
                       sd_bus_error   *ret_error)
{
  _cleanup_ (strv_freep) char **terms = NULL;
  const char                   *sig   = NULL;
  int                           r     = 0;

  arm_idle_timer ();

  sig = sd_bus_message_get_signature (m, 1);

  if (sig != NULL && strcmp (sig, "asas") == 0)
    {
      _cleanup_ (strv_freep) char **previous = NULL;

      r = sd_bus_message_read_strv (m, &previous);
      if (r < 0)
        return r;
    }

  r = sd_bus_message_read_strv (m, &terms);
  if (r < 0)
    return r;

  return build_and_send_search_reply (m, terms);
}

static int
method_get_result_metas (sd_bus_message *m,
                         void           *userdata,
                         sd_bus_error   *ret_error)
{
  _cleanup_ (sd_bus_message_unrefp) sd_bus_message *reply = NULL;
  _cleanup_ (strv_freep) char                     **ids   = NULL;
  char                                            **p     = NULL;
  int                                               r     = 0;

  arm_idle_timer ();

  r = sd_bus_message_read_strv (m, &ids);
  if (r < 0)
    return r;

  ensure_index_loaded ();

  SD_BUS_CHECK (sd_bus_message_new_method_return (m, &reply));
  SD_BUS_CHECK (sd_bus_message_open_container (reply, 'a', "a{sv}"));

  for (p = ids; p != NULL && *p != NULL; p++)
    {
      const SearchIndexEntry *e = NULL;

      e = search_index_find (g_index, *p);

      if (e == NULL)
        continue;

      SD_BUS_CHECK (sd_bus_message_open_container (reply, 'a', "{sv}"));
      SD_BUS_CHECK (sd_bus_message_append (reply, "{sv}", "id", "s", e->id));
      SD_BUS_CHECK (sd_bus_message_append (reply, "{sv}", "name", "s", e->title));

      if (e->description != NULL)
        SD_BUS_CHECK (sd_bus_message_append (reply, "{sv}", "description", "s", e->description));

      if (e->icon_path != NULL)
        SD_BUS_CHECK (sd_bus_message_append (reply, "{sv}", "gicon", "s", e->icon_path));

      SD_BUS_CHECK (sd_bus_message_close_container (reply));
    }

  SD_BUS_CHECK (sd_bus_message_close_container (reply));

  return sd_bus_send (NULL, reply, NULL);

out:
  return r;
}

static int
method_activate_result (sd_bus_message *m,
                        void           *userdata,
                        sd_bus_error   *ret_error)
{
  const char                     *id      = NULL;
  _cleanup_ (generic_freep) char *uri_arg = NULL;
  char                           *args[1] = { NULL };
  int                             r       = 0;

  arm_idle_timer ();

  r = sd_bus_message_read (m, "s", &id);
  if (r < 0)
    return r;

  uri_arg = malloc_or_bail (strlen ("appstream:") + strlen (id) + 1);
  sprintf (uri_arg, "appstream:%s", id);

  args[0] = uri_arg;
  launch_app (args, 1);

  return sd_bus_reply_method_return (m, NULL);
}

static int
method_launch_search (sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
  _cleanup_ (strv_freep) char   **terms     = NULL;
  _cleanup_ (generic_freep) char *joined    = NULL;
  _cleanup_ (generic_freep) char *arg       = NULL;
  char                           *args[1]   = { NULL };
  uint32_t                        timestamp = 0;
  size_t                          len       = 0;
  int                             n_terms   = 0;
  int                             i         = 0;
  int                             r         = 0;

  arm_idle_timer ();

  r = sd_bus_message_read_strv (m, &terms);
  if (r < 0)
    return r;

  r = sd_bus_message_read (m, "u", &timestamp);
  if (r < 0)
    return r;

  n_terms = strv_count_local (terms);
  if (n_terms == 0)
    {
      launch_app (NULL, 0);
      return sd_bus_reply_method_return (m, NULL);
    }

  for (i = 0; i < n_terms; i++)
    len += strlen (terms[i]) + 1;

  joined    = malloc_or_bail (len);
  joined[0] = '\0';
  for (i = 0; i < n_terms; i++)
    {
      strcat (joined, terms[i]);
      if (i + 1 < n_terms)
        strcat (joined, " ");
    }

  arg = malloc_or_bail (strlen ("--search-for=") + strlen (joined) + 1);
  sprintf (arg, "--search-for=%s", joined);

  args[0] = arg;
  launch_app (args, 1);

  return sd_bus_reply_method_return (m, NULL);
}

static int
on_signal (sd_event_source               *s,
           const struct signalfd_siginfo *si,
           void                          *userdata)
{
  log_msg ("Daemon shutting down");

  if (child_pid > 0)
    kill (child_pid, SIGTERM);

  sd_event_exit (event, 0);

  return 0;
}
