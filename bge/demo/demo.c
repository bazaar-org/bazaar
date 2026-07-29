/* demo.c
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

#include <bge.h>

static BgeWdgtRenderer *wdgt      = NULL;
static GtkLabel        *error_lbl = NULL;

static void
on_activate (GtkApplication *app);

static GtkWidget *
on_carousel_create_widget (BgeCarousel     *carousel,
                           GtkStringObject *string,
                           gpointer         user_data);

static void
on_buffer_changed (GtkTextBuffer *buffer,
                   gpointer       user_data);

int
main (int argc, char **argv)
{
  g_autoptr (GtkApplication) app      = NULL;
  g_autoptr (GtkCssProvider) provider = NULL;

  bge_init ();

  app = gtk_application_new (
      "io.github.kolunmi.BgeDemo",
      G_APPLICATION_NON_UNIQUE);
  g_signal_connect (app, "activate", G_CALLBACK (on_activate), NULL);

  provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_resource (provider, "/io/github/kolunmi/BgeDemo/style.css");
  gtk_style_context_add_provider_for_display (
      gdk_display_get_default (),
      GTK_STYLE_PROVIDER (provider),
      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  return g_application_run (G_APPLICATION (app), argc, argv);
}

static void
on_activate (GtkApplication *app)
{
  GtkWidget         *window         = NULL;
  GtkWidget         *root           = NULL;
  GtkTextBuffer     *buffer         = NULL;
  BgeMarkdownRender *markdown       = NULL;
  GtkBox            *carousel_box   = NULL;
  g_autoptr (GtkBuilder) builder    = NULL;
  g_autoptr (GtkBuilderScope) scope = NULL;

  window = gtk_application_window_new (app);
  gtk_window_set_default_size (GTK_WINDOW (window), 1000, 800);

  scope = gtk_builder_cscope_new ();

  builder = gtk_builder_new ();
  gtk_builder_set_scope (builder, scope);
  gtk_builder_add_from_resource (builder, "/io/github/kolunmi/BgeDemo/window.ui", NULL);
  root         = GTK_WIDGET (gtk_builder_get_object (builder, "root"));
  buffer       = GTK_TEXT_BUFFER (gtk_builder_get_object (builder, "buffer"));
  wdgt         = BGE_WDGT_RENDERER (gtk_builder_get_object (builder, "wdgt"));
  error_lbl    = GTK_LABEL (gtk_builder_get_object (builder, "error_lbl"));
  markdown     = BGE_MARKDOWN_RENDER (gtk_builder_get_object (builder, "markdown"));
  carousel_box = GTK_BOX (gtk_builder_get_object (builder, "carousel_box"));

  {
    GtkIconTheme *theme             = NULL;
    g_auto (GStrv) icons            = NULL;
    GtkStringList *string_lists[16] = { 0 };

    theme = gtk_icon_theme_get_for_display (gdk_display_get_default ());
    icons = gtk_icon_theme_get_icon_names (theme);

    for (guint i = 0; i < G_N_ELEMENTS (string_lists); i++)
      {
        string_lists[i] = gtk_string_list_new (NULL);
      }

    for (guint i = 0; icons[i] != NULL; i++)
      {
        if (g_str_has_suffix (icons[i], "-symbolic"))
          {
            gtk_string_list_append (string_lists[i % G_N_ELEMENTS (string_lists)], icons[i]);
          }
      }

    for (guint i = 0; i < G_N_ELEMENTS (string_lists); i++)
      {
        g_autoptr (GtkSingleSelection) selection = NULL;
        GtkWidget *carousel                      = NULL;

        selection = gtk_single_selection_new (G_LIST_MODEL (string_lists[i]));

        carousel = bge_carousel_new ();
        bge_carousel_set_allow_mouse_drag (BGE_CAROUSEL (carousel), TRUE);
        bge_carousel_set_allow_overshoot (BGE_CAROUSEL (carousel), TRUE);

        g_signal_connect (carousel, "create-widget", G_CALLBACK (on_carousel_create_widget), NULL);
        bge_carousel_set_model (BGE_CAROUSEL (carousel), selection);

        gtk_box_append (carousel_box, carousel);

        if (i < G_N_ELEMENTS (string_lists) - 1)
          gtk_box_append (carousel_box, gtk_separator_new (GTK_ORIENTATION_HORIZONTAL));
      }
  }

  {
    g_autoptr (GBytes) markdown_bytes  = NULL;
    gsize         markdown_buffer_size = 0;
    gconstpointer markdown_buffer      = NULL;

    markdown_bytes = g_resources_lookup_data (
        "/io/github/kolunmi/BgeDemo/example-markdown.md",
        G_RESOURCE_LOOKUP_FLAGS_NONE, NULL);
    g_assert (markdown_bytes != NULL);
    markdown_buffer = g_bytes_get_data (markdown_bytes, &markdown_buffer_size);
    bge_markdown_render_set_markdown (markdown, markdown_buffer);
  }

  {
    g_autoptr (GtkStringObject) reference = NULL;
    g_autoptr (GBytes) wdgt_bytes         = NULL;
    gsize         wdgt_buffer_size        = 0;
    gconstpointer wdgt_buffer             = NULL;

    reference = gtk_string_object_new ("Hello from demo.c!!");
    bge_wdgt_renderer_set_reference (wdgt, G_OBJECT (reference));

    g_signal_connect (
        buffer, "changed",
        G_CALLBACK (on_buffer_changed),
        NULL);

    wdgt_bytes = g_resources_lookup_data (
        "/io/github/kolunmi/BgeDemo/test.wdgt",
        G_RESOURCE_LOOKUP_FLAGS_NONE, NULL);
    g_assert (wdgt_bytes != NULL);
    wdgt_buffer = g_bytes_get_data (wdgt_bytes, &wdgt_buffer_size);
    gtk_text_buffer_set_text (buffer, wdgt_buffer, wdgt_buffer_size);
  }

  gtk_window_set_child (GTK_WINDOW (window), g_object_ref_sink (root));
  gtk_window_present (GTK_WINDOW (window));
}

static GtkWidget *
on_carousel_create_widget (BgeCarousel     *carousel,
                           GtkStringObject *string_object,
                           gpointer         user_data)
{
  const char *icon_name = NULL;
  GtkWidget  *button    = NULL;

  icon_name = gtk_string_object_get_string (string_object);

  button = gtk_button_new_from_icon_name (icon_name);
  gtk_widget_set_margin_start (button, 2);
  gtk_widget_set_margin_end (button, 2);

  return button;
}

static void
on_buffer_changed (GtkTextBuffer *buffer,
                   gpointer       user_data)
{
  g_autoptr (GError) local_error = NULL;
  g_autofree char *text          = NULL;
  GtkTextIter      start_iter    = { 0 };
  GtkTextIter      end_iter      = { 0 };
  g_autoptr (BgeWdgtSpec) spec   = NULL;

  gtk_text_buffer_get_start_iter (buffer, &start_iter);
  gtk_text_buffer_get_end_iter (buffer, &end_iter);

  text = gtk_text_buffer_get_text (buffer, &start_iter, &end_iter, FALSE);
  spec = bge_wdgt_spec_new_for_string (text, &local_error);
  if (spec != NULL)
    gtk_widget_set_visible (GTK_WIDGET (error_lbl), FALSE);
  else
    {
      gtk_label_set_label (error_lbl, local_error->message);
      gtk_widget_set_visible (GTK_WIDGET (error_lbl), TRUE);
    }
  bge_wdgt_renderer_set_spec (wdgt, spec);
}
