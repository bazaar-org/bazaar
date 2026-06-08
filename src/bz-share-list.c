/* bz-share-list.c
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

#include <glib/gi18n.h>

#include "bz-share-list.h"
#include "bz-url.h"
#include "bz-window.h"

struct _BzShareList
{
  GtkBox parent_instance;

  GListModel *urls;
  gboolean    dual_column;
};

G_DEFINE_FINAL_TYPE (BzShareList, bz_share_list, GTK_TYPE_BOX)

enum
{
  PROP_0,

  PROP_URLS,
  PROP_DUAL_COLUMN,

  LAST_PROP
};
static GParamSpec *props[LAST_PROP] = { 0 };

typedef struct
{
  const char *id;
  const char *display_name;
  const char *icon_name;
} UrlInfo;

static const UrlInfo url_info[] = {
  {     "flathub",    NC_ ("Project URL Type",    "Flathub Page"),       "flathub-symbolic" },
  {    "homepage",    NC_ ("Project URL Type", "Project Website"),         "globe-symbolic" },
  {  "bugtracker",    NC_ ("Project URL Type",   "Issue Tracker"), "computer-fail-symbolic" },
  {         "faq",    NC_ ("Project URL Type",             "FAQ"),      "help-faq-symbolic" },
  {        "help",    NC_ ("Project URL Type",            "Help"),  "help-browser-symbolic" },
  {    "donation",    NC_ ("Project URL Type",          "Donate"),  "heart-filled-symbolic" },
  {   "translate",    NC_ ("Project URL Type",       "Translate"),  "translations-symbolic" },
  {     "contact",    NC_ ("Project URL Type",         "Contact"),     "mail-send-symbolic" },
  { "vcs-browser",    NC_ ("Project URL Type",     "Source Code"),          "code-symbolic" },
  {  "contribute",    NC_ ("Project URL Type",      "Contribute"),  "system-users-symbolic" },
  {    "manifest",    NC_ ("Project URL Type",        "Manifest"),      "shoe-box-symbolic" },
  {          NULL,                         NULL,                                       NULL }
};

static void           populate_urls (BzShareList *self);
static GtkListBox    *create_list_box (void);
static AdwActionRow  *create_url_action_row (BzShareList *self,
                                             BzUrl       *url_item);
static void           copy_cb (BzShareList *self,
                               GtkButton   *button);
static void           follow_link_cb (BzShareList *self,
                                      GtkWidget   *widget);

static void
bz_share_list_dispose (GObject *object)
{
  BzShareList *self = BZ_SHARE_LIST (object);

  g_clear_object (&self->urls);

  G_OBJECT_CLASS (bz_share_list_parent_class)->dispose (object);
}

static void
bz_share_list_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  BzShareList *self = BZ_SHARE_LIST (object);

  switch (prop_id)
    {
    case PROP_URLS:
      g_value_set_object (value, self->urls);
      break;
    case PROP_DUAL_COLUMN:
      g_value_set_boolean (value, self->dual_column);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_share_list_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
  BzShareList *self = BZ_SHARE_LIST (object);

  switch (prop_id)
    {
    case PROP_URLS:
      g_clear_object (&self->urls);
      self->urls = g_value_dup_object (value);
      populate_urls (self);
      break;
    case PROP_DUAL_COLUMN:
      self->dual_column = g_value_get_boolean (value);
      populate_urls (self);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_share_list_class_init (BzShareListClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose      = bz_share_list_dispose;
  object_class->get_property = bz_share_list_get_property;
  object_class->set_property = bz_share_list_set_property;

  props[PROP_URLS] =
      g_param_spec_object (
          "urls",
          NULL, NULL,
          G_TYPE_LIST_MODEL,
          G_PARAM_READWRITE);

  props[PROP_DUAL_COLUMN] =
      g_param_spec_boolean (
          "dual-column",
          NULL, NULL,
          FALSE,
          G_PARAM_READWRITE);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  g_type_ensure (BZ_TYPE_URL);
}

static void
bz_share_list_init (BzShareList *self)
{
  gtk_orientable_set_orientation (GTK_ORIENTABLE (self), GTK_ORIENTATION_VERTICAL);
}

GtkWidget *
bz_share_list_new (void)
{
  return g_object_new (BZ_TYPE_SHARE_LIST, NULL);
}

static void
populate_urls (BzShareList *self)
{
  GtkWidget *child       = NULL;
  guint      n_items     = 0;
  guint      left_count  = 0;
  guint      right_count = 0;

  while ((child = gtk_widget_get_first_child (GTK_WIDGET (self))) != NULL)
    gtk_box_remove (GTK_BOX (self), child);

  if (!self->urls)
    return;

  n_items = g_list_model_get_n_items (self->urls);

  if (!self->dual_column)
    {
      GtkListBox *list_box = NULL;

      list_box = create_list_box ();
      gtk_widget_set_hexpand (GTK_WIDGET (list_box), TRUE);

      for (guint i = 0; i < n_items; i++)
        {
          g_autoptr (BzUrl) url_item = NULL;
          AdwActionRow *action_row   = NULL;

          url_item   = g_list_model_get_item (self->urls, i);
          action_row = create_url_action_row (self, url_item);
          gtk_list_box_append (list_box, GTK_WIDGET (action_row));
        }

      gtk_box_append (GTK_BOX (self), GTK_WIDGET (list_box));
    }
  else
    {
      GtkBox     *columns_box = NULL;
      GtkListBox *left_box    = NULL;
      GtkListBox *right_box   = NULL;

      left_count  = (n_items + 1) / 2;
      right_count = n_items / 2;

      columns_box = GTK_BOX (gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12));
      gtk_widget_set_hexpand (GTK_WIDGET (columns_box), TRUE);
      gtk_box_set_homogeneous (columns_box, TRUE);

      left_box = create_list_box ();
      gtk_widget_set_hexpand (GTK_WIDGET (left_box), TRUE);

      for (guint i = 0; i < left_count; i++)
        {
          g_autoptr (BzUrl) url_item = NULL;
          AdwActionRow *action_row   = NULL;

          url_item   = g_list_model_get_item (self->urls, i);
          action_row = create_url_action_row (self, url_item);
          gtk_list_box_append (left_box, GTK_WIDGET (action_row));
        }

      right_box = create_list_box ();
      gtk_widget_set_hexpand (GTK_WIDGET (right_box), TRUE);

      for (guint i = 0; i < right_count; i++)
        {
          g_autoptr (BzUrl) url_item = NULL;
          AdwActionRow *action_row   = NULL;

          url_item   = g_list_model_get_item (self->urls, left_count + i);
          action_row = create_url_action_row (self, url_item);
          gtk_list_box_append (right_box, GTK_WIDGET (action_row));
        }

      gtk_box_append (columns_box, GTK_WIDGET (left_box));
      gtk_box_append (columns_box, GTK_WIDGET (right_box));
      gtk_box_append (GTK_BOX (self), GTK_WIDGET (columns_box));
    }
}

static GtkListBox *
create_list_box (void)
{
  GtkListBox *list_box = NULL;

  list_box = GTK_LIST_BOX (gtk_list_box_new ());
  gtk_list_box_set_selection_mode (list_box, GTK_SELECTION_NONE);
  gtk_widget_add_css_class (GTK_WIDGET (list_box), "boxed-list");
  gtk_widget_set_valign (GTK_WIDGET (list_box), GTK_ALIGN_START);

  return list_box;
}

static AdwActionRow *
create_url_action_row (BzShareList *self,
                       BzUrl       *url_item)
{
  const char    *url_string  = NULL;
  const char    *id          = NULL;
  const char    *display     = NULL;
  AdwActionRow  *action_row  = NULL;
  GtkImage      *suffix_icon = NULL;
  GtkBox        *suffix_box  = NULL;
  GtkButton     *copy_button = NULL;
  GtkSeparator  *separator   = NULL;
  const UrlInfo *info        = NULL;

  url_string = bz_url_get_url (url_item);
  id         = bz_url_get_id (url_item);

  display = (g_str_has_prefix (url_string, "http://") ||
             g_str_has_prefix (url_string, "https://"))
                ? strstr (url_string, "://") + 3
                : url_string;

  for (int i = 0; url_info[i].id != NULL; i++)
    {
      if (g_strcmp0 (url_info[i].id, id) == 0)
        {
          info = &url_info[i];
          break;
        }
    }

  action_row = ADW_ACTION_ROW (adw_action_row_new ());
  adw_preferences_row_set_use_markup (ADW_PREFERENCES_ROW (action_row), FALSE);
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (action_row),
                                 info ? g_dpgettext2 (NULL, "Project URL Type", info->display_name)
                                      : url_string);
  adw_action_row_set_subtitle (action_row, display);
  adw_action_row_set_subtitle_lines (action_row, 1);
  gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (action_row), TRUE);

  g_object_set_data_full (G_OBJECT (action_row), "url", g_strdup (url_string), g_free);
  g_signal_connect_swapped (action_row, "activated",
                            G_CALLBACK (follow_link_cb), self);

  if (info != NULL && info->icon_name != NULL && info->icon_name[0] != '\0')
    {
      GtkImage *prefix_icon = NULL;

      prefix_icon = GTK_IMAGE (gtk_image_new_from_icon_name (info->icon_name));
      gtk_image_set_icon_size (prefix_icon, GTK_ICON_SIZE_NORMAL);
      adw_action_row_add_prefix (action_row, GTK_WIDGET (prefix_icon));
    }

  suffix_box = GTK_BOX (gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4));
  gtk_widget_set_valign (GTK_WIDGET (suffix_box), GTK_ALIGN_CENTER);

  copy_button = GTK_BUTTON (gtk_button_new_from_icon_name ("edit-copy-symbolic"));
  gtk_widget_set_tooltip_text (GTK_WIDGET (copy_button), _ ("Copy Link"));
  gtk_button_set_has_frame (copy_button, FALSE);
  g_object_set_data_full (G_OBJECT (copy_button), "url", g_strdup (url_string), g_free);
  g_signal_connect_swapped (copy_button, "clicked",
                            G_CALLBACK (copy_cb), self);

  separator = GTK_SEPARATOR (gtk_separator_new (GTK_ORIENTATION_VERTICAL));
  gtk_widget_set_margin_top (GTK_WIDGET (separator), 6);
  gtk_widget_set_margin_bottom (GTK_WIDGET (separator), 6);

  suffix_icon = GTK_IMAGE (gtk_image_new_from_icon_name ("external-link-symbolic"));
  gtk_image_set_icon_size (suffix_icon, GTK_ICON_SIZE_NORMAL);
  gtk_widget_set_margin_start (GTK_WIDGET (suffix_icon), 8);
  gtk_widget_set_margin_end (GTK_WIDGET (suffix_icon), 4);

  gtk_box_append (suffix_box, GTK_WIDGET (copy_button));
  gtk_box_append (suffix_box, GTK_WIDGET (separator));
  gtk_box_append (suffix_box, GTK_WIDGET (suffix_icon));

  adw_action_row_add_suffix (action_row, GTK_WIDGET (suffix_box));

  return action_row;
}

static void
copy_cb (BzShareList *self,
         GtkButton   *button)
{
  const char   *link      = NULL;
  GdkClipboard *clipboard = NULL;
  AdwToast     *toast     = NULL;
  GtkWidget    *ancestor  = NULL;

  link = g_object_get_data (G_OBJECT (button), "url");

  clipboard = gdk_display_get_clipboard (gdk_display_get_default ());
  gdk_clipboard_set_text (clipboard, link);

  toast = adw_toast_new (_ ("Copied!"));
  adw_toast_set_timeout (toast, 1);

  ancestor = gtk_widget_get_ancestor (GTK_WIDGET (self), ADW_TYPE_TOAST_OVERLAY);
  if (ancestor != NULL)
    {
      adw_toast_overlay_add_toast (ADW_TOAST_OVERLAY (ancestor), toast);
      return;
    }
}

static void
follow_link_cb (BzShareList *self,
                GtkWidget   *widget)
{
  const char *link = NULL;

  link = g_object_get_data (G_OBJECT (widget), "url");
  g_app_info_launch_default_for_uri (link, NULL, NULL);
}
