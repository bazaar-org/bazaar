/* bz-donations-dialog.c
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

#include "config.h"

#include <glib/gi18n.h>
#include <xmlb.h>

#include "bz-appstream-description-render.h"
#include "bz-donations-dialog.h"
#include "template-callbacks.h"

struct _BzDonationsDialog
{
  AdwDialog parent_instance;

  char          *release_notes;
  char          *release_url;
  AdwBreakpoint *breakpoint;

  /* Template widgets */
  GtkLabel  *title;
  GtkLabel  *subtitle;
  GtkButton *release_button;
  GtkButton *donate_button;
};

G_DEFINE_FINAL_TYPE (BzDonationsDialog, bz_donations_dialog, ADW_TYPE_DIALOG);

enum
{
  PROP_0,

  PROP_RELEASE_NOTES,

  LAST_PROP
};
static GParamSpec *props[LAST_PROP] = { 0 };

static const char *bz_donations_dialog_get_release_notes (BzDonationsDialog *self);

static void
bz_donations_dialog_dispose (GObject *object)
{
  BzDonationsDialog *self = BZ_DONATIONS_DIALOG (object);

  g_clear_pointer (&self->release_notes, g_free);
  g_clear_pointer (&self->release_url, g_free);
  g_clear_object (&self->breakpoint);

  G_OBJECT_CLASS (bz_donations_dialog_parent_class)->dispose (object);
}

static void
bz_donations_dialog_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  BzDonationsDialog *self = BZ_DONATIONS_DIALOG (object);

  switch (prop_id)
    {
    case PROP_RELEASE_NOTES:
      g_value_set_string (value, bz_donations_dialog_get_release_notes (self));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
donate_clicked (BzDonationsDialog *self,
                GtkButton         *button)
{
  g_app_info_launch_default_for_uri (
      DONATE_LINK, NULL, NULL);
}

static void
release_page_clicked (BzDonationsDialog *self,
                      GtkButton         *button)
{
  if (self->release_url != NULL)
    g_app_info_launch_default_for_uri (self->release_url, NULL, NULL);
}

static void
on_map (BzDonationsDialog *self,
        gpointer           user_data)
{
  GtkRoot *root = NULL;

  root = gtk_widget_get_root (GTK_WIDGET (self));

  if (root == NULL || self->breakpoint == NULL)
    return;

  adw_application_window_add_breakpoint (ADW_APPLICATION_WINDOW (root), g_object_ref (self->breakpoint));
}

static void
bz_donations_dialog_class_init (BzDonationsDialogClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->get_property = bz_donations_dialog_get_property;
  object_class->dispose      = bz_donations_dialog_dispose;

  props[PROP_RELEASE_NOTES] =
      g_param_spec_string (
          "release-notes",
          NULL, NULL,
          NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  g_type_ensure (BZ_TYPE_APPSTREAM_DESCRIPTION_RENDER);

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/kolunmi/Bazaar/bz-donations-dialog.ui");
  bz_widget_class_bind_all_util_callbacks (widget_class);
  gtk_widget_class_bind_template_child (widget_class, BzDonationsDialog, title);
  gtk_widget_class_bind_template_child (widget_class, BzDonationsDialog, subtitle);
  gtk_widget_class_bind_template_child (widget_class, BzDonationsDialog, release_button);
  gtk_widget_class_bind_template_child (widget_class, BzDonationsDialog, donate_button);
  gtk_widget_class_bind_template_callback (widget_class, donate_clicked);
  gtk_widget_class_bind_template_callback (widget_class, release_page_clicked);
}

static void
bz_donations_dialog_init (BzDonationsDialog *self)
{
  g_autoptr (GBytes) release_notes_bytes = NULL;
  g_autoptr (GError) error               = NULL;
  g_autoptr (XbBuilderSource) source     = NULL;
  g_autoptr (XbBuilder) builder          = NULL;
  g_autoptr (XbSilo) silo                = NULL;
  g_autoptr (XbNode) release_node        = NULL;
  g_autoptr (XbNode) url_node            = NULL;
  g_autoptr (XbNode) description_node    = NULL;
  const char             *version        = NULL;
  const char             *date           = NULL;
  const char             *url            = NULL;
  AdwBreakpointCondition *condition      = NULL;

  gtk_widget_init_template (GTK_WIDGET (self));

  gtk_widget_set_tooltip_text (GTK_WIDGET (self->release_button), RELEASE_PAGE);
  gtk_widget_set_tooltip_text (GTK_WIDGET (self->donate_button), DONATE_LINK);

  condition = adw_breakpoint_condition_new_length (
      ADW_BREAKPOINT_CONDITION_MAX_WIDTH,
      500,
      ADW_LENGTH_UNIT_PX);
  self->breakpoint = adw_breakpoint_new (condition);
  adw_breakpoint_add_setter (self->breakpoint,
                             G_OBJECT (self),
                             "width-request",
                             &(GValue) { G_TYPE_INT, { { .v_int = 350 } } });

  g_signal_connect (self, "map", G_CALLBACK (on_map), NULL);

  release_notes_bytes = g_resources_lookup_data (
      "/io/github/kolunmi/Bazaar/release-notes.xml",
      G_RESOURCE_LOOKUP_FLAGS_NONE,
      &error);

  if (release_notes_bytes == NULL)
    {
      g_warning ("Failed to load release notes: %s", error->message);
      return;
    }

  source = xb_builder_source_new ();
  if (!xb_builder_source_load_bytes (source, release_notes_bytes,
                                     XB_BUILDER_SOURCE_FLAG_LITERAL_TEXT,
                                     &error))
    {
      g_warning ("Failed to load release notes into xmlb: %s", error->message);
      return;
    }

  builder = xb_builder_new ();
  xb_builder_import_source (builder, source);

  silo = xb_builder_compile (builder, XB_BUILDER_COMPILE_FLAG_NONE, NULL, &error);
  if (silo == NULL)
    {
      g_warning ("Failed to compile release notes silo: %s", error->message);
      return;
    }

  release_node = xb_silo_query_first (silo, "release", &error);
  if (release_node == NULL)
    {
      g_warning ("Failed to find release node: %s", error != NULL ? error->message : "no error");
      g_clear_error (&error);
      return;
    }

  version = xb_node_get_attr (release_node, "version");
  date    = xb_node_get_attr (release_node, "date");

  if (version != NULL)
    {
      g_autofree char *title_str = NULL;
      /* Translators: the %s format specifier will be something along the lines of "0.7.6" etc */
      title_str = g_strdup_printf (_ ("What's New in %s?"), version);
      gtk_label_set_label (self->title, title_str);
    }

  if (date != NULL)
    {
      g_autofree char *date_full    = NULL;
      g_autoptr (GDateTime) dt      = NULL;
      g_autofree char *subtitle_str = NULL;

      date_full = g_strdup_printf ("%sT00:00:00Z", date);
      dt        = g_date_time_new_from_iso8601 (date_full, NULL);

      if (dt != NULL)
        {
          /* Translators: this is a release date label, like "Released February 9, 2026" */
          subtitle_str = g_date_time_format (dt, _ ("Released %B %-e, %Y"));
          if (subtitle_str != NULL)
            gtk_label_set_label (self->subtitle, subtitle_str);
        }
    }

  url_node = xb_silo_query_first (silo, "release/url[@type='details']", &error);
  if (url_node != NULL)
    {
      url = xb_node_get_text (url_node);
      if (url != NULL)
        self->release_url = g_strdup (url);
    }
  else
    g_clear_error (&error);

  description_node = xb_silo_query_first (silo, "release/description", &error);
  if (description_node != NULL)
    {
      self->release_notes = xb_node_export (description_node, XB_NODE_EXPORT_FLAG_INCLUDE_SIBLINGS, &error);
      if (self->release_notes == NULL)
        {
          g_warning ("Failed to export description node: %s", error->message);
          g_clear_error (&error);
        }
    }
  else
    {
      g_warning ("Failed to find description node: %s", error != NULL ? error->message : "no error");
      g_clear_error (&error);
    }

  if (self->release_notes != NULL)
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP_RELEASE_NOTES]);
}

AdwDialog *
bz_donations_dialog_new (void)
{
  return g_object_new (BZ_TYPE_DONATIONS_DIALOG, NULL);
}

static const char *
bz_donations_dialog_get_release_notes (BzDonationsDialog *self)
{
  g_return_val_if_fail (BZ_IS_DONATIONS_DIALOG (self), NULL);
  return self->release_notes;
}

/* End of bz-donations-dialog.c */
