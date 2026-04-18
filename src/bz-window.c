/* bz-window.c
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

// This file is an utter mess
#include "config.h"

#include <glib/gi18n.h>

#include "bz-addons-dialog.h"
#include "bz-application.h"
#include "bz-comet-overlay.h"
#include "bz-curated-view.h"
#include "bz-entry-group-util.h"
#include "bz-entry-group.h"
#include "bz-env.h"
#include "bz-error.h"
#include "bz-flathub-page.h"
#include "bz-flatpak-entry.h"
#include "bz-full-view.h"
#include "bz-global-progress.h"
#include "bz-hooks.h"
#include "bz-io.h"
#include "bz-library-page.h"
#include "bz-progress-bar.h"
#include "bz-search-page.h"
#include "bz-template-callbacks.h"
#include "bz-transaction-dialog.h"
#include "bz-transaction-manager.h"
#include "bz-user-data-page.h"
#include "bz-util.h"
#include "bz-window.h"

struct _BzWindow
{
  AdwApplicationWindow parent_instance;

  BzStateInfo *state;

  GtkEventController *key_controller;

  gboolean breakpoint_applied;

  /* Template widgets */
  BzCometOverlay    *comet_overlay;
  AdwNavigationView *navigation_view;
  BzFullView        *full_view;
  BzSearchPage      *search_page;
  BzLibraryPage     *library_page;
  AdwToastOverlay   *toasts;
  AdwViewStack      *main_view_stack;
  GtkStack          *main_stack;
};

G_DEFINE_FINAL_TYPE (BzWindow, bz_window, ADW_TYPE_APPLICATION_WINDOW)

enum
{
  PROP_0,

  PROP_STATE,
  PROP_COMPACT,

  LAST_PROP
};
static GParamSpec *props[LAST_PROP] = { 0 };

BZ_DEFINE_DATA (
    transact,
    Transact,
    {
      GWeakRef     *self;
      BzEntry      *entry;
      BzEntryGroup *group;
      gboolean      remove;
      gboolean      auto_confirm;
      GtkWidget    *source;
    },
    BZ_RELEASE_DATA (self, bz_weak_release);
    BZ_RELEASE_DATA (entry, g_object_unref);
    BZ_RELEASE_DATA (group, g_object_unref);
    BZ_RELEASE_DATA (source, g_object_unref))

static DexFuture *
transact_fiber (TransactData *data);

BZ_DEFINE_DATA (
    bulk_install,
    BulkInstall,
    {
      GWeakRef   *self;
      GListModel *groups;
    },
    BZ_RELEASE_DATA (self, bz_weak_release);
    BZ_RELEASE_DATA (groups, g_object_unref))

static DexFuture *
bulk_install_fiber (BulkInstallData *data);

static DexFuture *
transact (BzWindow  *self,
          BzEntry   *entry,
          gboolean   remove,
          GtkWidget *source);

static void
try_transact (BzWindow     *self,
              BzEntry      *entry,
              BzEntryGroup *group,
              gboolean      remove,
              gboolean      auto_confirm,
              GtkWidget    *source);

static void
search (BzWindow   *self,
        const char *text);

static void
bulk_install (BzWindow *self,
              BzEntry **installs,
              guint     n_installs);

static void
set_page (BzWindow *self);

static void
emit_hook_disown (BzWindow     *self,
                  BzHookSignal  signal,
                  BzEntryGroup *group);

static void
bz_window_dispose (GObject *object)
{
  BzWindow *self = BZ_WINDOW (object);

  g_clear_object (&self->state);

  G_OBJECT_CLASS (bz_window_parent_class)->dispose (object);
}

static void
bz_window_get_property (GObject    *object,
                        guint       prop_id,
                        GValue     *value,
                        GParamSpec *pspec)
{
  BzWindow *self = BZ_WINDOW (object);

  switch (prop_id)
    {
    case PROP_STATE:
      g_value_set_object (value, self->state);
      break;
    case PROP_COMPACT:
      g_value_set_boolean (value, self->breakpoint_applied);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_window_set_property (GObject      *object,
                        guint         prop_id,
                        const GValue *value,
                        GParamSpec   *pspec)
{
  // BzWindow *self = BZ_WINDOW (object);

  switch (prop_id)
    {
    case PROP_STATE:
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static char *
list_length (gpointer    object,
             GListModel *model)
{
  if (model == NULL)
    return g_strdup (0);

  return g_strdup_printf ("%u", g_list_model_get_n_items (model));
}

static void
update_cb (BzWindow   *self,
           GListModel *entries,
           GtkWidget  *widget)
{
  g_autoptr (BzTransaction) transaction  = NULL;
  guint                n_updates         = 0;
  g_autofree BzEntry **updates_buf       = NULL;
  GListModel          *available_updates = NULL;

  g_return_if_fail (BZ_IS_WINDOW (self));
  g_return_if_fail (G_IS_LIST_MODEL (entries));

  n_updates = g_list_model_get_n_items (entries);
  if (n_updates == 0)
    return;

  updates_buf = g_malloc_n (n_updates, sizeof (*updates_buf));
  for (guint i = 0; i < n_updates; i++)
    updates_buf[i] = g_list_model_get_item (entries, i);

  transaction = bz_transaction_new_full (
      NULL, 0,
      updates_buf, n_updates,
      NULL, 0);

  dex_future_disown (bz_transaction_manager_add (
      bz_state_info_get_transaction_manager (self->state),
      transaction));

  available_updates = bz_state_info_get_available_updates (self->state);
  if (G_IS_LIST_STORE (available_updates))
    {
      GListStore *store       = G_LIST_STORE (available_updates);
      guint       n_available = g_list_model_get_n_items (available_updates);

      for (guint i = n_available; i > 0; i--)
        {
          guint current_size                  = 0;
          guint idx                           = 0;
          g_autoptr (BzEntry) available_entry = NULL;
          const char *available_id            = NULL;

          idx          = i - 1;
          current_size = g_list_model_get_n_items (available_updates);

          if (idx >= current_size)
            continue;

          available_entry = g_list_model_get_item (available_updates, idx);
          available_id    = bz_entry_get_id (available_entry);

          for (guint j = 0; j < n_updates; j++)
            {
              if (g_strcmp0 (available_id, bz_entry_get_id (updates_buf[j])) == 0)
                {
                  g_list_store_remove (store, idx);
                  break;
                }
            }
        }
    }

  g_object_notify (G_OBJECT (self->state), "available-updates");

  for (guint i = 0; i < n_updates; i++)
    g_object_unref (updates_buf[i]);
}

void
bz_window_show_app_id (BzWindow   *self,
                       const char *app_id)
{
  g_autoptr (BzEntryGroup) group = NULL;

  g_return_if_fail (BZ_IS_WINDOW (self));
  g_return_if_fail (app_id != NULL);

  group = bz_application_map_factory_convert_one (
      bz_state_info_get_application_factory (self->state),
      gtk_string_object_new (app_id));

  if (group != NULL)
    bz_window_show_group (self, group);
}

static void
page_toggled_cb (BzWindow       *self,
                 GParamSpec     *pspec,
                 AdwToggleGroup *toggles)
{
  set_page (self);
}

static void
browse_flathub_cb (BzWindow      *self,
                   BzCuratedView *widget)
{
  adw_view_stack_set_visible_child_name (self->main_view_stack, "flathub");
}

static void
open_search_cb (BzWindow     *self,
                BzSearchPage *widget)
{
  adw_view_stack_set_visible_child_name (self->main_view_stack, "search");
}

static void
breakpoint_apply_cb (BzWindow      *self,
                     AdwBreakpoint *breakpoint)
{
  self->breakpoint_applied = TRUE;

  gtk_widget_add_css_class (GTK_WIDGET (self), "narrow");
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_COMPACT]);
}

static void
breakpoint_unapply_cb (BzWindow      *self,
                       AdwBreakpoint *breakpoint)
{
  self->breakpoint_applied = FALSE;

  gtk_widget_remove_css_class (GTK_WIDGET (self), "narrow");
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_COMPACT]);
}

static void
sync_cb (BzWindow  *self,
         GtkButton *button)
{
  g_action_group_activate_action (
      G_ACTION_GROUP (g_application_get_default ()),
      "sync-remotes", NULL);
}

static void
transactions_clear_cb (BzWindow  *self,
                       GtkButton *button)
{
  bz_transaction_manager_clear_finished (
      bz_state_info_get_transaction_manager (self->state));
}

static void
action_escape (GtkWidget  *widget,
               const char *action_name,
               GVariant   *parameter)
{
  BzWindow   *self    = BZ_WINDOW (widget);
  GListModel *stack   = NULL;
  guint       n_pages = 0;

  stack   = adw_navigation_view_get_navigation_stack (self->navigation_view);
  n_pages = g_list_model_get_n_items (stack);

  adw_navigation_view_pop (self->navigation_view);
  if (n_pages <= 2)
    set_page (self);
}

static char *
format_progress (gpointer object,
                 double   value)
{
  return g_strdup_printf ("%.0f%%", 100.0 * value);
}

static char *
format_title (gpointer    object,
              const char *title)
{
  if (title == NULL || *title == '\0' || g_strcmp0 (title, _ ("Bazaar")) == 0)
    return g_strdup (_ ("Bazaar"));
  /* Translators: %s is the title of the current page */
  return g_strdup_printf (_ ("Bazaar — %s"), title);
}

static BzEntryGroup *
resolve_group_from_parameter (BzWindow *self,
                              GVariant *parameter,
                              gboolean *auto_confirm)
{
  const char *id = NULL;

  g_variant_get (parameter, "(&sb)", &id, auto_confirm);

  return bz_application_map_factory_convert_one (
      bz_state_info_get_application_factory (self->state),
      gtk_string_object_new (id));
}

static void
action_install_group (GtkWidget  *widget,
                      const char *action_name,
                      GVariant   *parameter)
{
  BzWindow *self                 = BZ_WINDOW (widget);
  g_autoptr (BzEntryGroup) group = NULL;
  gboolean auto_confirm          = FALSE;

  group = resolve_group_from_parameter (self, parameter, &auto_confirm);
  if (group != NULL)
    try_transact (self, NULL, group, FALSE, auto_confirm, NULL);
}

static void
action_remove_group (GtkWidget  *widget,
                     const char *action_name,
                     GVariant   *parameter)
{
  BzWindow *self                 = BZ_WINDOW (widget);
  g_autoptr (BzEntryGroup) group = NULL;
  gboolean auto_confirm          = FALSE;

  group = resolve_group_from_parameter (self, parameter, &auto_confirm);
  if (group != NULL)
    try_transact (self, NULL, group, TRUE, auto_confirm, NULL);
}

static void
action_show_group (GtkWidget  *widget,
                   const char *action_name,
                   GVariant   *parameter)
{
  BzWindow   *self               = BZ_WINDOW (widget);
  const char *id                 = NULL;
  g_autoptr (BzEntryGroup) group = NULL;

  id    = g_variant_get_string (parameter, NULL);
  group = bz_application_map_factory_convert_one (
      bz_state_info_get_application_factory (self->state),
      gtk_string_object_new (id));

  if (group == NULL)
    return;

  if (bz_entry_group_is_addon (group))
    {
      AdwDialog *dialog = NULL;

      dialog = bz_addons_dialog_new_single (group);
      adw_dialog_present (dialog, GTK_WIDGET (self));
    }
  else
    bz_window_show_group (self, group);
}

static void
action_addons_group (GtkWidget  *widget,
                     const char *action_name,
                     GVariant   *parameter)
{
  BzWindow   *self               = BZ_WINDOW (widget);
  const char *id                 = NULL;
  g_autoptr (BzEntryGroup) group = NULL;
  AdwDialog *addons_dialog       = NULL;

  id    = g_variant_get_string (parameter, NULL);
  group = bz_application_map_factory_convert_one (
      bz_state_info_get_application_factory (self->state),
      gtk_string_object_new (id));

  if (group == NULL)
    return;

  addons_dialog = bz_addons_dialog_new (group);
  adw_dialog_present (addons_dialog, GTK_WIDGET (self));
}

static void
action_bulk_install (GtkWidget  *widget,
                     const char *action_name,
                     GVariant   *parameter)
{
  BzWindow *self                = BZ_WINDOW (widget);
  g_autoptr (GListStore) ids    = NULL;
  g_autoptr (GListModel) groups = NULL;
  GVariantIter iter             = { 0 };
  const char  *id               = NULL;

  ids = g_list_store_new (GTK_TYPE_STRING_OBJECT);

  g_variant_iter_init (&iter, parameter);
  while (g_variant_iter_next (&iter, "&s", &id))
    {
      g_autoptr (GtkStringObject) string = gtk_string_object_new (id);
      g_list_store_append (ids, string);
    }

  groups = bz_application_map_factory_generate (
      bz_state_info_get_application_factory (self->state),
      G_LIST_MODEL (ids));

  if (groups != NULL && g_list_model_get_n_items (groups) > 0)
    bz_window_bulk_install (self, groups);
}

static void
action_user_data (GtkWidget  *widget,
                  const char *action_name,
                  GVariant   *parameter)
{
  BzWindow          *self           = BZ_WINDOW (widget);
  AdwNavigationPage *user_data_page = NULL;

  user_data_page = ADW_NAVIGATION_PAGE (bz_user_data_page_new (self->state));
  adw_navigation_view_push (self->navigation_view, user_data_page);
}

static void
action_open_library (GtkWidget  *widget,
                     const char *action_name,
                     GVariant   *parameter)
{
  BzWindow *self = BZ_WINDOW (widget);

  adw_navigation_view_pop_to_tag (self->navigation_view, "main");
  adw_view_stack_set_visible_child_name (self->main_view_stack, "installed");
  bz_library_page_reset_search (self->library_page);
}

static DexFuture *
launch_group_fiber (BzEntryGroup *group)
{
  g_autoptr (GError) local_error = NULL;
  g_autoptr (GListStore) store   = NULL;
  GtkWidget   *window            = NULL;
  BzStateInfo *state             = NULL;

  state  = bz_state_info_get_default ();
  window = GTK_WIDGET (gtk_application_get_active_window (
      GTK_APPLICATION (g_application_get_default ())));

  store = dex_await_object (
      bz_entry_group_dup_all_into_store (group), &local_error);
  if (store == NULL)
    {
      if (window != NULL)
        bz_show_error_for_widget (window, _ ("Failed to launch application"), local_error->message);
      return dex_future_new_for_error (g_steal_pointer (&local_error));
    }

  for (guint i = 0; i < g_list_model_get_n_items (G_LIST_MODEL (store)); i++)
    {
      g_autoptr (BzEntry) entry = NULL;

      entry = g_list_model_get_item (G_LIST_MODEL (store), i);
      if (BZ_IS_FLATPAK_ENTRY (entry) && bz_entry_is_installed (entry))
        {
          gboolean result = bz_flatpak_entry_launch (
              BZ_FLATPAK_ENTRY (entry),
              BZ_FLATPAK_INSTANCE (bz_state_info_get_backend (state)),
              &local_error);

          if (!result && window != NULL)
            bz_show_error_for_widget (window, _ ("Failed to launch application"), local_error->message);

          return result ? dex_future_new_true () : dex_future_new_for_error (g_steal_pointer (&local_error));
        }
    }

  return dex_future_new_false ();
}

static void
action_launch_group (GtkWidget  *widget,
                     const char *action_name,
                     GVariant   *parameter)
{
  BzWindow   *self               = BZ_WINDOW (widget);
  const char *id                 = NULL;
  g_autoptr (BzEntryGroup) group = NULL;

  id    = g_variant_get_string (parameter, NULL);
  group = bz_application_map_factory_convert_one (
      bz_state_info_get_application_factory (self->state),
      gtk_string_object_new (id));

  if (group == NULL)
    return;

  dex_future_disown (dex_scheduler_spawn (
      dex_scheduler_get_default (),
      bz_get_dex_stack_size (),
      (DexFiberFunc) launch_group_fiber,
      g_object_ref (group),
      g_object_unref));
}

static void
bz_window_class_init (BzWindowClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose      = bz_window_dispose;
  object_class->get_property = bz_window_get_property;
  object_class->set_property = bz_window_set_property;

  props[PROP_STATE] =
      g_param_spec_object (
          "state",
          NULL, NULL,
          BZ_TYPE_STATE_INFO,
          G_PARAM_READABLE);

  props[PROP_COMPACT] =
      g_param_spec_boolean (
          "compact",
          NULL, NULL, FALSE,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  g_type_ensure (BZ_TYPE_COMET_OVERLAY);
  g_type_ensure (BZ_TYPE_SEARCH_PAGE);
  g_type_ensure (BZ_TYPE_GLOBAL_PROGRESS);
  g_type_ensure (BZ_TYPE_PROGRESS_BAR);
  g_type_ensure (BZ_TYPE_CURATED_VIEW);
  g_type_ensure (BZ_TYPE_FULL_VIEW);
  g_type_ensure (BZ_TYPE_LIBRARY_PAGE);
  g_type_ensure (BZ_TYPE_FLATHUB_PAGE);

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/kolunmi/Bazaar/bz-window.ui");
  bz_widget_class_bind_all_util_callbacks (widget_class);

  gtk_widget_class_bind_template_child (widget_class, BzWindow, comet_overlay);
  gtk_widget_class_bind_template_child (widget_class, BzWindow, navigation_view);
  gtk_widget_class_bind_template_child (widget_class, BzWindow, full_view);
  gtk_widget_class_bind_template_child (widget_class, BzWindow, toasts);
  gtk_widget_class_bind_template_child (widget_class, BzWindow, search_page);
  gtk_widget_class_bind_template_child (widget_class, BzWindow, library_page);
  gtk_widget_class_bind_template_child (widget_class, BzWindow, main_view_stack);
  gtk_widget_class_bind_template_child (widget_class, BzWindow, main_stack);
  gtk_widget_class_bind_template_callback (widget_class, list_length);
  gtk_widget_class_bind_template_callback (widget_class, update_cb);
  gtk_widget_class_bind_template_callback (widget_class, page_toggled_cb);
  gtk_widget_class_bind_template_callback (widget_class, breakpoint_apply_cb);
  gtk_widget_class_bind_template_callback (widget_class, breakpoint_unapply_cb);
  gtk_widget_class_bind_template_callback (widget_class, sync_cb);
  gtk_widget_class_bind_template_callback (widget_class, transactions_clear_cb);
  gtk_widget_class_bind_template_callback (widget_class, browse_flathub_cb);
  gtk_widget_class_bind_template_callback (widget_class, open_search_cb);
  gtk_widget_class_bind_template_callback (widget_class, format_progress);
  gtk_widget_class_bind_template_callback (widget_class, format_title);

  gtk_widget_class_install_action (widget_class, "escape", NULL, action_escape);
  gtk_widget_class_install_action (widget_class, "window.user-data", NULL, action_user_data);
  gtk_widget_class_install_action (widget_class, "window.open-library", NULL, action_open_library);

  gtk_widget_class_install_action (widget_class, "window.install-group", "(sb)", action_install_group);
  gtk_widget_class_install_action (widget_class, "window.remove-group", "(sb)", action_remove_group);
  gtk_widget_class_install_action (widget_class, "window.show-group", "s", action_show_group);
  gtk_widget_class_install_action (widget_class, "window.addons-group", "s", action_addons_group);
  gtk_widget_class_install_action (widget_class, "window.bulk-install", NULL, action_bulk_install);
  gtk_widget_class_install_action (widget_class, "window.launch-group", "s", action_launch_group);

  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_d, GDK_CONTROL_MASK, "window.open-library", NULL);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_w, GDK_CONTROL_MASK, "window.close", NULL);
}

static gboolean
key_pressed (BzWindow              *self,
             guint                  keyval,
             guint                  keycode,
             GdkModifierType        state,
             GtkEventControllerKey *controller)
{
  gunichar    unichar            = 0;
  char        buf[32]            = { 0 };
  const char *visible_child_name = NULL;

  /* Ignore if this is a modifier-shortcut of some sort */
  if (state & ~(GDK_NO_MODIFIER_MASK | GDK_SHIFT_MASK))
    return FALSE;

  unichar = gdk_keyval_to_unicode (keyval);
  if (unichar == 0 || !g_unichar_isgraph (unichar))
    return FALSE;
  g_unichar_to_utf8 (unichar, buf);

  visible_child_name = adw_view_stack_get_visible_child_name (self->main_view_stack);
  if (g_strcmp0 (visible_child_name, "installed") == 0)
    return bz_library_page_ensure_active (self->library_page, buf);
  else
    {
      adw_view_stack_set_visible_child_name (self->main_view_stack, "search");
      return bz_search_page_ensure_active (self->search_page, buf);
    }
}

static void
bz_window_init (BzWindow *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

#ifdef DEVELOPMENT_BUILD
  gtk_widget_add_css_class (GTK_WIDGET (self), "devel");
#endif

  adw_view_stack_set_visible_child_name (self->main_view_stack, "flathub");

  self->key_controller = gtk_event_controller_key_new ();
  g_signal_connect_swapped (self->key_controller,
                            "key-pressed",
                            G_CALLBACK (key_pressed),
                            self);
  gtk_widget_add_controller (GTK_WIDGET (self), self->key_controller);
}

static void
app_busy_changed (BzWindow    *self,
                  GParamSpec  *pspec,
                  BzStateInfo *info)
{
  bz_search_page_refresh (self->search_page);
  set_page (self);
}

static void
has_inputs_changed (BzWindow          *self,
                    GParamSpec        *pspec,
                    BzContentProvider *provider)
{
  if (!bz_content_provider_get_has_inputs (provider))
    adw_view_stack_set_visible_child_name (self->main_view_stack, "flathub");
}

static DexFuture *
transact_fiber (TransactData *data)
{
  g_autoptr (BzWindow) self             = NULL;
  g_autoptr (GError) local_error        = NULL;
  g_autoptr (BzEntry) selected_entry    = NULL;
  g_autoptr (DexFuture) transact_future = NULL;
  g_autofree char *id_dup               = NULL;
  BzMainConfig    *config               = NULL;
  GListModel      *hooks                = NULL;
  gboolean         delete_user_data     = FALSE;
  GdkDisplay      *display              = NULL;
  GdkSeat         *seat                 = NULL;
  GdkDevice       *keyboard             = NULL;
  GdkModifierType  modifiers            = GDK_NO_MODIFIER_MASK;

  bz_weak_get_or_return_reject (self, data->self);

  // Get ID early before any async operations
  if (data->group != NULL)
    id_dup = g_strdup (bz_entry_group_get_id (data->group));
  else
    id_dup = g_strdup (bz_entry_get_id (data->entry));

  /* Prevent Bazaar from being removed by itself */
  if (data->remove)
    {
      const char *bazaar_id = NULL;

      bazaar_id = g_application_get_application_id (g_application_get_default ());
      if (g_strcmp0 (id_dup, bazaar_id) == 0)
        {
          bz_show_error_for_widget (GTK_WIDGET (self), _ ("You can't remove Bazaar from Bazaar!"), _ ("You can't remove Bazaar from Bazaar!"));
          return dex_future_new_false ();
        }
    }

  config = bz_state_info_get_main_config (self->state);
  if (config != NULL)
    hooks = bz_main_config_get_hooks (config);

#define RUN_HOOK(_signal)                                               \
  G_STMT_START                                                          \
  {                                                                     \
    if (hooks != NULL &&                                                \
        !dex_await (                                                    \
            bz_run_hook_emission (                                      \
                hooks, (_signal),                                       \
                data->remove                                            \
                    ? BZ_HOOK_TRANSACTION_TYPE_REMOVAL                  \
                    : BZ_HOOK_TRANSACTION_TYPE_INSTALL,                 \
                id_dup, NULL),                                          \
            &local_error))                                              \
      return dex_future_new_for_error (g_steal_pointer (&local_error)); \
  }                                                                     \
  G_STMT_END

  RUN_HOOK (BZ_HOOK_SIGNAL_BEFORE_TRANSACTION);

  display  = gdk_display_get_default ();
  seat     = gdk_display_get_default_seat (display);
  keyboard = gdk_seat_get_keyboard (seat);
  if (keyboard != NULL)
    modifiers = gdk_device_get_modifier_state (keyboard);

  if (modifiers & GDK_SHIFT_MASK)
    /* Holding shift while invoking a transaction skips the dialog and assumes
       the first valid entry */
    {
      if (data->group != NULL)
        {
          g_autoptr (GListModel) store = NULL;
          guint n_items                = 0;

          store = dex_await_object (
              bz_entry_group_dup_all_into_store (data->group),
              &local_error);
          if (store == NULL)
            return dex_future_new_for_error (g_steal_pointer (&local_error));

          n_items = g_list_model_get_n_items (store);
          for (guint i = 0; i < n_items; i++)
            {
              g_autoptr (BzEntry) entry = NULL;
              gboolean installed        = FALSE;

              entry     = g_list_model_get_item (store, i);
              installed = bz_entry_is_installed (entry);
              if ((data->remove && installed) ||
                  (!data->remove && !installed))
                {
                  selected_entry = g_steal_pointer (&entry);
                  break;
                }
            }
          if (selected_entry == NULL)
            return dex_future_new_false ();
        }
      else
        selected_entry = g_object_ref (data->entry);
    }
  else
    {
      g_autoptr (BzTransactionDialogResult) dialog_result = NULL;

      // Show the dialog
      dialog_result = dex_await_object (
          bz_transaction_dialog_show (
              GTK_WIDGET (self),
              data->entry,
              data->group,
              data->remove,
              data->auto_confirm),
          &local_error);

      if (dialog_result == NULL)
        return dex_future_new_for_error (g_steal_pointer (&local_error));
      if (!bz_transaction_dialog_result_get_confirmed (dialog_result))
        return dex_future_new_false ();

      selected_entry = g_object_ref (
          bz_transaction_dialog_result_get_selected_entry (dialog_result));
      delete_user_data = bz_transaction_dialog_result_get_delete_user_data (
          dialog_result);
    }

  // Perform the transaction
  transact_future = transact (
      self,
      selected_entry,
      data->remove,
      data->source);

  if (!dex_await (g_steal_pointer (&transact_future), &local_error))
    return dex_future_new_for_error (g_steal_pointer (&local_error));

  // Handle user data deletion
  if (delete_user_data)
    {
      if (data->group != NULL)
        bz_entry_group_reap_user_data (data->group);
      else
        dex_future_disown (bz_reap_user_data_dex (id_dup));
    }

  RUN_HOOK (BZ_HOOK_SIGNAL_AFTER_TRANSACTION);
#undef RUN_HOOK

  return dex_future_new_true ();
}

BzWindow *
bz_window_new (BzStateInfo *state)
{
  BzWindow *window = NULL;

  g_return_val_if_fail (BZ_IS_STATE_INFO (state), NULL);

  window        = g_object_new (BZ_TYPE_WINDOW, NULL);
  window->state = g_object_ref (state);

  g_signal_connect_object (state,
                           "notify::busy",
                           G_CALLBACK (app_busy_changed),
                           window, G_CONNECT_SWAPPED);

  /* these seem unsafe but BzApplication never
   * changes the objects we are connecting to
   */
  g_signal_connect_object (bz_state_info_get_curated_provider (state),
                           "notify::has-inputs",
                           G_CALLBACK (has_inputs_changed),
                           window, G_CONNECT_SWAPPED);

  g_object_notify_by_pspec (G_OBJECT (window), props[PROP_STATE]);

  set_page (window);
  return window;
}

void
bz_window_search (BzWindow   *self,
                  const char *text)
{
  g_return_if_fail (BZ_IS_WINDOW (self));
  search (self, text);
}

void
bz_window_show_entry (BzWindow *self,
                      BzEntry  *entry)
{
  g_autoptr (BzEntryGroup) group = NULL;

  g_return_if_fail (BZ_IS_WINDOW (self));
  g_return_if_fail (BZ_IS_ENTRY (entry));

  group = bz_entry_group_new_for_single_entry (entry);
  bz_window_show_group (self, group);
}

void
bz_window_show_group (BzWindow     *self,
                      BzEntryGroup *group)
{
  GListModel *stack    = NULL;
  gboolean    in_stack = FALSE;

  g_return_if_fail (BZ_IS_WINDOW (self));
  g_return_if_fail (BZ_IS_ENTRY_GROUP (group));

  bz_full_view_set_entry_group (self->full_view, group);
  emit_hook_disown (self, BZ_HOOK_SIGNAL_VIEW_APP, group);
  stack = adw_navigation_view_get_navigation_stack (self->navigation_view);

  for (guint i = 0; i < g_list_model_get_n_items (stack); i++)
    {
      g_autoptr (AdwNavigationPage) page = NULL;
      page                               = g_list_model_get_item (stack, i);

      if (g_strcmp0 (adw_navigation_page_get_tag (page), "view") == 0)
        {
          in_stack = TRUE;
          adw_navigation_view_pop_to_page (self->navigation_view, page);
          break;
        }
    }
  if (!in_stack)
    adw_navigation_view_push_by_tag (self->navigation_view, "view");
}

void
bz_window_add_toast (BzWindow *self,
                     AdwToast *toast)
{
  g_return_if_fail (BZ_IS_WINDOW (self));
  g_return_if_fail (ADW_IS_TOAST (toast));

  adw_toast_overlay_add_toast (self->toasts, toast);
}

void
bz_window_push_page (BzWindow *self, AdwNavigationPage *page)
{
  g_return_if_fail (BZ_IS_WINDOW (self));
  g_return_if_fail (ADW_IS_NAVIGATION_PAGE (page));

  adw_navigation_view_push (self->navigation_view, page);
}

void
bz_window_bulk_install (BzWindow   *self,
                        GListModel *groups)
{
  g_autoptr (BulkInstallData) data = NULL;

  g_return_if_fail (BZ_IS_WINDOW (self));
  g_return_if_fail (G_IS_LIST_MODEL (groups));

  data         = bulk_install_data_new ();
  data->self   = bz_track_weak (self);
  data->groups = g_object_ref (groups);

  dex_future_disown (dex_scheduler_spawn (
      dex_scheduler_get_default (),
      bz_get_dex_stack_size (),
      (DexFiberFunc) bulk_install_fiber,
      bulk_install_data_ref (data),
      bulk_install_data_unref));
}

BzStateInfo *
bz_window_get_state_info (BzWindow *self)
{
  g_return_val_if_fail (BZ_IS_WINDOW (self), NULL);
  return self->state;
}

static DexFuture *
transact (BzWindow  *self,
          BzEntry   *entry,
          gboolean   remove,
          GtkWidget *source)
{
  g_autoptr (BzTransaction) transaction = NULL;

  if (remove)
    transaction = bz_transaction_new_full (
        NULL, 0,
        NULL, 0,
        &entry, 1);
  else
    transaction = bz_transaction_new_full (
        &entry, 1,
        NULL, 0,
        NULL, 0);

  return bz_transaction_manager_add (
      bz_state_info_get_transaction_manager (self->state),
      transaction);
}

static void
try_transact (BzWindow     *self,
              BzEntry      *entry,
              BzEntryGroup *group,
              gboolean      remove,
              gboolean      auto_confirm,
              GtkWidget    *source)
{
  g_autoptr (TransactData) data = NULL;

  g_return_if_fail (entry != NULL || group != NULL);
  if (bz_state_info_get_busy (self->state))
    {
      adw_toast_overlay_add_toast (
          self->toasts,
          adw_toast_new_format (_ ("Can't do that right now!")));
      return;
    }

  data               = transact_data_new ();
  data->self         = bz_track_weak (self);
  data->entry        = bz_object_maybe_ref (entry);
  data->group        = bz_object_maybe_ref (group);
  data->remove       = remove;
  data->auto_confirm = auto_confirm;
  data->source       = bz_object_maybe_ref (source);

  dex_future_disown (dex_scheduler_spawn (
      dex_scheduler_get_default (),
      bz_get_dex_stack_size (),
      (DexFiberFunc) transact_fiber,
      transact_data_ref (data), transact_data_unref));
}

static void
bulk_install (BzWindow *self,
              BzEntry **installs,
              guint     n_installs)
{
  g_autoptr (BzTransaction) transaction = NULL;

  g_return_if_fail (BZ_IS_WINDOW (self));
  g_return_if_fail (installs != NULL);
  g_return_if_fail (n_installs > 0);

  if (bz_state_info_get_busy (self->state))
    {
      adw_toast_overlay_add_toast (
          self->toasts,
          adw_toast_new_format (_ ("Can't do that right now!")));
      return;
    }

  transaction = bz_transaction_new_full (
      installs, n_installs,
      NULL, 0,
      NULL, 0);

  dex_future_disown (bz_transaction_manager_add (
      bz_state_info_get_transaction_manager (self->state),
      transaction));
}

static DexFuture *
bulk_install_fiber (BulkInstallData *data)
{
  g_autoptr (BzWindow) self                    = NULL;
  g_autoptr (GError) local_error               = NULL;
  g_autoptr (BzBulkInstallDialogResult) result = NULL;
  GListModel          *entries                 = NULL;
  guint                n_installs              = 0;
  g_autofree BzEntry **installs_buf            = NULL;

  bz_weak_get_or_return_reject (self, data->self);

  result = dex_await_object (
      bz_bulk_install_dialog_show (GTK_WIDGET (self), data->groups),
      &local_error);

  if (result == NULL)
    return dex_future_new_for_error (g_steal_pointer (&local_error));

  if (!bz_bulk_install_dialog_result_get_confirmed (result))
    return dex_future_new_false ();

  entries    = bz_bulk_install_dialog_result_get_entries (result);
  n_installs = g_list_model_get_n_items (entries);
  if (n_installs == 0)
    return dex_future_new_false ();

  installs_buf = g_malloc_n (n_installs, sizeof (*installs_buf));
  for (guint i = 0; i < n_installs; i++)
    installs_buf[i] = g_list_model_get_item (entries, i);

  bulk_install (self, installs_buf, n_installs);
  for (guint i = 0; i < n_installs; i++)
    g_object_unref (installs_buf[i]);

  return dex_future_new_true ();
}

static void
search (BzWindow   *self,
        const char *initial)
{
  if (initial != NULL && *initial != '\0')
    bz_search_page_set_text (self->search_page, initial);

  adw_view_stack_set_visible_child_name (self->main_view_stack, "search");
  adw_navigation_view_pop_to_tag (self->navigation_view, "main");
  gtk_widget_grab_focus (GTK_WIDGET (self->search_page));
}

static void
set_page (BzWindow *self)
{
  const char *selected_navigation_page_name = NULL;

  if (self->state == NULL)
    return;

  if (bz_state_info_get_busy (self->state))
    {
      gtk_stack_set_visible_child_name (self->main_stack, "loading");
      adw_navigation_view_pop_to_tag (self->navigation_view, "main");
    }
  else
    gtk_stack_set_visible_child_name (self->main_stack, "main");

  selected_navigation_page_name = adw_navigation_view_get_visible_page_tag (self->navigation_view);

  if (g_strcmp0 (selected_navigation_page_name, "view") != 0)
    bz_full_view_set_entry_group (self->full_view, NULL);
}

static void
emit_hook_disown (BzWindow     *self,
                  BzHookSignal  signal,
                  BzEntryGroup *group)
{
  BzMainConfig *config = NULL;
  GListModel   *hooks  = NULL;

  if (self->state == NULL)
    return;

  config = bz_state_info_get_main_config (self->state);
  if (config == NULL)
    return;

  hooks = bz_main_config_get_hooks (config);
  if (hooks == NULL)
    return;

  dex_future_disown (bz_run_hook_emission (
      hooks, signal, 0, NULL, group));
}
