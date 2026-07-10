/* bz-releases-list.c
 *
 * Copyright 2025 Alexander Vanhee, Adam Masciola
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

#include <glib/gi18n.h>

#include "bz-appstream-description-render.h"
#include "bz-fading-clamp.h"
#include "bz-release.h"
#include "bz-releases-list.h"
#include "template-callbacks.h"

/* Dialog structure */
typedef struct
{
  /* Template widgets */
  AdwDialog   parent_instance;
  GtkListBox *releases_box;
  GListModel *installed_versions;
} BzReleasesDialog;

typedef struct
{
  AdwDialogClass parent_class;
} BzReleasesDialogClass;

static GType bz_releases_dialog_get_type (void) G_GNUC_CONST;
G_DEFINE_TYPE (BzReleasesDialog, bz_releases_dialog, ADW_TYPE_DIALOG)

/* Main widget structure */
struct _BzReleasesList
{
  AdwBin parent_instance;

  GListModel *version_history;
  GListModel *installed_versions;

  /* Template widgets */
  GtkListBox *preview_box;
  GtkBox     *show_all_box;
};

G_DEFINE_FINAL_TYPE (BzReleasesList, bz_releases_list, ADW_TYPE_BIN)

enum
{
  PROP_0,
  PROP_VERSION_HISTORY,
  PROP_INSTALLED_VERSIONS,
  LAST_PROP
};

static GParamSpec *props[LAST_PROP] = { 0 };

static gboolean
is_version_installed (GListModel *installed_versions, const char *version)
{
  guint n_items = 0;

  if (!installed_versions || !version)
    return FALSE;

  n_items = g_list_model_get_n_items (installed_versions);
  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr (GObject) item = NULL;

      item = g_list_model_get_item (installed_versions, i);
      if (GTK_IS_STRING_OBJECT (item))
        {
          const char *installed_version = NULL;

          installed_version = gtk_string_object_get_string (GTK_STRING_OBJECT (item));
          if (installed_version != NULL && g_strcmp0 (installed_version, version) == 0)
            return TRUE;
        }
    }

  return FALSE;
}

// From gs-common.c
static char *
format_timestamp (guint64 unix_time)
{
  g_autoptr (GDateTime) then = NULL;
  g_autoptr (GDateTime) now  = NULL;
  gint days, weeks, months, years;

  if (unix_time == 0)
    return NULL;

  then = g_date_time_new_from_unix_utc (unix_time);
  now  = g_date_time_new_now_local ();

  if (then == NULL || now == NULL)
    return NULL;

  days   = (gint) (g_date_time_difference (now, then) / G_TIME_SPAN_DAY);
  weeks  = days / 7;
  months = days / 30;
  years  = weeks / 52;

  if (days < 1)
    /* Translators: something happened less than a day ago */
    return g_strdup (_ ("Today"));
  else if (days < 2)
    /* Translators: something happened more than a day ago but less than 2 days ago */
    return g_strdup (_ ("Yesterday"));
  else if (days < 15)
    /* Translators: something happened days ago */
    return g_strdup_printf (ngettext ("%d day ago", "%d days ago", days), days);
  else if (weeks < 8)
    /* Translators: something happened weeks ago */
    return g_strdup_printf (ngettext ("%d week ago", "%d weeks ago", weeks), weeks);
  else if (years < 1)
    /* Translators: something happened months ago */
    return g_strdup_printf (ngettext ("%d month ago", "%d months ago", months), months);
  else
    /* Translators: something happened years ago */
    return g_strdup_printf (ngettext ("%d year ago", "%d years ago", years), years);
}

static char *
format_timestamp_tooltip (guint64 unix_time)
{
  g_autoptr (GDateTime) then = NULL;

  if (unix_time == 0)
    return NULL;

  then = g_date_time_new_from_unix_utc (unix_time);

  if (then == NULL)
    return NULL;

  /* TRANSLATORS: This is the date string with: day number, month name, year.
   i.e. "22 March 2026" */
  return g_date_time_format (then, _ ("%e %B %Y"));
}

static GtkWidget *
create_release_row (const char *version,
                    const char *description,
                    guint64     timestamp,
                    const char *url,
                    gboolean    use_clamp,
                    GListModel *installed_versions)
{
  AdwActionRow                 *row                = NULL;
  GtkBox                       *content_box        = NULL;
  GtkBox                       *header_box         = NULL;
  GtkLabel                     *version_label      = NULL;
  GtkLabel                     *date_label         = NULL;
  GtkLabel                     *installed_label    = NULL;
  BzAppstreamDescriptionRender *description_widget = NULL;
  BzFadingClamp                *fading_clamp       = NULL;
  GtkBox                       *more_info_box      = NULL;
  GtkLabel                     *more_info_label    = NULL;
  GtkImage                     *more_info_icon     = NULL;
  g_autofree char              *date_str           = NULL;
  g_autofree char              *date_tooltip       = NULL;
  g_autofree char              *version_text       = NULL;
  g_autofree char              *markup             = NULL;

  date_str = format_timestamp (timestamp);

  row = ADW_ACTION_ROW (adw_action_row_new ());
  gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), FALSE);

  content_box = GTK_BOX (gtk_box_new (GTK_ORIENTATION_VERTICAL, 3));
  gtk_widget_set_margin_top (GTK_WIDGET (content_box), 15);
  gtk_widget_set_margin_bottom (GTK_WIDGET (content_box), 15);
  gtk_widget_set_margin_start (GTK_WIDGET (content_box), 15);
  gtk_widget_set_margin_end (GTK_WIDGET (content_box), 15);

  header_box = GTK_BOX (gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8));
  gtk_widget_set_size_request (GTK_WIDGET (header_box), -1, 24);

  version_text  = g_strdup_printf (_ ("Version %s"), version);
  version_label = GTK_LABEL (gtk_label_new (version_text));
  gtk_widget_add_css_class (GTK_WIDGET (version_label), "accent");
  gtk_widget_add_css_class (GTK_WIDGET (version_label), "heading");
  gtk_label_set_ellipsize (version_label, PANGO_ELLIPSIZE_END);
  gtk_widget_set_halign (GTK_WIDGET (version_label), GTK_ALIGN_START);
  gtk_box_append (header_box, GTK_WIDGET (version_label));

  if (is_version_installed (installed_versions, version))
    {
      installed_label = GTK_LABEL (gtk_label_new (_ ("Installed")));
      gtk_widget_add_css_class (GTK_WIDGET (installed_label), "lozenge");
      gtk_widget_add_css_class (GTK_WIDGET (installed_label), "small");
      gtk_widget_set_halign (GTK_WIDGET (installed_label), GTK_ALIGN_START);
      gtk_widget_set_hexpand (GTK_WIDGET (installed_label), TRUE);
      gtk_box_append (header_box, GTK_WIDGET (installed_label));
    }
  else
    {
      gtk_widget_set_hexpand (GTK_WIDGET (version_label), TRUE);
    }

  date_label = GTK_LABEL (gtk_label_new (date_str ? date_str : ""));
  gtk_widget_add_css_class (GTK_WIDGET (date_label), "dim-label");
  gtk_widget_set_halign (GTK_WIDGET (date_label), GTK_ALIGN_END);
  date_tooltip = format_timestamp_tooltip (timestamp);
  if (date_tooltip != NULL)
    gtk_widget_set_tooltip_text (GTK_WIDGET (date_label), date_tooltip);
  gtk_box_append (header_box, GTK_WIDGET (date_label));

  gtk_box_append (content_box, GTK_WIDGET (header_box));

  if (description != NULL && *description)
    {
      description_widget = bz_appstream_description_render_new ();
      bz_appstream_description_render_set_appstream_description (description_widget, description);

      if (use_clamp)
        {
          fading_clamp = BZ_FADING_CLAMP (bz_fading_clamp_new ());
          bz_fading_clamp_set_max_height (fading_clamp, 270);
          bz_fading_clamp_set_child (fading_clamp, GTK_WIDGET (description_widget));
          gtk_widget_set_margin_top (GTK_WIDGET (fading_clamp), 10);
          gtk_box_append (content_box, GTK_WIDGET (fading_clamp));
        }
      else
        {
          gtk_widget_set_margin_top (GTK_WIDGET (description_widget), 10);
          gtk_box_append (content_box, GTK_WIDGET (description_widget));
        }
    }
  else
    {
      GtkLabel *fallback_label = NULL;

      fallback_label = GTK_LABEL (gtk_label_new (_ ("No details for this release")));
      gtk_widget_set_margin_top (GTK_WIDGET (fallback_label), 5);
      gtk_label_set_selectable (fallback_label, TRUE);
      gtk_widget_add_css_class (GTK_WIDGET (fallback_label), "dim-label");
      gtk_label_set_xalign (fallback_label, 0.0);
      gtk_label_set_wrap (fallback_label, TRUE);
      gtk_box_append (content_box, GTK_WIDGET (fallback_label));
    }

  if (!use_clamp && url && *url)
    {
      more_info_box = GTK_BOX (gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4));

      markup = g_markup_printf_escaped ("<a href=\"%s\" title=\"%s\">%s</a>", url, url, _ ("Get More Information"));
      more_info_label = GTK_LABEL (gtk_label_new (NULL));
      gtk_label_set_markup (more_info_label, markup);
      gtk_widget_set_tooltip_text (GTK_WIDGET (more_info_label), url);
      gtk_box_append (more_info_box, GTK_WIDGET (more_info_label));

      more_info_icon = GTK_IMAGE (gtk_image_new_from_icon_name ("external-link-symbolic"));
      gtk_image_set_pixel_size (more_info_icon, 12);
      gtk_widget_add_css_class (GTK_WIDGET (more_info_icon), "accent");
      gtk_box_append (more_info_box, GTK_WIDGET (more_info_icon));

      gtk_box_append (content_box, GTK_WIDGET (more_info_box));
    }

  gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (row), GTK_WIDGET (content_box));

  return GTK_WIDGET (row);
}

static void
bz_releases_dialog_dispose (GObject *object)
{
  BzReleasesDialog *self = (BzReleasesDialog *) object;

  g_clear_object (&self->installed_versions);
  G_OBJECT_CLASS (bz_releases_dialog_parent_class)->dispose (object);
}

static void
bz_releases_dialog_class_init (BzReleasesDialogClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = bz_releases_dialog_dispose;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/io/github/kolunmi/Bazaar/bz-releases-dialog.ui");
  gtk_widget_class_bind_template_child (widget_class, BzReleasesDialog, releases_box);
}

static void
bz_releases_dialog_init (BzReleasesDialog *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

GtkWidget *
bz_releases_dialog_new (GListModel *version_history,
                        GListModel *installed_versions)
{
  BzReleasesDialog *dialog  = NULL;
  guint             n_items = 0;

  dialog = g_object_new (bz_releases_dialog_get_type (), NULL);

  if (installed_versions)
    dialog->installed_versions = g_object_ref (installed_versions);

  if (version_history == NULL)
    return GTK_WIDGET (dialog);

  n_items = g_list_model_get_n_items (version_history);
  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr (BzRelease) release = NULL;
      const char *version           = NULL;
      const char *description       = NULL;
      const char *url               = NULL;
      guint64     timestamp         = 0;
      GtkWidget  *row               = NULL;

      release = g_list_model_get_item (version_history, i);
      if (release == NULL)
        continue;

      version     = bz_release_get_version (release);
      description = bz_release_get_description (release);
      url         = bz_release_get_url (release);
      timestamp   = bz_release_get_timestamp (release);

      row = create_release_row (version, description, timestamp, url, FALSE, dialog->installed_versions);
      gtk_list_box_append (dialog->releases_box, row);
    }

  return GTK_WIDGET (dialog);
}

static void
clear_preview_box (BzReleasesList *self)
{
  GtkWidget *child = NULL;

  g_return_if_fail (BZ_IS_RELEASES_LIST (self));

  while ((child = gtk_widget_get_first_child (GTK_WIDGET (self->preview_box))) != NULL)
    {
      if (child == GTK_WIDGET (self->show_all_box))
        break;
      gtk_list_box_remove (self->preview_box, child);
    }
}

static void
populate_preview_box (BzReleasesList *self)
{
  guint n_items = 0;

  g_return_if_fail (BZ_IS_RELEASES_LIST (self));

  clear_preview_box (self);

  if (self->version_history == NULL)
    {
      gtk_widget_set_visible (GTK_WIDGET (self->show_all_box), FALSE);
      return;
    }

  n_items = g_list_model_get_n_items (self->version_history);

  if (n_items == 0)
    {
      gtk_widget_set_visible (GTK_WIDGET (self->show_all_box), FALSE);
      return;
    }

  if (n_items > 0)
    {
      g_autoptr (BzRelease) release = NULL;
      const char *version           = NULL;
      const char *description       = NULL;
      guint64     timestamp         = 0;
      GtkWidget  *row               = NULL;

      release = g_list_model_get_item (self->version_history, 0);
      if (release != NULL)
        {
          version     = bz_release_get_version (release);
          description = bz_release_get_description (release);
          timestamp   = bz_release_get_timestamp (release);

          row = create_release_row (version, description, timestamp, NULL, TRUE, self->installed_versions);
          gtk_list_box_insert (self->preview_box, row, 0);
        }
    }

  gtk_widget_set_visible (GTK_WIDGET (self->show_all_box), n_items > 0);
}

static void
show_all_releases_cb (AdwButtonRow   *button,
                      BzReleasesList *self)
{
  GtkWidget *dialog = NULL;
  GtkRoot   *root   = NULL;

  g_return_if_fail (BZ_IS_RELEASES_LIST (self));

  root = gtk_widget_get_root (GTK_WIDGET (self));
  if (root == NULL)
    return;

  dialog = bz_releases_dialog_new (self->version_history, self->installed_versions);
  adw_dialog_present (ADW_DIALOG (dialog), GTK_WIDGET (root));
}

static void
bz_releases_list_dispose (GObject *object)
{
  BzReleasesList *self = BZ_RELEASES_LIST (object);

  g_clear_object (&self->version_history);
  g_clear_object (&self->installed_versions);

  G_OBJECT_CLASS (bz_releases_list_parent_class)->dispose (object);
}

static void
bz_releases_list_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  BzReleasesList *self = BZ_RELEASES_LIST (object);

  switch (prop_id)
    {
    case PROP_VERSION_HISTORY:
      g_value_set_object (value, self->version_history);
      break;
    case PROP_INSTALLED_VERSIONS:
      g_value_set_object (value, self->installed_versions);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_releases_list_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  BzReleasesList *self = BZ_RELEASES_LIST (object);

  switch (prop_id)
    {
    case PROP_VERSION_HISTORY:
      bz_releases_list_set_version_history (self, g_value_get_object (value));
      break;
    case PROP_INSTALLED_VERSIONS:
      g_clear_object (&self->installed_versions);
      self->installed_versions = g_value_dup_object (value);
      populate_preview_box (self);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_releases_list_class_init (BzReleasesListClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose      = bz_releases_list_dispose;
  object_class->get_property = bz_releases_list_get_property;
  object_class->set_property = bz_releases_list_set_property;

  props[PROP_VERSION_HISTORY] =
      g_param_spec_object ("version-history",
                           NULL,
                           NULL,
                           G_TYPE_LIST_MODEL,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  props[PROP_INSTALLED_VERSIONS] =
      g_param_spec_object ("installed-versions",
                           NULL,
                           NULL,
                           G_TYPE_LIST_MODEL,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  g_type_ensure (BZ_TYPE_RELEASE);
  g_type_ensure (BZ_TYPE_APPSTREAM_DESCRIPTION_RENDER);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/io/github/kolunmi/Bazaar/bz-releases-list.ui");
  bz_widget_class_bind_all_util_callbacks (widget_class);
  gtk_widget_class_bind_template_child (widget_class, BzReleasesList, preview_box);
  gtk_widget_class_bind_template_child (widget_class, BzReleasesList, show_all_box);
  gtk_widget_class_bind_template_callback (widget_class, show_all_releases_cb);
}

static void
bz_releases_list_init (BzReleasesList *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

GtkWidget *
bz_releases_list_new (void)
{
  return g_object_new (BZ_TYPE_RELEASES_LIST, NULL);
}

void
bz_releases_list_set_version_history (BzReleasesList *self,
                                      GListModel     *version_history)
{
  g_return_if_fail (BZ_IS_RELEASES_LIST (self));
  g_return_if_fail (version_history == NULL || G_IS_LIST_MODEL (version_history));

  if (self->version_history == version_history)
    return;

  g_clear_object (&self->version_history);

  if (version_history != NULL)
    {
      self->version_history = g_object_ref (version_history);
      populate_preview_box (self);
      gtk_widget_set_visible (GTK_WIDGET (self),
                              g_list_model_get_n_items (version_history) > 0);
    }
  else
    {
      clear_preview_box (self);
      gtk_widget_set_visible (GTK_WIDGET (self), FALSE);
    }

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_VERSION_HISTORY]);
}

GListModel *
bz_releases_list_get_version_history (BzReleasesList *self)
{
  g_return_val_if_fail (BZ_IS_RELEASES_LIST (self), NULL);
  return self->version_history;
}
