/* spdx.c
 *
 * Copyright 2025 Alexander Vanhee
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

#include "spdx.h"

gboolean
bz_spdx_is_valid (const char *license_id)
{
  g_autofree char *url = NULL;

  g_return_val_if_fail (license_id != NULL, FALSE);

  url = as_get_license_url (license_id);
  return url != NULL;
}

char *
bz_spdx_get_url (const char *license_id)
{
  g_return_val_if_fail (license_id != NULL, NULL);

  return as_get_license_url (license_id);
}

char *
bz_spdx_get_name (const char *license_id)
{
  char *result = NULL;
  g_return_val_if_fail (license_id != NULL, NULL);

  if (bz_spdx_is_proprietary (license_id))
    return g_strdup ("Proprietary");

  result = as_license_to_spdx_id (license_id);
  if (result == NULL)
    return g_strdup (license_id);

  return result;
}

gboolean
bz_spdx_is_proprietary (const char *license_id)
{
  g_return_val_if_fail (license_id != NULL, FALSE);

  return g_str_has_prefix (license_id, "LicenseRef-proprietary");
}
