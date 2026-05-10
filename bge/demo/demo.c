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

static void
on_activate (GtkApplication *app);

static void
on_buffer_changed (GtkTextBuffer   *buffer,
                   BgeWdgtRenderer *wdgt);

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
  GtkWidget         *window             = NULL;
  GtkWidget         *root               = NULL;
  GtkTextBuffer     *buffer             = NULL;
  BgeWdgtRenderer   *wdgt               = NULL;
  BgeMarkdownRender *markdown           = NULL;
  g_autoptr (GtkStringObject) reference = NULL;
  g_autoptr (GtkBuilder) builder        = NULL;
  g_autoptr (GtkBuilderScope) scope     = NULL;
  g_autoptr (GBytes) wdgt_bytes         = NULL;
  gsize         wdgt_buffer_size        = 0;
  gconstpointer wdgt_buffer             = NULL;
  g_autoptr (GBytes) markdown_bytes     = NULL;
  gsize         markdown_buffer_size    = 0;
  gconstpointer markdown_buffer         = NULL;

  window = gtk_application_window_new (app);
  gtk_window_set_default_size (GTK_WINDOW (window), 1000, 500);

  scope = gtk_builder_cscope_new ();

  builder = gtk_builder_new ();
  gtk_builder_set_scope (builder, scope);
  gtk_builder_add_from_resource (builder, "/io/github/kolunmi/BgeDemo/window.ui", NULL);
  root     = GTK_WIDGET (gtk_builder_get_object (builder, "root"));
  buffer   = GTK_TEXT_BUFFER (gtk_builder_get_object (builder, "buffer"));
  wdgt     = BGE_WDGT_RENDERER (gtk_builder_get_object (builder, "wdgt"));
  markdown = BGE_MARKDOWN_RENDER (gtk_builder_get_object (builder, "markdown"));

  reference = gtk_string_object_new ("Hello from demo.c!!");
  bge_wdgt_renderer_set_reference (wdgt, G_OBJECT (reference));

  g_signal_connect (
      buffer, "changed",
      G_CALLBACK (on_buffer_changed),
      wdgt);

  wdgt_bytes = g_resources_lookup_data (
      "/io/github/kolunmi/BgeDemo/test.wdgt",
      G_RESOURCE_LOOKUP_FLAGS_NONE, NULL);
  g_assert (wdgt_bytes != NULL);
  wdgt_buffer = g_bytes_get_data (wdgt_bytes, &wdgt_buffer_size);
  gtk_text_buffer_set_text (buffer, wdgt_buffer, wdgt_buffer_size);

  markdown_bytes = g_resources_lookup_data (
      "/io/github/kolunmi/BgeDemo/example-markdown.md",
      G_RESOURCE_LOOKUP_FLAGS_NONE, NULL);
  g_assert (markdown_bytes != NULL);
  markdown_buffer = g_bytes_get_data (markdown_bytes, &markdown_buffer_size);
  bge_markdown_render_set_markdown (markdown, markdown_buffer);

  gtk_window_set_child (GTK_WINDOW (window), g_object_ref_sink (root));
  gtk_window_present (GTK_WINDOW (window));
}

static void
on_buffer_changed (GtkTextBuffer   *buffer,
                   BgeWdgtRenderer *wdgt)
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
  if (spec == NULL)
    g_print ("Error!! %s\n", local_error->message);
  bge_wdgt_renderer_set_spec (wdgt, spec);
}
