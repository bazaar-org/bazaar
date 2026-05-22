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

#include "bz-share-list.h"
#include "bz-url.h"
#include "bz-window.h"
#include <glib/gi18n.h>

struct _BzShareList
{
  GtkBox parent_instance;

  GListModel *urls;

  AdwPreferencesGroup *group;
};

G_DEFINE_FINAL_TYPE (BzShareList, bz_share_list, GTK_TYPE_BOX)

enum
{
  PROP_0,

  PROP_URLS,

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
  {          NULL,                         NULL,                                       NULL }
};

static const UrlInfo *
get_url_info (const char *id)
{
  for (int i = 0; url_info[i].id != NULL; i++)
    {
      if (g_strcmp0 (url_info[i].id, id) == 0)
        return &url_info[i];
    }
  return NULL;
}

static void
copy_cb (BzShareList *self,
         GtkButton   *button)
{
  const char   *link      = NULL;
  GdkClipboard *clipboard = NULL;
  AdwToast     *toast     = NULL;
  GtkWidget    *ancestor  = NULL;
  GtkRoot      *root      = NULL;

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

  root = gtk_widget_get_root (GTK_WIDGET (self));
  if (root != NULL && BZ_IS_WINDOW (root))
    bz_window_add_toast (BZ_WINDOW (root), toast);
}

static void
follow_link_cb (BzShareList *self,
                GtkButton   *button)
{
  const char *link = NULL;

  link = g_object_get_data (G_OBJECT (button), "url");
  g_app_info_launch_default_for_uri (link, NULL, NULL);
}

static AdwActionRow *
create_url_action_row (BzShareList *self, BzUrl *url_item)
{
  const char    *url_string = NULL;
  const char    *id         = NULL;
  const UrlInfo *info       = NULL;
  AdwActionRow  *action_row;
  GtkBox        *suffix_box;
  GtkButton     *copy_button;
  GtkButton     *open_button;
  GtkSeparator  *separator;

  url_string = bz_url_get_url (url_item);
  id         = bz_url_get_id (url_item);

  info       = get_url_info (id);
  action_row = ADW_ACTION_ROW (adw_action_row_new ());
  adw_preferences_row_set_use_markup (ADW_PREFERENCES_ROW (action_row), FALSE);
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (action_row),
                                 info ? g_dpgettext2 (NULL, "Project URL Type", info->display_name)
                                      : url_string);
  adw_action_row_set_subtitle (action_row, url_string);

  if (info != NULL && info->icon_name != NULL && info->icon_name[0] != '\0')
    {
      GtkImage *prefix_icon = GTK_IMAGE (gtk_image_new_from_icon_name (info->icon_name));
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

  open_button = GTK_BUTTON (gtk_button_new_from_icon_name ("external-link-symbolic"));
  gtk_widget_set_tooltip_text (GTK_WIDGET (open_button), _ ("Open Link"));
  gtk_button_set_has_frame (open_button, FALSE);
  g_object_set_data_full (G_OBJECT (open_button), "url", g_strdup (url_string), g_free);
  g_signal_connect_swapped (open_button, "clicked",
                            G_CALLBACK (follow_link_cb), self);

  gtk_box_append (suffix_box, GTK_WIDGET (copy_button));
  gtk_box_append (suffix_box, GTK_WIDGET (separator));
  gtk_box_append (suffix_box, GTK_WIDGET (open_button));

  adw_action_row_add_suffix (action_row, GTK_WIDGET (suffix_box));
  adw_action_row_set_activatable_widget (action_row, GTK_WIDGET (open_button));
  adw_action_row_set_subtitle_lines (action_row, 1);

  return action_row;
}

static void
populate_urls (BzShareList *self)
{
  guint n_items = 0;

  if (self->group)
    {
      gtk_box_remove (GTK_BOX (self), GTK_WIDGET (self->group));
      self->group = NULL;
    }

  self->group = ADW_PREFERENCES_GROUP (adw_preferences_group_new ());
  gtk_box_append (GTK_BOX (self), GTK_WIDGET (self->group));

  if (!self->urls)
    return;

  n_items = g_list_model_get_n_items (self->urls);

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr (BzUrl) url_item = NULL;
      AdwActionRow *action_row;

      url_item   = g_list_model_get_item (self->urls, i);
      action_row = create_url_action_row (self, url_item);
      adw_preferences_group_add (self->group, GTK_WIDGET (action_row));
    }
}

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
