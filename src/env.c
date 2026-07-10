/* env.c
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

#include <libdex.h>

#include "env.h"

gsize
bz_get_dex_stack_size (void)
{
  static guint64 stack_size = 0;

  if (g_once_init_enter (&stack_size))
    {
      const char *envvar = NULL;
      guint64     value  = 0;

      /* Ensure we have enough space for gtk/glycin

         Some routines try to optimize by putting stuff on the stack, see
         https://gitlab.gnome.org/GNOME/libdex/-/issues/27#note_2582332

         2025-10-21 22:47:02 eva */
      value = MAX (8388608, dex_get_min_stack_size ());

      envvar = g_getenv ("BAZAAR_DEX_STACK_SIZE");
      if (envvar != NULL)
        {
          g_autoptr (GError) local_error = NULL;
          g_autoptr (GVariant) variant   = NULL;

          variant = g_variant_parse (
              G_VARIANT_TYPE_UINT64, envvar,
              NULL, NULL, &local_error);
          if (variant != NULL)
            {
              guint64 parse_result = 0;

              parse_result = g_variant_get_uint64 (variant);
              if (parse_result < dex_get_min_stack_size ())
                g_warning ("BAZAAR_DEX_STACK_SIZE must be greater than %zu on this system",
                           dex_get_min_stack_size ());
              else
                value = parse_result;
            }
          else
            g_warning ("BAZAAR_DEX_STACK_SIZE is invalid: %s", local_error->message);
        }

      g_once_init_leave (&stack_size, value);
    }

  return stack_size;
}

guint64
bz_get_n_download_workers (void)
{
  static guint64 n_dl_workers = 0;

  if (g_once_init_enter (&n_dl_workers))
    {
      const char *envvar = NULL;
      guint64     value  = 0;

      value = 8;

      envvar = g_getenv ("BAZAAR_N_DOWNLOAD_WORKERS");
      if (envvar != NULL)
        {
          g_autoptr (GError) local_error = NULL;
          g_autoptr (GVariant) variant   = NULL;

          variant = g_variant_parse (
              G_VARIANT_TYPE_UINT64, envvar,
              NULL, NULL, &local_error);
          if (variant != NULL)
            {
              guint64 parse_result = 0;

              parse_result = g_variant_get_uint64 (variant);
              if (parse_result == 0)
                g_warning ("BAZAAR_N_DOWNLOAD_WORKERS must be greater than 0");
              else
                value = parse_result;
            }
          else
            g_warning ("BAZAAR_N_DOWNLOAD_WORKERS is invalid: %s", local_error->message);
        }

      g_once_init_leave (&n_dl_workers, value);
    }

  return n_dl_workers;
}

guint
bz_get_desktop_search_provider_icon_size (void)
{
  static guint64 icon_size = 0;

  if (g_once_init_enter (&icon_size))
    {
      const char *envvar = NULL;
      guint64     value  = 0;

      /* default 24x24 for the gnome-shell search provider */
      value = 24;

      envvar = g_getenv ("BAZAAR_DESKTOP_SEARCH_PROVIDER_ICON_SIZE");
      if (envvar != NULL)
        {
          g_autoptr (GError) local_error = NULL;
          g_autoptr (GVariant) variant   = NULL;

          variant = g_variant_parse (
              G_VARIANT_TYPE_UINT64, envvar,
              NULL, NULL, &local_error);
          if (variant != NULL)
            {
              guint64 parse_result = 0;

              parse_result = g_variant_get_uint64 (variant);
              if (parse_result == 0 || parse_result > 256)
                g_warning ("BAZAAR_DESKTOP_SEARCH_PROVIDER_ICON_SIZE must be "
                           "greater than 0 but no greater than 256");
              else
                value = parse_result;
            }
          else
            g_warning ("BAZAAR_DESKTOP_SEARCH_PROVIDER_ICON_SIZE is invalid: %s", local_error->message);
        }

      g_once_init_leave (&icon_size, value);
    }

  return (guint) icon_size;
}
