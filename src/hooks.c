/* hooks.c
 *
 * Copyright 2026 Eva M
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

#define G_LOG_DOMAIN "BAZAAR::HOOKS"

#include <adwaita.h>

#include "env.h"
#include "error.h"
#include "hooks.h"
#include "util.h"

BZ_DEFINE_DATA (
    execute_hook,
    ExecuteHook,
    {
      BzHook               *hook;
      BzHookTransactionType ts_type;
      char                 *ts_appid;
      BzEntryGroup         *group;
    },
    BZ_RELEASE_DATA (hook, g_object_unref);
    BZ_RELEASE_DATA (ts_appid, g_free);
    BZ_RELEASE_DATA (group, g_object_unref))

BZ_DEFINE_DATA (
    run_emission,
    RunEmission,
    {
      GListModel           *hooks;
      BzHookSignal          signal;
      BzHookTransactionType ts_type;
      char                 *ts_appid;
      BzEntryGroup         *group;
    },
    BZ_RELEASE_DATA (hooks, g_object_unref);
    BZ_RELEASE_DATA (ts_appid, g_free);
    BZ_RELEASE_DATA (group, g_object_unref))

BZ_DEFINE_DATA (
    dialog,
    Dialog,
    {
      char      *id;
      AdwDialog *dialog;
    },
    BZ_RELEASE_DATA (id, g_free);
    BZ_RELEASE_DATA (dialog, g_object_unref));

static DexFuture *
execute_hook_fiber (ExecuteHookData *data);

static DexFuture *
run_emission_fiber (RunEmissionData *data);

DexFuture *
bz_execute_hook (BzHook               *hook,
                 BzHookTransactionType ts_type,
                 const char           *ts_appid,
                 BzEntryGroup         *group)
{
  g_autoptr (ExecuteHookData) data = NULL;

  dex_return_error_if_fail (BZ_IS_HOOK (hook));

  data           = execute_hook_data_new ();
  data->hook     = g_object_ref (hook);
  data->ts_type  = ts_type;
  data->ts_appid = bz_maybe_strdup (ts_appid);
  data->group    = bz_object_maybe_ref (group);

  return dex_scheduler_spawn (
      dex_scheduler_get_default (),
      bz_get_dex_stack_size (),
      (DexFiberFunc) execute_hook_fiber,
      execute_hook_data_ref (data),
      execute_hook_data_unref);
}

DexFuture *
bz_run_hook_emission (GListModel           *hooks,
                      BzHookSignal          signal,
                      BzHookTransactionType ts_type,
                      const char           *ts_appid,
                      BzEntryGroup         *group)
{
  g_autoptr (RunEmissionData) data = NULL;

  dex_return_error_if_fail (G_IS_LIST_MODEL (hooks));

  data           = run_emission_data_new ();
  data->hooks    = g_object_ref (hooks);
  data->signal   = signal;
  data->ts_type  = ts_type;
  data->ts_appid = bz_maybe_strdup (ts_appid);
  data->group    = bz_object_maybe_ref (group);

  return dex_scheduler_spawn (
      dex_scheduler_get_default (),
      bz_get_dex_stack_size (),
      (DexFiberFunc) run_emission_fiber,
      execute_hook_data_ref (data),
      execute_hook_data_unref);
}

static DexFuture *
execute_hook_fiber (ExecuteHookData *data)
{
  BzHook               *hook                = data->hook;
  BzHookTransactionType ts_type             = data->ts_type;
  char                 *ts_appid            = data->ts_appid;
  BzEntryGroup         *group               = data->group;
  BzHookSignal          signal              = 0;
  g_autoptr (GEnumClass) signal_enum_class  = NULL;
  g_autoptr (GEnumClass) ts_type_enum_class = NULL;
  GEnumValue *signal_enum                   = NULL;
  GEnumValue *ts_type_enum                  = NULL;
  g_autoptr (GDateTime) date                = NULL;
  g_autofree char *timestamp_sec            = NULL;
  g_autofree char *timestamp_usec           = NULL;
  const char      *id                       = NULL;
  const char      *shell                    = NULL;
  g_autoptr (GPtrArray) dialogs             = NULL;
  g_autoptr (DialogData) current_dialog     = NULL;
  gboolean hook_aborted                     = FALSE;
  gboolean finish                           = FALSE;

  signal = bz_hook_get_when (hook);
  switch (signal)
    {
    case BZ_HOOK_SIGNAL_BEFORE_TRANSACTION:
    case BZ_HOOK_SIGNAL_AFTER_TRANSACTION:
      dex_return_error_if_fail (ts_appid != NULL);
      break;
    case BZ_HOOK_SIGNAL_VIEW_APP:
    case BZ_HOOK_SIGNAL_ARTICLE_APP:
    default:
      dex_return_error_if_fail (BZ_IS_ENTRY_GROUP (group));
      break;
    }

  signal_enum_class  = g_type_class_ref (BZ_TYPE_HOOK_SIGNAL);
  ts_type_enum_class = g_type_class_ref (BZ_TYPE_HOOK_TRANSACTION_TYPE);
  signal_enum        = g_enum_get_value (signal_enum_class, signal);
  ts_type_enum       = g_enum_get_value (ts_type_enum_class, ts_type);

  date           = g_date_time_new_now_utc ();
  timestamp_sec  = g_strdup_printf ("%zu", g_date_time_to_unix (date));
  timestamp_usec = g_strdup_printf ("%zu", g_date_time_to_unix_usec (date));

  id    = bz_hook_get_id (hook);
  shell = bz_hook_get_shell (hook);
  if (shell == NULL)
    {
      g_warning ("Main Config: hook definition must have shell code, skipping this hook");
      return dex_future_new_for_int (BZ_HOOK_RETURN_STATUS_CONTINUE);
    }

  dialogs = g_ptr_array_new_with_free_func (dialog_data_unref);
  if (bz_hook_get_dialogs (hook) != NULL)
    {
      GListModel *config_dialogs = NULL;
      guint       n_dialogs      = 0;

      config_dialogs = bz_hook_get_dialogs (hook);
      n_dialogs      = g_list_model_get_n_items (config_dialogs);

      for (guint i = 0; i < n_dialogs; i++)
        {
          g_autoptr (BzHookDialog) config_dialog = NULL;
          const char *dialog_id                  = NULL;
          const char *dialog_title               = NULL;
          const char *dialog_body                = NULL;
          gboolean    dialog_body_use_markup     = FALSE;
          const char *dialog_default_response    = NULL;
          g_autoptr (AdwDialog) dialog           = NULL;
          guint n_opts                           = 0;
          g_autoptr (DialogData) dialog_data     = NULL;

          config_dialog           = g_list_model_get_item (config_dialogs, i);
          dialog_id               = bz_hook_dialog_get_id (config_dialog);
          dialog_title            = bz_hook_dialog_get_title (config_dialog);
          dialog_body             = bz_hook_dialog_get_body (config_dialog);
          dialog_body_use_markup  = bz_hook_dialog_get_body_use_markup (config_dialog);
          dialog_default_response = bz_hook_dialog_get_default_response_id (config_dialog);

          if (dialog_title == NULL ||
              dialog_body == NULL)
            {
              g_warning ("Main Config: dialog definition must have a title and body, skipping this hook");
              return dex_future_new_for_int (BZ_HOOK_RETURN_STATUS_CONTINUE);
            }
          if (dialog_default_response == NULL)
            {
              g_warning ("Main Config: dialog definition must have a default response, skipping this hook");
              return dex_future_new_for_int (BZ_HOOK_RETURN_STATUS_CONTINUE);
            }
          dialog = g_object_ref_sink (adw_alert_dialog_new (dialog_title, dialog_body));

          if (bz_hook_dialog_get_options (config_dialog) != NULL)
            {
              GListModel *config_opts = NULL;
              guint       n_options   = 0;

              config_opts = bz_hook_dialog_get_options (config_dialog);
              n_options   = g_list_model_get_n_items (config_opts);

              for (guint j = 0; j < n_options; j++)
                {
                  g_autoptr (BzHookDialogOption) config_opt = NULL;
                  const char *opt_id                        = NULL;
                  const char *opt_string                    = NULL;
                  const char *opt_style                     = NULL;

                  config_opt = g_list_model_get_item (config_opts, j);

                  opt_id = bz_hook_dialog_option_get_id (config_opt);
                  if (opt_id == NULL)
                    {
                      g_warning ("Main Config: dialog option definition must have an id, skipping this hook");
                      return dex_future_new_for_int (BZ_HOOK_RETURN_STATUS_CONTINUE);
                    }

                  opt_string = bz_hook_dialog_option_get_string (config_opt);
                  if (opt_string == NULL)
                    {
                      g_warning ("Main Config: dialog option definition must have a string, skipping this hook");
                      return dex_future_new_for_int (BZ_HOOK_RETURN_STATUS_CONTINUE);
                    }

                  opt_style = bz_hook_dialog_option_get_style (config_opt);

                  adw_alert_dialog_add_response (ADW_ALERT_DIALOG (dialog), opt_id, opt_string);
                  if (opt_style != NULL)
                    {
                      AdwResponseAppearance appearance = ADW_RESPONSE_DEFAULT;

                      if (g_strcmp0 (opt_style, "suggested") == 0)
                        appearance = ADW_RESPONSE_SUGGESTED;
                      else if (g_strcmp0 (opt_style, "destructive") == 0)
                        appearance = ADW_RESPONSE_DESTRUCTIVE;
                      else
                        g_warning ("Main Config: dialog option definition appearance can be "
                                   "\"suggested\" or \"destructive\". \"%s\" is invalid.",
                                   opt_style);

                      adw_alert_dialog_set_response_appearance (
                          ADW_ALERT_DIALOG (dialog),
                          opt_id,
                          appearance);
                    }

                  n_opts++;
                }
            }
          if (n_opts == 0)
            {
              g_warning ("Main Config: dialog definition must have options, skipping this hook");
              return dex_future_new_for_int (BZ_HOOK_RETURN_STATUS_CONTINUE);
            }

          adw_alert_dialog_set_body_use_markup (
              ADW_ALERT_DIALOG (dialog),
              dialog_body_use_markup);
          adw_alert_dialog_set_default_response (
              ADW_ALERT_DIALOG (dialog),
              dialog_default_response);

          dialog_data         = dialog_data_new ();
          dialog_data->id     = dialog_id != NULL ? g_strdup (dialog_id) : NULL;
          dialog_data->dialog = g_steal_pointer (&dialog);
          g_ptr_array_add (dialogs, g_steal_pointer (&dialog_data));
        }
    }

  for (guint stage = 0;; stage++)
    {
      g_autoptr (GError) local_error           = NULL;
      g_autoptr (GSubprocessLauncher) launcher = NULL;
      g_autofree char *stage_str               = NULL;
      const char      *hook_stage              = NULL;
      g_autoptr (GSubprocess) subprocess       = NULL;
      gboolean      result                     = FALSE;
      GInputStream *stdout_pipe                = NULL;
      g_autoptr (GBytes) stdout_bytes          = NULL;
      gsize            stdout_size             = 0;
      gconstpointer    stdout_data             = NULL;
      g_autofree char *stdout_str              = NULL;
      char            *stdout_newline          = NULL;

      launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_STDOUT_PIPE);
      g_subprocess_launcher_set_cwd (launcher, g_get_home_dir ());

      g_subprocess_launcher_setenv (launcher, "BAZAAR_HOOK_INITIATED_UNIX_STAMP", timestamp_sec, TRUE);
      g_subprocess_launcher_setenv (launcher, "BAZAAR_HOOK_INITIATED_UNIX_STAMP_USEC", timestamp_usec, TRUE);

      stage_str = g_strdup_printf ("%d", stage);
      g_subprocess_launcher_setenv (launcher, "BAZAAR_HOOK_STAGE_IDX", stage_str, TRUE);

      g_subprocess_launcher_setenv (launcher, "BAZAAR_HOOK_ID", id, TRUE);
      g_subprocess_launcher_setenv (launcher, "BAZAAR_HOOK_TYPE", signal_enum->value_nick, TRUE);

      g_subprocess_launcher_setenv (launcher, "BAZAAR_HOOK_WAS_ABORTED", hook_aborted ? "true" : "false", TRUE);

      switch (signal)
        {
        case BZ_HOOK_SIGNAL_BEFORE_TRANSACTION:
        case BZ_HOOK_SIGNAL_AFTER_TRANSACTION:
          g_subprocess_launcher_setenv (launcher, "BAZAAR_TS_APPID", ts_appid, TRUE);
          g_subprocess_launcher_setenv (launcher, "BAZAAR_TS_TYPE", ts_type_enum->value_nick, TRUE);
          break;
        case BZ_HOOK_SIGNAL_VIEW_APP:
        case BZ_HOOK_SIGNAL_ARTICLE_APP:
        default:
          g_subprocess_launcher_setenv (launcher, "BAZAAR_APPID", bz_entry_group_get_id (group), TRUE);
          break;
        }

      if (finish)
        hook_stage = "teardown";
      else if (hook_aborted)
        hook_stage = "catch";
      else if (stage == 0)
        hook_stage = "setup";
      else if (current_dialog != NULL)
        {
          GApplication    *application = NULL;
          GtkWindow       *window      = NULL;
          g_autofree char *response    = NULL;

          hook_stage = "teardown-dialog";

          application = g_application_get_default ();
          window      = gtk_application_get_active_window (GTK_APPLICATION (application));

          if (window != NULL)
            {
              adw_dialog_present (current_dialog->dialog, GTK_WIDGET (window));
              response = dex_await_string (
                  bz_make_alert_dialog_future (ADW_ALERT_DIALOG (current_dialog->dialog)),
                  &local_error);
              if (response == NULL)
                g_warning ("Failed to resolve response from dialog "
                           "\"%s\", assuming default response \"%s\": %s",
                           current_dialog->id,
                           adw_alert_dialog_get_default_response (
                               ADW_ALERT_DIALOG (current_dialog->dialog)),
                           local_error->message);
              g_clear_pointer (&local_error, g_error_free);
            }
          else
            g_warning ("A window was not available to present dialog "
                       "\"%s\" on, assuming default response \"%s\"",
                       current_dialog->id,
                       adw_alert_dialog_get_default_response (
                           ADW_ALERT_DIALOG (current_dialog->dialog)));

          g_subprocess_launcher_setenv (launcher, "BAZAAR_HOOK_DIALOG_ID", current_dialog->id, TRUE);
          g_subprocess_launcher_setenv (
              launcher,
              "BAZAAR_HOOK_DIALOG_RESPONSE_ID",
              response != NULL
                  ? response
                  : adw_alert_dialog_get_default_response (
                        ADW_ALERT_DIALOG (current_dialog->dialog)),
              TRUE);
          g_clear_pointer (&current_dialog, dialog_data_unref);
        }
      else if (dialogs->len > 0)
        {
          hook_stage = "setup-dialog";

          current_dialog = g_ptr_array_steal_index (dialogs, 0);
          g_subprocess_launcher_setenv (launcher, "BAZAAR_HOOK_DIALOG_ID", current_dialog->id, TRUE);
        }
      else
        hook_stage = "action";
      g_subprocess_launcher_setenv (launcher, "BAZAAR_HOOK_STAGE", hook_stage, TRUE);

      subprocess = g_subprocess_launcher_spawn (
          launcher,
          &local_error,
          "/bin/sh",
          "-c",
          shell,
          NULL);
      if (subprocess == NULL)
        {
          g_warning ("Hook failed to spawn, abandoning it now: %s", local_error->message);
          return dex_future_new_for_int (BZ_HOOK_RETURN_STATUS_CONTINUE);
        }

      result = dex_await (
          dex_subprocess_wait_check (subprocess),
          &local_error);
      if (!result)
        {
          g_warning ("Hook failed to exit cleanly, abandoning it now: %s", local_error->message);
          return dex_future_new_for_int (BZ_HOOK_RETURN_STATUS_CONTINUE);
        }

      stdout_pipe  = g_subprocess_get_stdout_pipe (subprocess);
      stdout_bytes = g_input_stream_read_bytes (stdout_pipe, 1024, NULL, &local_error);
      if (!stdout_bytes)
        {
          g_warning ("Failed to read stdout pipe of hook, abandoning it now: %s", local_error->message);
          return dex_future_new_for_int (BZ_HOOK_RETURN_STATUS_CONTINUE);
        }

      stdout_data = g_bytes_get_data (stdout_bytes, &stdout_size);
      stdout_str  = g_malloc (stdout_size + 1);

      memcpy (stdout_str, stdout_data, stdout_size);
      stdout_str[stdout_size] = '\0';

      stdout_newline = strchr (stdout_str, '\n');
      if (stdout_newline != NULL)
        *stdout_newline = '\0';

      if (g_strcmp0 (hook_stage, "setup") == 0)
        {
          if (g_strcmp0 (stdout_str, "ok") == 0)
            continue;
          else if (g_strcmp0 (stdout_str, "pass") == 0)
            return dex_future_new_for_int (BZ_HOOK_RETURN_STATUS_CONTINUE);
        }
      else if (g_strcmp0 (hook_stage, "setup-dialog") == 0)
        {
          if (g_strcmp0 (stdout_str, "ok") == 0)
            continue;
          else if (g_strcmp0 (stdout_str, "pass") == 0)
            {
              g_clear_pointer (&current_dialog, dialog_data_unref);
              continue;
            }
        }
      else if (g_strcmp0 (hook_stage, "teardown-dialog") == 0)
        {
          if (g_strcmp0 (stdout_str, "ok") == 0)
            continue;
          else if (g_strcmp0 (stdout_str, "abort") == 0)
            {
              hook_aborted = TRUE;
              continue;
            }
        }
      else if (g_strcmp0 (hook_stage, "catch") == 0)
        {
          if (g_strcmp0 (stdout_str, "recover") == 0)
            {
              hook_aborted = FALSE;
              continue;
            }
          else if (g_strcmp0 (stdout_str, "abort") == 0)
            {
              finish = TRUE;
              continue;
            }
        }
      else if (g_strcmp0 (hook_stage, "action") == 0)
        {
          finish = TRUE;
          continue;
        }
      else if (g_strcmp0 (hook_stage, "teardown") == 0)
        {
          if (g_strcmp0 (stdout_str, "continue") == 0)
            return dex_future_new_for_int (BZ_HOOK_RETURN_STATUS_CONTINUE);
          else if (g_strcmp0 (stdout_str, "stop") == 0)
            return dex_future_new_for_int (BZ_HOOK_RETURN_STATUS_STOP);
          else if (g_strcmp0 (stdout_str, "confirm") == 0)
            return dex_future_new_for_int (BZ_HOOK_RETURN_STATUS_CONFIRM);
          else if (g_strcmp0 (stdout_str, "deny") == 0)
            return dex_future_new_for_int (BZ_HOOK_RETURN_STATUS_DENY);
        }
      else
        g_assert_not_reached ();

      g_warning ("Received invalid response from hook for stage \"%s\", abandoning it now",
                 hook_stage);
      return dex_future_new_for_int (BZ_HOOK_RETURN_STATUS_CONTINUE);
    }
}

static DexFuture *
run_emission_fiber (RunEmissionData *data)
{
  GListModel           *hooks    = data->hooks;
  BzHookSignal          signal   = data->signal;
  BzHookTransactionType ts_type  = data->ts_type;
  char                 *ts_appid = data->ts_appid;
  BzEntryGroup         *group    = data->group;
  guint                 n_hooks  = 0;

  n_hooks = g_list_model_get_n_items (hooks);
  for (guint i = 0; i < n_hooks; i++)
    {
      g_autoptr (BzHook) hook = NULL;
      BzHookSignal when       = 0;

      hook = g_list_model_get_item (hooks, i);
      when = bz_hook_get_when (hook);

      if (when == signal)
        {
          const char        *check_appid_regex = NULL;
          BzHookReturnStatus hook_result       = BZ_HOOK_RETURN_STATUS_CONTINUE;

          check_appid_regex = bz_hook_get_check_appid_regex (hook);
          if (check_appid_regex != NULL)
            {
              GRegex *regex = NULL;

              regex = g_object_get_data (G_OBJECT (hook), "check-appid-regex");
              if (regex == NULL)
                {
                  g_autoptr (GError) local_error = NULL;

                  regex = g_regex_new (
                      check_appid_regex,
                      G_REGEX_OPTIMIZE,
                      G_REGEX_MATCH_DEFAULT,
                      &local_error);
                  if (regex != NULL)
                    g_object_set_data_full (
                        G_OBJECT (hook),
                        "check-appid-regex",
                        regex,
                        (GDestroyNotify) g_regex_unref);
                  else
                    g_warning ("Hook check-appid-regex property '%s' "
                               "is an invalid regex string: %s",
                               check_appid_regex,
                               local_error->message);
                }

              if (regex != NULL &&
                  !g_regex_match (regex, ts_appid, G_REGEX_MATCH_DEFAULT, NULL))
                continue;
            }

          hook_result = dex_await_int (
              bz_execute_hook (hook, ts_type, ts_appid, group),
              NULL);

          if (hook_result == BZ_HOOK_RETURN_STATUS_CONFIRM ||
              hook_result == BZ_HOOK_RETURN_STATUS_STOP)
            break;
          else if (hook_result == BZ_HOOK_RETURN_STATUS_DENY)
            return dex_future_new_reject (
                G_IO_ERROR,
                G_IO_ERROR_UNKNOWN,
                "Prevented by a configured hook");
        }
    }

  return dex_future_new_true ();
}
