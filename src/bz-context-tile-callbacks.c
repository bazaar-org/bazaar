/* bz-context-tile-callbacks.c
 *
 * Copyright 2026 Eva M, Alexander Vanhee
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

#include <appstream.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <math.h>

#include "bz-context-tile-callbacks.h"
#include "bz-entry.h"
#include "bz-safety-calculator.h"
#include "bz-spdx.h"

static char *
format_with_small_suffix (char *number, const char *suffix)
{
  char *dot = g_strrstr (number, ".");

  if (dot != NULL)
    {
      char *end = dot;
      while (*(end + 1) != '\0')
        end++;
      while (end > dot && *end == '0')
        *end-- = '\0';
      if (end == dot)
        *dot = '\0';
    }

  return g_strdup_printf ("%s\xC2\xA0<span font_size='x-small'>%s</span>",
                          number, suffix);
}

static char *
format_favorites_count (gpointer object,
                        int      favorites_count)
{
  if (favorites_count < 0)
    return g_strdup ("  ");
  return g_strdup_printf ("%d", favorites_count);
}

static char *
format_recent_downloads (gpointer object,
                         int      value)
{
  double result;
  int    digits;

  if (value <= 0)
    return g_strdup (_ ("---"));

  if (value >= 1000000)
    {
      result = value / 1000000.0;
      digits = (int) log10 (result) + 1;
      /* Translators: M is the suffix for millions */
      return g_strdup_printf (_ ("%.*fM"), 3 - digits, result);
    }
  else if (value >= 1000)
    {
      result = value / 1000.0;
      digits = (int) log10 (result) + 1;
      /* Translators: K is the suffix for thousands*/
      return g_strdup_printf (_ ("%.*fK"), 3 - digits, result);
    }
  else
    return g_strdup_printf ("%'d", value);
}

static char *
format_recent_downloads_tooltip (gpointer object,
                                 int      value)
{
  return g_strdup_printf (_ ("%d downloads in the last month"), value);
}

static char *
format_size (gpointer object, guint64 value)
{
  g_autofree char *size_str = g_format_size (value);
  char            *space    = g_strrstr (size_str, "\xC2\xA0");
  char            *decimal  = NULL;
  int              digits   = 0;

  if (value == 0)
    return g_strdup (_ ("N/A"));

  if (space != NULL)
    {
      *space = '\0';
      for (char *p = size_str; *p != '\0' && *p != '.'; p++)
        if (g_ascii_isdigit (*p))
          digits++;
      if (digits >= 3)
        {
          decimal = g_strrstr (size_str, ".");
          if (decimal != NULL)
            *decimal = '\0';
        }
      return format_with_small_suffix (size_str, space + 2);
    }
  return g_strdup (size_str);
}

static char *
get_size_label (gpointer object,
                gboolean is_installable,
                gboolean runtime_installed,
                guint64  runtime_size)
{
  if (is_installable && !runtime_installed && runtime_size > 0)
    {
      g_autofree char *size_str = g_format_size (runtime_size);
      return g_strdup_printf (_ ("+%s runtime"), size_str);
    }

  return g_strdup (is_installable ? _ ("Download") : _ ("Installed"));
}

static guint64
get_size_type (gpointer object,
               BzEntry *entry,
               gboolean is_installable)
{
  if (entry == NULL)
    return 0;

  return is_installable ? bz_entry_get_size (entry) : bz_entry_get_installed_size (entry);
}

static char *
format_size_tooltip (gpointer object, guint64 value)
{
  g_autofree char *size_str = NULL;

  if (value == 0)
    return g_strdup (_ ("Size information unavailable"));

  size_str = g_format_size (value);
  return g_strdup_printf (_ ("Download size of %s"), size_str);
}

static char *
format_age_rating (gpointer         object,
                   AsContentRating *content_rating)
{
  guint age;

  if (content_rating == NULL)
    return g_strdup ("?");

  age = as_content_rating_get_minimum_age (content_rating);

  if (age < 3)
    age = 3;

  /* Translators: Age rating format, e.g. "12+" for ages 12 and up */
  return g_strdup_printf (_ ("%d+"), age);
}

static char *
get_age_rating_label (gpointer         object,
                      AsContentRating *content_rating)
{
  guint age;

  if (content_rating == NULL)
    return g_strdup (_ ("Age Rating"));

  age = as_content_rating_get_minimum_age (content_rating);

  if (age == 0)
    return g_strdup (_ ("All Ages"));
  else
    return g_strdup (_ ("Age Rating"));
}

static char *
get_age_rating_tooltip (gpointer         object,
                        AsContentRating *content_rating)
{
  guint age;

  if (content_rating == NULL)
    return g_strdup (_ ("Age rating information unavailable"));

  age = as_content_rating_get_minimum_age (content_rating);

  if (age == 0)
    return g_strdup (_ ("Suitable for all ages"));

  return g_strdup_printf (_ ("Suitable for ages %d and up"), age);
}

static char *
get_age_rating_style (gpointer         object,
                      AsContentRating *content_rating)
{
  guint age;

  if (content_rating == NULL)
    return g_strdup ("grey");

  age = as_content_rating_get_minimum_age (content_rating);

  if (age >= 18)
    return g_strdup ("error");
  else if (age >= 15)
    return g_strdup ("orange");
  else if (age >= 12)
    return g_strdup ("warning");
  else
    return g_strdup ("grey");
}

static char *
format_license_tooltip (gpointer object,
                        BzEntry *entry)
{
  const char      *license;
  gboolean         is_floss = FALSE;
  g_autofree char *name     = NULL;

  if (entry == NULL)
    return g_strdup (_ ("Unknown"));

  g_object_get (entry, "is-floss", &is_floss, "project-license", &license, NULL);

  if (license == NULL || *license == '\0')
    return g_strdup (_ ("Unknown"));

  if (is_floss && bz_spdx_is_valid (license))
    {
      name = bz_spdx_get_name (license);
      return g_strdup_printf (_ ("Free software licensed under %s"),
                              (name != NULL && *name != '\0') ? name : license);
    }

  if (is_floss)
    return g_strdup (_ ("Free software"));

  if (bz_spdx_is_proprietary (license))
    return g_strdup (_ ("Proprietary Software"));

  name = bz_spdx_get_name (license);
  return g_strdup_printf (_ ("Special License: %s"),
                          (name != NULL && *name != '\0') ? name : license);
}

static char *
get_license_label (gpointer object,
                   BzEntry *entry)
{
  const char *license;
  gboolean    is_floss = FALSE;

  if (entry == NULL)
    return g_strdup (_ ("Unknown"));

  g_object_get (entry, "is-floss", &is_floss, "project-license", &license, NULL);

  if (is_floss)
    return g_strdup (_ ("Free"));

  if (license == NULL || *license == '\0')
    return g_strdup (_ ("Unknown"));

  if (bz_spdx_is_proprietary (license))
    return g_strdup (_ ("Proprietary"));

  return g_strdup (_ ("Special License"));
}

static char *
get_license_icon (gpointer object,
                  gboolean is_floss,
                  int      index)
{
  const char *icons[][2] = {
    {   "license-symbolic", "proprietary-code-symbolic" },
    { "community-symbolic",          "license-symbolic" }
  };

  return g_strdup (icons[is_floss ? 1 : 0][index]);
}

static char *
get_formfactor_label (gpointer object,
                      gboolean is_mobile_friendly)
{
  return g_strdup (is_mobile_friendly ? _ ("Adaptive") : _ ("Desktop Only"));
}

static char *
get_formfactor_tooltip (gpointer object, gboolean is_mobile_friendly)
{
  return g_strdup (is_mobile_friendly ? _ ("Works on desktop, tablets, and phones")
                                      : _ ("May not work on mobile devices"));
}

static char *
get_safety_rating_icon (gpointer object,
                        BzEntry *entry,
                        int      index)
{
  char        *icon       = NULL;
  BzImportance importance = 0;

  if (entry == NULL)
    return g_strdup ("app-safety-unknown-symbolic");

  if (index < 0 || index > 2)
    return NULL;

  if (index == 0)
    {
      importance = bz_safety_calculator_calculate_rating (entry);
      switch (importance)
        {
        case BZ_IMPORTANCE_UNIMPORTANT:
        case BZ_IMPORTANCE_NEUTRAL:
          return g_strdup ("app-safety-ok-symbolic");
        case BZ_IMPORTANCE_INFORMATION:
        case BZ_IMPORTANCE_WARNING:
          return NULL;
        case BZ_IMPORTANCE_IMPORTANT:
          return g_strdup ("dialog-warning-symbolic");
        default:
          return NULL;
        }
    }

  icon = bz_safety_calculator_get_top_icon (entry, index - 1);
  return icon;
}

const char *
bz_safety_style_for_importance (BzImportance importance)
{
  switch (importance)
    {
    case BZ_IMPORTANCE_UNIMPORTANT:
    case BZ_IMPORTANCE_NEUTRAL:
      return "grey";
    case BZ_IMPORTANCE_INFORMATION:
      return "warning";
    case BZ_IMPORTANCE_WARNING:
      return "orange";
    case BZ_IMPORTANCE_IMPORTANT:
      return "error";
    default:
      return "grey";
    }
}

static char *
get_safety_rating_style (gpointer object,
                         BzEntry *entry)
{
  if (entry == NULL)
    return g_strdup ("grey");

  return g_strdup (bz_safety_style_for_importance (
      bz_safety_calculator_calculate_rating (entry)));
}

static char *
get_safety_rating_label (gpointer object,
                         BzEntry *entry)
{
  BzImportance importance;

  if (entry == NULL)
    return g_strdup (_ ("N/A"));

  importance = bz_safety_calculator_calculate_rating (entry);

  switch (importance)
    {
    case BZ_IMPORTANCE_UNIMPORTANT:
      return g_strdup (_ ("Safe"));
    case BZ_IMPORTANCE_NEUTRAL:
      return g_strdup (_ ("Low Risk"));
    case BZ_IMPORTANCE_INFORMATION:
      return g_strdup (_ ("Low Risk"));
    case BZ_IMPORTANCE_WARNING:
      return g_strdup (_ ("Medium Risk"));
    case BZ_IMPORTANCE_IMPORTANT:
      return g_strdup (_ ("High Risk"));
    default:
      return g_strdup (_ ("N/A"));
    }
}

void
bz_widget_class_bind_all_context_tile_callbacks (GtkWidgetClass *widget_class)
{
  g_return_if_fail (GTK_IS_WIDGET_CLASS (widget_class));

  gtk_widget_class_bind_template_callback (widget_class, format_favorites_count);
  gtk_widget_class_bind_template_callback (widget_class, format_recent_downloads);
  gtk_widget_class_bind_template_callback (widget_class, format_recent_downloads_tooltip);
  gtk_widget_class_bind_template_callback (widget_class, format_size);
  gtk_widget_class_bind_template_callback (widget_class, get_size_label);
  gtk_widget_class_bind_template_callback (widget_class, get_size_type);
  gtk_widget_class_bind_template_callback (widget_class, format_size_tooltip);
  gtk_widget_class_bind_template_callback (widget_class, format_age_rating);
  gtk_widget_class_bind_template_callback (widget_class, get_age_rating_label);
  gtk_widget_class_bind_template_callback (widget_class, get_age_rating_tooltip);
  gtk_widget_class_bind_template_callback (widget_class, get_age_rating_style);
  gtk_widget_class_bind_template_callback (widget_class, format_license_tooltip);
  gtk_widget_class_bind_template_callback (widget_class, get_license_label);
  gtk_widget_class_bind_template_callback (widget_class, get_license_icon);
  gtk_widget_class_bind_template_callback (widget_class, get_formfactor_label);
  gtk_widget_class_bind_template_callback (widget_class, get_formfactor_tooltip);
  gtk_widget_class_bind_template_callback (widget_class, get_safety_rating_icon);
  gtk_widget_class_bind_template_callback (widget_class, get_safety_rating_style);
  gtk_widget_class_bind_template_callback (widget_class, get_safety_rating_label);
}
