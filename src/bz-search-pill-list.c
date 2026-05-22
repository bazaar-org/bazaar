/* bz-search-pill-list.c
 *
 * Copyright 2026 Alexander Vanhee
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

#include "bz-search-pill-list.h"

struct _BzSearchPillList
{
  GtkBox parent_instance;
};

G_DEFINE_FINAL_TYPE (BzSearchPillList, bz_search_pill_list, GTK_TYPE_BOX)

enum
{
  SIGNAL_ACTIVATED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

static void
pill_button_clicked_cb (BzSearchPillList *self,
                        GtkButton        *button);

static void
bz_search_pill_list_class_init (BzSearchPillListClass *klass)
{
  signals[SIGNAL_ACTIVATED] =
      g_signal_new ("activated",
                    G_TYPE_FROM_CLASS (klass),
                    G_SIGNAL_RUN_LAST,
                    0, NULL, NULL,
                    g_cclosure_marshal_VOID__STRING,
                    G_TYPE_NONE, 1, G_TYPE_STRING);
}

static void
bz_search_pill_list_init (BzSearchPillList *self)
{
  GtkLayoutManager  *layout  = NULL;
  static const char *pills[] = {
    /* Translators: Search suggestion: the english text will be used for the
       search regardless of what you put here, so don't worry about the string
       yielding poor search results. Focus on correctness and friendliness,
       etc */
    N_ ("Browser"),
    /* Translators: Search suggestion: the english text will be used for the
       search regardless of what you put here, so don't worry about the string
       yielding poor search results. Focus on correctness and friendliness,
       etc */
    N_ ("Video"),
    /* Translators: Search suggestion: the english text will be used for the
       search regardless of what you put here, so don't worry about the string
       yielding poor search results. Focus on correctness and friendliness,
       etc */
    N_ ("Music"),
    /* Translators: Search suggestion: the english text will be used for the
       search regardless of what you put here, so don't worry about the string
       yielding poor search results. Focus on correctness and friendliness,
       etc */
    N_ ("Office"),
    /* Translators: Search suggestion: the english text will be used for the
       search regardless of what you put here, so don't worry about the string
       yielding poor search results. Focus on correctness and friendliness,
       etc */
    N_ ("PDF"),
    /* Translators: Search suggestion: the english text will be used for the
       search regardless of what you put here, so don't worry about the string
       yielding poor search results. Focus on correctness and friendliness,
       etc */
    N_ ("Calendar"),
    /* Translators: Search suggestion: the english text will be used for the
       search regardless of what you put here, so don't worry about the string
       yielding poor search results. Focus on correctness and friendliness,
       etc */
    N_ ("Messaging"),
    /* Translators: Search suggestion: the english text will be used for the
       search regardless of what you put here, so don't worry about the string
       yielding poor search results. Focus on correctness and friendliness,
       etc */
    N_ ("Paint"),
    /* Translators: Search suggestion: the english text will be used for the
       search regardless of what you put here, so don't worry about the string
       yielding poor search results. Focus on correctness and friendliness,
       etc */
    N_ ("VPN"),
    /* Translators: Search suggestion: the english text will be used for the
       search regardless of what you put here, so don't worry about the string
       yielding poor search results. Focus on correctness and friendliness,
       etc */
    N_ ("Torrent"),
    /* Translators: Search suggestion: the english text will be used for the
       search regardless of what you put here, so don't worry about the string
       yielding poor search results. Focus on correctness and friendliness,
       etc */
    N_ ("Emulator"),

    /* This array MUST be NULL terminated */
    NULL
  };

  gtk_orientable_set_orientation (GTK_ORIENTABLE (self), GTK_ORIENTATION_HORIZONTAL);

  layout = GTK_LAYOUT_MANAGER (adw_wrap_layout_new ());
  adw_wrap_layout_set_child_spacing (ADW_WRAP_LAYOUT (layout), 10);
  adw_wrap_layout_set_line_spacing (ADW_WRAP_LAYOUT (layout), 8);
  adw_wrap_layout_set_justify (ADW_WRAP_LAYOUT (layout), ADW_JUSTIFY_FILL);
  gtk_widget_set_layout_manager (GTK_WIDGET (self), layout);

  for (guint i = 0; pills[i] != NULL; i++)
    {
      GtkWidget *button = NULL;

      button = gtk_button_new_with_label (Q_ (pills[i]));
      gtk_widget_add_css_class (button, "small-pill");
      gtk_widget_add_css_class (button, "search-pill");
      g_signal_connect_swapped (button, "clicked",
                                G_CALLBACK (pill_button_clicked_cb), self);
      g_object_set_data_full (G_OBJECT (button), "search-token",
                              g_strdup (pills[i]), g_free);
      gtk_box_append (GTK_BOX (self), button);
    }
}

GtkWidget *
bz_search_pill_list_new (void)
{
  return g_object_new (BZ_TYPE_SEARCH_PILL_LIST, NULL);
}

static void
pill_button_clicked_cb (BzSearchPillList *self,
                        GtkButton        *button)
{
  const char *search_token = NULL;

  search_token = g_object_get_data (G_OBJECT (button), "search-token");
  g_signal_emit (self, signals[SIGNAL_ACTIVATED], 0, search_token);
}
