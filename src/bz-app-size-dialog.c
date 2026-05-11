/* bz-app-size-dialog.c
 *
 * Copyright 2025 Adam Masciola, Alexander Vanhee
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

#include "bz-app-size-dialog.h"
#include "bz-entry-group.h"
#include "bz-io.h"
#include "bz-lozenge.h"
#include "bz-template-callbacks.h"

#include <glib/gi18n.h>

struct _BzAppSizeDialog
{
  AdwBin parent_instance;

  BzEntryGroup *group;
};

G_DEFINE_FINAL_TYPE (BzAppSizeDialog, bz_app_size_dialog, ADW_TYPE_BIN)

enum
{
  PROP_0,

  PROP_GROUP,

  LAST_PROP
};
static GParamSpec *props[LAST_PROP] = { 0 };

static void
bz_app_size_dialog_dispose (GObject *object)
{
  BzAppSizeDialog *self = BZ_APP_SIZE_DIALOG (object);

  g_clear_object (&self->group);

  G_OBJECT_CLASS (bz_app_size_dialog_parent_class)->dispose (object);
}

static void
bz_app_size_dialog_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  BzAppSizeDialog *self = BZ_APP_SIZE_DIALOG (object);

  switch (prop_id)
    {
    case PROP_GROUP:
      g_value_set_object (value, self->group);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_app_size_dialog_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  BzAppSizeDialog *self = BZ_APP_SIZE_DIALOG (object);

  switch (prop_id)
    {
    case PROP_GROUP:
      g_clear_object (&self->group);
      self->group = g_value_dup_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static char *
get_runtime_size_title (gpointer object,
                        gboolean runtime_installed)
{
  return g_strdup (runtime_installed ? _ ("Installed Runtime Size") : _ ("Runtime Download Size"));
}

static char *
format_size (gpointer object,
             guint64  value)
{
  g_autofree char *size_str = g_format_size (value);
  char            *space    = g_strrstr (size_str, "\xC2\xA0");

  if (space != NULL)
    {
      *space = '\0';
      return g_strdup_printf ("%s <span font_size='x-small'>%s</span>",
                              size_str, space + 2);
    }

  return g_strdup (size_str);
}

static void
open_user_data_folder_cb (GtkWidget       *widget,
                          BzAppSizeDialog *self)
{
  const char      *id                  = NULL;
  g_autofree char *path                = NULL;
  g_autoptr (GFile) file               = NULL;
  g_autoptr (GtkFileLauncher) launcher = NULL;
  GtkRoot *root                        = NULL;

  if (self->group == NULL)
    return;

  id = bz_entry_group_get_id (self->group);
  if (id == NULL)
    return;

  path     = bz_dup_user_data_path (id);
  file     = g_file_new_for_path (path);
  launcher = gtk_file_launcher_new (file);
  root     = gtk_widget_get_root (widget);

  gtk_file_launcher_launch (launcher, GTK_WINDOW (root), NULL, NULL, NULL);
}

static void
delete_cache_cb (GtkWidget       *widget,
                 BzAppSizeDialog *self)
{
  if (self->group == NULL)
    return;

  bz_entry_group_reap_user_cache (self->group);
}

static void
bz_app_size_dialog_class_init (BzAppSizeDialogClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose      = bz_app_size_dialog_dispose;
  object_class->get_property = bz_app_size_dialog_get_property;
  object_class->set_property = bz_app_size_dialog_set_property;

  props[PROP_GROUP] =
      g_param_spec_object (
          "group",
          NULL, NULL,
          BZ_TYPE_ENTRY_GROUP,
          G_PARAM_READWRITE);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  g_type_ensure (BZ_TYPE_LOZENGE);

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/kolunmi/Bazaar/bz-app-size-dialog.ui");
  bz_widget_class_bind_all_util_callbacks (widget_class);
  gtk_widget_class_bind_template_callback (widget_class, format_size);
  gtk_widget_class_bind_template_callback (widget_class, get_runtime_size_title);
  gtk_widget_class_bind_template_callback (widget_class, open_user_data_folder_cb);
  gtk_widget_class_bind_template_callback (widget_class, delete_cache_cb);
}

static void
bz_app_size_dialog_init (BzAppSizeDialog *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

AdwDialog *
bz_app_size_dialog_new (BzEntryGroup *group)
{
  BzAppSizeDialog *widget = NULL;
  AdwDialog       *dialog = NULL;

  widget = g_object_new (BZ_TYPE_APP_SIZE_DIALOG, "group", group, NULL);

  dialog = adw_dialog_new ();
  adw_dialog_set_content_height (dialog, 500);
  adw_dialog_set_content_width (dialog, 600);
  adw_dialog_set_child (dialog, GTK_WIDGET (widget));

  return dialog;
}

AdwNavigationPage *
bz_app_size_page_new (BzEntryGroup *group)
{
  BzAppSizeDialog   *widget = NULL;
  AdwNavigationPage *page   = NULL;

  widget = g_object_new (BZ_TYPE_APP_SIZE_DIALOG, "group", group, NULL);
  page   = adw_navigation_page_new (GTK_WIDGET (widget), _ ("App Size"));
  adw_navigation_page_set_tag (page, "app-size");

  return page;
}
