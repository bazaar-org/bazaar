/* bz-inspector.c
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

#define G_LOG_DOMAIN "BAZAAR::INSPECTOR"

#include <json-glib/json-glib.h>

#include "bz-entry-inspector.h"
#include "bz-env.h"
#include "bz-inspector.h"
#include "bz-serializable.h"
#include "bz-template-callbacks.h"
#include "bz-window.h"

struct _BzInspector
{
  AdwWindow parent_instance;

  BzStateInfo *state;

  GBinding  *debug_mode_binding;
  GBinding  *disable_blocklists_binding;
  GtkWindow *preview_window;

  GtkCheckButton     *debug_mode_check;
  GtkCheckButton     *disable_blocklists_check;
  GtkEditable        *serialize_all_entries_path_entry;
  GtkButton          *serialize_all_entries_btn;
  GtkProgressBar     *serialize_all_entries_progress;
  GtkEditable        *search_entry;
  GtkFilterListModel *filter_model;
  GtkSingleSelection *groups_selection;
};

G_DEFINE_FINAL_TYPE (BzInspector, bz_inspector, ADW_TYPE_WINDOW);

enum
{
  PROP_0,

  PROP_STATE,

  LAST_PROP
};
static GParamSpec *props[LAST_PROP] = { 0 };

static DexFuture *
serialize_all_entries_fiber (BzInspector *self);

static gboolean
filter_func (BzEntryGroup *group,
             BzInspector  *self);

static void
bz_inspector_dispose (GObject *object)
{
  BzInspector *self = BZ_INSPECTOR (object);

  g_clear_pointer (&self->state, g_object_unref);

  g_clear_object (&self->debug_mode_binding);
  g_clear_object (&self->disable_blocklists_binding);
  if (self->preview_window != NULL)
    gtk_window_close (self->preview_window);
  g_clear_object (&self->preview_window);

  G_OBJECT_CLASS (bz_inspector_parent_class)->dispose (object);
}

static void
bz_inspector_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
  BzInspector *self = BZ_INSPECTOR (object);

  switch (prop_id)
    {
    case PROP_STATE:
      g_value_set_object (value, bz_inspector_get_state (self));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_inspector_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  BzInspector *self = BZ_INSPECTOR (object);

  switch (prop_id)
    {
    case PROP_STATE:
      bz_inspector_set_state (self, g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
serialize_all_entries_cb (BzInspector *self,
                          GtkButton   *button)
{
  dex_future_disown (
      dex_scheduler_spawn (
          dex_scheduler_get_default (),
          bz_get_dex_stack_size (),
          (DexFiberFunc) serialize_all_entries_fiber,
          g_object_ref (self), g_object_unref));
}

static void
preview_changed (BzInspector    *self,
                 GParamSpec     *pspec,
                 GtkCheckButton *button)
{
  if (gtk_check_button_get_active (button))
    {
      BzWindow     *window   = NULL;
      BzEntryGroup *selected = NULL;

      g_assert (self->preview_window == NULL);

      window = bz_window_new (self->state);
      gtk_window_set_default_size (GTK_WINDOW (window), 750, 750);
      gtk_window_present (GTK_WINDOW (window));

      selected = gtk_single_selection_get_selected_item (self->groups_selection);
      if (selected != NULL)
        bz_window_show_group (window, selected);

      self->preview_window = (GtkWindow *) g_object_ref_sink (window);
    }
  else
    {
      if (self->preview_window != NULL)
        gtk_window_close (self->preview_window);
      g_clear_object (&self->preview_window);
    }
}

static void
selected_group_changed (BzInspector        *self,
                        GParamSpec         *pspec,
                        GtkSingleSelection *selection)
{
  BzEntryGroup *group = NULL;

  if (self->preview_window == NULL ||
      !gtk_widget_get_mapped (GTK_WIDGET (self->preview_window)))
    return;

  group = gtk_single_selection_get_selected_item (self->groups_selection);
  if (group != NULL)
    bz_window_show_group (BZ_WINDOW (self->preview_window), group);
}

static void
entry_changed (BzInspector *self,
               GtkEditable *editable)
{
  GtkFilter *filter = NULL;

  filter = gtk_filter_list_model_get_filter (self->filter_model);
  gtk_filter_changed (filter, GTK_FILTER_CHANGE_DIFFERENT);
}

static void
decache_and_inspect_cb (GtkListItem *list_item,
                        GtkButton   *button)
{
  GtkStringObject *item       = NULL;
  BzInspector     *self       = NULL;
  g_autoptr (BzResult) result = NULL;

  item = gtk_list_item_get_item (list_item);

  self = BZ_INSPECTOR (gtk_widget_get_ancestor (GTK_WIDGET (button), BZ_TYPE_INSPECTOR));
  g_assert (self != NULL);

  result = bz_application_map_factory_convert_one (
      bz_state_info_get_entry_factory (self->state),
      g_object_ref (item));
  if (result != NULL)
    {
      BzEntryInspector *inspector = NULL;

      inspector = bz_entry_inspector_new ();
      bz_entry_inspector_set_result (inspector, result);

      gtk_window_present (GTK_WINDOW (inspector));
    }
}

static void
copy_unique_id_cb (GtkListItem *list_item,
                   GtkButton   *button)
{
  GtkStringObject *item      = NULL;
  const char      *text      = NULL;
  GdkClipboard    *clipboard = NULL;

  item = gtk_list_item_get_item (list_item);
  text = gtk_string_object_get_string (item);

  clipboard = gdk_display_get_clipboard (gdk_display_get_default ());
  gdk_clipboard_set_text (clipboard, text);
}

static void
open_file_externally_cb (GtkListItem *list_item,
                         GtkButton   *button)
{
  GtkStringObject *string = NULL;
  const char      *path   = NULL;
  g_autofree char *uri    = NULL;

  string = gtk_list_item_get_item (list_item);
  path   = gtk_string_object_get_string (string);

  uri = g_strdup_printf ("file://%s", path);
  g_app_info_launch_default_for_uri (uri, NULL, NULL);
}

static void
bz_inspector_class_init (BzInspectorClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->set_property = bz_inspector_set_property;
  object_class->get_property = bz_inspector_get_property;
  object_class->dispose      = bz_inspector_dispose;

  props[PROP_STATE] =
      g_param_spec_object (
          "state",
          NULL, NULL,
          BZ_TYPE_STATE_INFO,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/kolunmi/Bazaar/bz-inspector.ui");
  bz_widget_class_bind_all_util_callbacks (widget_class);

  gtk_widget_class_bind_template_child (widget_class, BzInspector, debug_mode_check);
  gtk_widget_class_bind_template_child (widget_class, BzInspector, disable_blocklists_check);
  gtk_widget_class_bind_template_child (widget_class, BzInspector, serialize_all_entries_path_entry);
  gtk_widget_class_bind_template_child (widget_class, BzInspector, serialize_all_entries_btn);
  gtk_widget_class_bind_template_child (widget_class, BzInspector, serialize_all_entries_progress);
  gtk_widget_class_bind_template_child (widget_class, BzInspector, search_entry);
  gtk_widget_class_bind_template_child (widget_class, BzInspector, filter_model);
  gtk_widget_class_bind_template_child (widget_class, BzInspector, groups_selection);
  gtk_widget_class_bind_template_callback (widget_class, serialize_all_entries_cb);
  gtk_widget_class_bind_template_callback (widget_class, preview_changed);
  gtk_widget_class_bind_template_callback (widget_class, selected_group_changed);
  gtk_widget_class_bind_template_callback (widget_class, decache_and_inspect_cb);
  gtk_widget_class_bind_template_callback (widget_class, copy_unique_id_cb);
  gtk_widget_class_bind_template_callback (widget_class, open_file_externally_cb);
  gtk_widget_class_bind_template_callback (widget_class, entry_changed);
}

static void
on_map (BzInspector *self,
        GtkWidget   *widget)
{
  gtk_widget_grab_focus (GTK_WIDGET (self->search_entry));
}

static void
bz_inspector_init (BzInspector *self)
{
  GtkCustomFilter *filter                       = NULL;
  g_autofree char *serialize_all_entries_output = NULL;

  gtk_widget_init_template (GTK_WIDGET (self));

  filter = gtk_custom_filter_new ((GtkCustomFilterFunc) filter_func, self, NULL);
  gtk_filter_list_model_set_filter (self->filter_model, GTK_FILTER (filter));

  g_signal_connect_swapped (self, "map", G_CALLBACK (on_map), self);

  serialize_all_entries_output = g_build_filename (
      g_get_home_dir (),
      "BAZAAR_SERIALIZED_ENTRIES.TXT",
      NULL);
  gtk_editable_set_text (
      self->serialize_all_entries_path_entry,
      serialize_all_entries_output);
}

BzInspector *
bz_inspector_new (void)
{
  return g_object_new (BZ_TYPE_INSPECTOR, NULL);
}

BzStateInfo *
bz_inspector_get_state (BzInspector *self)
{
  g_return_val_if_fail (BZ_IS_INSPECTOR (self), NULL);
  return self->state;
}

void
bz_inspector_set_state (BzInspector *self,
                        BzStateInfo *state)
{
  g_return_if_fail (BZ_IS_INSPECTOR (self));

  g_clear_pointer (&self->state, g_object_unref);
  g_clear_pointer (&self->debug_mode_binding, g_object_unref);
  g_clear_pointer (&self->disable_blocklists_binding, g_object_unref);

  if (state != NULL)
    {
      self->state              = g_object_ref (state);
      self->debug_mode_binding = g_object_bind_property (
          state, "debug-mode",
          self->debug_mode_check, "active",
          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
      self->disable_blocklists_binding = g_object_bind_property (
          state, "disable-blocklists",
          self->disable_blocklists_check, "active",
          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
    }

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_STATE]);
}

static DexFuture *
serialize_all_entries_fiber (BzInspector *self)
{
  g_autoptr (GError) local_error        = NULL;
  g_autoptr (BzEntryCacheManager) cache = NULL;
  const char      *path                 = NULL;
  g_autofree char *path_clone           = NULL;
  g_autoptr (GFile) file                = NULL;
  g_autoptr (GHashTable) cached_set     = NULL;
  guint cached_set_size                 = 0;
  g_autoptr (GFileOutputStream) output  = NULL;
  GHashTableIter iter                   = { 0 };

  if (self->state == NULL)
    return dex_future_new_false ();
  cache = bz_state_info_get_cache_manager (self->state);
  if (cache == NULL)
    return dex_future_new_false ();
  g_object_ref (cache);

  path = gtk_editable_get_text (self->serialize_all_entries_path_entry);
  if (path == NULL || *path == '\0')
    return dex_future_new_false ();
  path_clone = g_strdup (path);
  file       = g_file_new_for_path (path_clone);

  gtk_widget_set_sensitive (GTK_WIDGET (self->serialize_all_entries_btn), FALSE);
  gtk_widget_set_visible (GTK_WIDGET (self->serialize_all_entries_progress), TRUE);
  gtk_progress_bar_set_fraction (self->serialize_all_entries_progress, 0.0);

  cached_set = dex_await_boxed (
      bz_entry_cache_manager_enumerate_disk (
          bz_state_info_get_cache_manager (self->state)),
      &local_error);
  if (cached_set == NULL)
    goto err;

  cached_set_size = g_hash_table_size (cached_set);

  output = dex_await_object (
      dex_file_replace (
          file, NULL, TRUE,
          G_FILE_CREATE_REPLACE_DESTINATION,
          G_PRIORITY_DEFAULT),
      &local_error);
  if (output == NULL)
    goto err;

#define WRITE_STRING(_string)         \
  G_STMT_START                        \
  {                                   \
    dex_await (                       \
        dex_output_stream_write (     \
            G_OUTPUT_STREAM (output), \
            (_string),                \
            strlen ((_string)),       \
            G_PRIORITY_DEFAULT),      \
        &local_error);                \
    if (local_error != NULL)          \
      goto err;                       \
  }                                   \
  G_STMT_END

  WRITE_STRING ("[\n");

  g_hash_table_iter_init (&iter, cached_set);
  for (guint i = 1;; i++)
    {
      char *checksum                      = NULL;
      g_autoptr (BzEntry) entry           = NULL;
      g_autoptr (GVariantBuilder) builder = NULL;
      g_autoptr (GVariant) variant        = NULL;
      g_autoptr (JsonNode) node           = NULL;
      g_autoptr (JsonGenerator) generator = NULL;
      gsize            length             = 0;
      g_autofree char *string             = NULL;

      if (!g_hash_table_iter_next (
              &iter, (gpointer *) &checksum, NULL))
        break;

      if (i > 1)
        WRITE_STRING (",\n\n");

      entry = dex_await_object (
          bz_entry_cache_manager_get_by_checksum (
              cache, checksum),
          &local_error);
      if (entry == NULL)
        goto err;

      builder = g_variant_builder_new (G_VARIANT_TYPE_VARDICT);
      bz_serializable_serialize (BZ_SERIALIZABLE (entry), builder);
      variant = g_variant_builder_end (builder);

      node = json_gvariant_serialize (variant);

      generator = json_generator_new ();
      json_generator_set_pretty (generator, TRUE);
      json_generator_set_root (generator, node);

      string = json_generator_to_data (generator, &length);
      dex_await (
          dex_output_stream_write (
              G_OUTPUT_STREAM (output),
              string,
              length,
              G_PRIORITY_DEFAULT),
          &local_error);
      if (local_error != NULL)
        goto err;

      gtk_progress_bar_set_fraction (
          self->serialize_all_entries_progress,
          (double) i / (double) cached_set_size);
    }

  WRITE_STRING ("\n]\n");
  dex_await (
      dex_output_stream_close (
          G_OUTPUT_STREAM (output),
          G_PRIORITY_DEFAULT),
      NULL);

  gtk_widget_set_sensitive (GTK_WIDGET (self->serialize_all_entries_btn), TRUE);
  gtk_widget_set_visible (GTK_WIDGET (self->serialize_all_entries_progress), FALSE);
  return dex_future_new_true ();

err:
  if (local_error != NULL)
    g_warning ("Failed to serialize: %s", local_error->message);
  gtk_widget_set_sensitive (GTK_WIDGET (self->serialize_all_entries_btn), TRUE);
  gtk_widget_set_visible (GTK_WIDGET (self->serialize_all_entries_progress), FALSE);
  return dex_future_new_for_error (g_steal_pointer (&local_error));
}

static gboolean
filter_func (BzEntryGroup *group,
             BzInspector  *self)
{
  const char *text  = NULL;
  const char *id    = NULL;
  const char *title = NULL;

  text  = gtk_editable_get_text (self->search_entry);
  id    = bz_entry_group_get_id (group);
  title = bz_entry_group_get_title (group);

  if (text == NULL || *text == '\0')
    return TRUE;

  if (strcasestr (id, text) != NULL)
    return TRUE;
  if (strcasestr (title, text) != NULL)
    return TRUE;

  return FALSE;
}

/* End of bz-inspector.c */
