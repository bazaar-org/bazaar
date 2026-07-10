/* error.c
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

#include <adwaita.h>
#include <glib/gi18n.h>

#include "bz-error-dialog.h"
#include "error.h"
#include "bz-window.h"

static void
show_alert (GtkWidget  *widget,
            const char *title,
            const char *text);

static void
await_alert_response (AdwAlertDialog *alert,
                      gchar          *response,
                      DexPromise     *promise);

static void
unref_dex_closure (gpointer  data,
                   GClosure *closure);

void
bz_show_error_for_widget (GtkWidget  *widget,
                          const char *title,
                          const char *text)
{
  g_return_if_fail (GTK_IS_WIDGET (widget));
  g_return_if_fail (text != NULL);

  show_alert (widget, title, text);
}

static void
on_toast_button_clicked (AdwToast  *toast,
                         GtkWidget *widget)
{
  BzErrorDialog *dialog;
  const char    *title = NULL;
  const char    *text  = NULL;

  if (!GTK_IS_WIDGET (widget))
    return;

  title = g_object_get_data (G_OBJECT (toast), "title");
  text  = g_object_get_data (G_OBJECT (toast), "text");

  dialog = bz_error_dialog_new (title ? title : _ ("Details"), text ? text : "");
  adw_dialog_present (ADW_DIALOG (dialog), widget);
}

static void
show_alert (GtkWidget  *widget,
            const char *title,
            const char *text)
{
  BzWindow        *window      = NULL;
  AdwToast        *toast       = NULL;
  g_autofree char *toast_title = NULL;

  window = BZ_WINDOW (gtk_widget_get_ancestor (widget, BZ_TYPE_WINDOW));
  if (window == NULL)
    return;

  toast_title = g_strdup_printf ("%s", title);
  toast       = adw_toast_new (toast_title);
  adw_toast_set_button_label (toast, _ ("Details"));
  adw_toast_set_priority (toast, ADW_TOAST_PRIORITY_HIGH);
  adw_toast_set_timeout (toast, 5);

  g_object_set_data_full (G_OBJECT (toast), "title", g_strdup (title), g_free);
  g_object_set_data_full (G_OBJECT (toast), "text", g_strdup (text), g_free);

  g_signal_connect (toast, "button-clicked",
                    G_CALLBACK (on_toast_button_clicked),
                    widget);

  bz_window_add_toast (window, toast);
}

DexFuture *
bz_make_alert_dialog_future (AdwAlertDialog *dialog)
{
  g_autoptr (DexPromise) promise = NULL;

  dex_return_error_if_fail (ADW_IS_ALERT_DIALOG (dialog));

  promise = dex_promise_new ();
  g_signal_connect_data (
      dialog, "response",
      G_CALLBACK (await_alert_response),
      dex_ref (promise), unref_dex_closure,
      G_CONNECT_DEFAULT);

  return DEX_FUTURE (g_steal_pointer (&promise));
}

static void
await_alert_response (AdwAlertDialog *alert,
                      gchar          *response,
                      DexPromise     *promise)
{
  dex_promise_resolve_string (promise, g_strdup (response));
}

static void
unref_dex_closure (gpointer  data,
                   GClosure *closure)
{
  DexPromise *promise = data;

  if (dex_future_is_pending (DEX_FUTURE (promise)))
    dex_promise_reject (
        promise,
        g_error_new (
            DEX_ERROR,
            DEX_ERROR_UNKNOWN,
            "The signal was disconnected"));

  dex_unref (promise);
}
