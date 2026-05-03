/* bz-template-callbacks.c
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

#include "bz-template-callbacks.h"

static gboolean
invert_boolean (gpointer object,
                gboolean value)
{
  return !value;
}

/* Let's try to use this more for conciseness */
static gboolean
not (gpointer object,
     gboolean value)
{
  return invert_boolean (object, value);
}

static gboolean
is_zero (gpointer object,
         int      value)
{
  return value == 0;
}

static gboolean
is_double_zero (gpointer object,
                double   value)
{
  return value == 0.0;
}

static gboolean
is_null (gpointer object,
         GObject *value)
{
  return value == NULL;
}

static gboolean
is_gt (gpointer object,
       int      value1,
       int      value2)
{
  return value1 > value2;
}

static gboolean
is_lt (gpointer object,
       int      value1,
       int      value2)
{
  return value1 < value2;
}

static gboolean
is_ge (gpointer object,
       int      value1,
       int      value2)
{
  return value1 >= value2;
}

static gboolean
is_le (gpointer object,
       int      value1,
       int      value2)
{
  return value1 <= value2;
}

static gboolean
logical_and (gpointer object,
             gboolean value1,
             gboolean value2)
{
  return value1 && value2;
}

static gboolean
logical_or (gpointer object,
            gboolean value1,
            gboolean value2)
{
  return value1 || value2;
}

static gboolean
is_positive (gpointer object,
             int      value)
{
  return value >= 0;
}

static gboolean
is_empty (gpointer    object,
          GListModel *model)
{
  return model == NULL ||
         g_list_model_get_n_items (model) == 0;
}

static gboolean
is_empty_string (gpointer    object,
                 const char *str)
{
  return str == NULL ||
         *str == '\0';
}

static gboolean
is_longer (gpointer    object,
           GListModel *model,
           int         value)
{
  return model != NULL &&
         g_list_model_get_n_items (model) > value;
}

static char *
bool_to_string (gpointer object,
                gboolean condition,
                char    *if_true,
                char    *if_false)
{
  return g_strdup (condition ? if_true : if_false);
}

static gpointer
choose (gpointer object,
        gboolean condition,
        gpointer if_true,
        gpointer if_false)
{
  return condition ? if_true : if_false;
}

static char *
choose_string (gpointer    object,
               gboolean    condition,
               const char *if_true,
               const char *if_false)
{
  return g_strdup (condition ? if_true : if_false);
}

static char *
format_int (gpointer object,
            gint     integer)
{
  return g_strdup_printf ("%d", integer);
}

static char *
format_uint (gpointer object,
             guint    uint)
{
  return g_strdup_printf ("%u", uint);
}

static char *
format_double (gpointer object,
               double   number)
{
  return g_strdup_printf ("%f", number);
}

void
bz_widget_class_bind_all_util_callbacks (GtkWidgetClass *widget_class)
{
  g_return_if_fail (GTK_IS_WIDGET_CLASS (widget_class));

  gtk_widget_class_bind_template_callback (widget_class, invert_boolean);
  gtk_widget_class_bind_template_callback (widget_class, not);
  gtk_widget_class_bind_template_callback (widget_class, is_zero);
  gtk_widget_class_bind_template_callback (widget_class, is_double_zero);
  gtk_widget_class_bind_template_callback (widget_class, is_null);
  gtk_widget_class_bind_template_callback (widget_class, is_gt);
  gtk_widget_class_bind_template_callback (widget_class, is_lt);
  gtk_widget_class_bind_template_callback (widget_class, is_ge);
  gtk_widget_class_bind_template_callback (widget_class, is_le);
  gtk_widget_class_bind_template_callback (widget_class, logical_and);
  gtk_widget_class_bind_template_callback (widget_class, logical_or);
  gtk_widget_class_bind_template_callback (widget_class, is_positive);
  gtk_widget_class_bind_template_callback (widget_class, is_empty);
  gtk_widget_class_bind_template_callback (widget_class, is_empty_string);
  gtk_widget_class_bind_template_callback (widget_class, is_longer);
  gtk_widget_class_bind_template_callback (widget_class, bool_to_string);
  gtk_widget_class_bind_template_callback (widget_class, choose);
  gtk_widget_class_bind_template_callback (widget_class, choose_string);
  gtk_widget_class_bind_template_callback (widget_class, format_int);
  gtk_widget_class_bind_template_callback (widget_class, format_uint);
  gtk_widget_class_bind_template_callback (widget_class, format_double);
}
