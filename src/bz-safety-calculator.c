/* bz-safety-calculator.c
 *
 * Copyright 2026 Alexander Vanhee
 * Copyright (C) 2022 Red Hat <www.redhat.com>
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

#include "bz-app-permissions.h"
#include "bz-context-row.h"
#include "bz-safety-calculator.h"
#include "bz-safety-row.h"

static char *
format_bus_policy_title (const BzBusPolicy *bus_policy);
static const char *
format_bus_policy_subtitle (const BzBusPolicy *bus_policy);
static void
add_row_if_permission (GListStore  *store,
                       gboolean     has_permission,
                       BzImportance item_rating,
                       const char  *icon_name_with_permission,
                       const char  *title_with_permission,
                       const char  *description_with_permission,
                       const char  *icon_name_without_permission,
                       const char  *title_without_permission,
                       const char  *description_without_permission);

static gboolean
lookup_well_known_bus_policy (const char  *bus_name,
                              const char **out_title,
                              const char **out_description);

GListModel *
bz_safety_calculator_analyze_entry (BzEntry *entry)
{
  GListStore               *store           = NULL;
  BzAppPermissions         *permissions     = NULL;
  BzAppPermissionsFlags     perm_flags      = BZ_APP_PERMISSIONS_FLAGS_NONE;
  gboolean                  is_verified     = FALSE;
  gboolean                  is_foss         = FALSE;
  const GPtrArray          *filesystem_read = NULL;
  const GPtrArray          *filesystem_full = NULL;
  const BzBusPolicy *const *bus_policies    = NULL;
  size_t                    n_bus_policies  = 0;
  gboolean                  has_system_tray = FALSE;
  BzImportance              fs_importance   = BZ_IMPORTANCE_WARNING;
  guint                     i               = 0;

  g_return_val_if_fail (BZ_IS_ENTRY (entry), NULL);

  store       = g_list_store_new (BZ_TYPE_SAFETY_ROW);
  is_verified = bz_entry_is_verified (entry);
  is_foss     = bz_entry_get_is_foss (entry);

  g_object_get (entry, "permissions", &permissions, NULL);
  if (permissions != NULL)
    perm_flags = bz_app_permissions_get_flags (permissions);

  fs_importance = (perm_flags & BZ_APP_PERMISSIONS_FLAGS_NETWORK)
                      ? BZ_IMPORTANCE_WARNING
                      : BZ_IMPORTANCE_INFORMATION;

  if (permissions == NULL)
    {
      add_row_if_permission (store,
                             TRUE,
                             BZ_IMPORTANCE_WARNING,
                             "channel-insecure-symbolic",
                             _ ("Unknown Permissions"),
                             _ ("Permissions are missing for this app."),
                             NULL, NULL, NULL);
    }
  else
    {
      filesystem_read = bz_app_permissions_get_filesystem_read (permissions);
      filesystem_full = bz_app_permissions_get_filesystem_full (permissions);
      bus_policies    = bz_app_permissions_get_bus_policies (permissions, &n_bus_policies);

      add_row_if_permission (store,
                             bz_app_permissions_is_empty (permissions),
                             BZ_IMPORTANCE_UNIMPORTANT,
                             "permissions-sandboxed-symbolic",
                             _ ("No Permissions"),
                             _ ("App is fully sandboxed"),
                             NULL, NULL, NULL);
      add_row_if_permission (store,
                             (perm_flags & BZ_APP_PERMISSIONS_FLAGS_NETWORK) != 0,
                             BZ_IMPORTANCE_INFORMATION,
                             "network-wireless-symbolic",
                             _ ("Network Access"),
                             _ ("Can access the internet"),
                             "network-wireless-disabled-symbolic",
                             _ ("No Network Access"),
                             _ ("Cannot access the internet"));
      add_row_if_permission (store,
                             (perm_flags & BZ_APP_PERMISSIONS_FLAGS_DEVICES) != 0,
                             (perm_flags & BZ_APP_PERMISSIONS_FLAGS_NETWORK) ? BZ_IMPORTANCE_WARNING : BZ_IMPORTANCE_INFORMATION,
                             "camera-photo-symbolic",
                             _ ("User Device Access"),
                             _ ("Can access devices such as webcams or gaming controllers"),
                             "camera-disabled-symbolic",
                             _ ("No User Device Access"),
                             _ ("Cannot access devices such as webcams or gaming controllers"));
      add_row_if_permission (store,
                             (perm_flags & BZ_APP_PERMISSIONS_FLAGS_INPUT_DEVICES) != 0,
                             BZ_IMPORTANCE_INFORMATION,
                             "input-keyboard-symbolic",
                             _ ("Input Device Access"),
                             _ ("Can access input devices"),
                             NULL, NULL, NULL);
      add_row_if_permission (store,
                             (perm_flags & BZ_APP_PERMISSIONS_FLAGS_AUDIO_DEVICES) != 0,
                             BZ_IMPORTANCE_INFORMATION,
                             "permissions-microphone-symbolic",
                             _ ("Microphone Access and Audio Playback"),
                             _ ("Can listen using microphones and play audio without asking permission"),
                             NULL, NULL, NULL);
      add_row_if_permission (store,
                             (perm_flags & BZ_APP_PERMISSIONS_FLAGS_SYSTEM_DEVICES) != 0,
                             BZ_IMPORTANCE_WARNING,
                             "permissions-system-devices-symbolic",
                             _ ("System Device Access"),
                             _ ("Can access system devices which require elevated permissions"),
                             NULL, NULL, NULL);
      add_row_if_permission (store,
                             (perm_flags & BZ_APP_PERMISSIONS_FLAGS_SCREEN) != 0,
                             BZ_IMPORTANCE_WARNING,
                             "permissions-screen-contents-symbolic",
                             _ ("Screen Contents Access"),
                             _ ("Can access the contents of the screen or other windows"),
                             NULL, NULL, NULL);
      add_row_if_permission (store,
                             (perm_flags & BZ_APP_PERMISSIONS_FLAGS_X11) != 0,
                             BZ_IMPORTANCE_IMPORTANT,
                             "permissions-legacy-windowing-system-symbolic",
                             _ ("Legacy Windowing System"),
                             _ ("Always uses a legacy windowing system (X11)"),
                             NULL, NULL, NULL);
      add_row_if_permission (store,
                             (perm_flags & BZ_APP_PERMISSIONS_FLAGS_ESCAPE_SANDBOX) != 0,
                             BZ_IMPORTANCE_IMPORTANT,
                             "earth-symbolic",
                             _ ("Arbitrary Permissions"),
                             _ ("Can acquire arbitrary permissions"),
                             NULL, NULL, NULL);
      add_row_if_permission (store,
                             (perm_flags & BZ_APP_PERMISSIONS_FLAGS_SETTINGS) != 0,
                             BZ_IMPORTANCE_WARNING,
                             "emblem-system-symbolic",
                             _ ("User Settings"),
                             _ ("Can access and change user settings"),
                             NULL, NULL, NULL);
      add_row_if_permission (store,
                             (perm_flags & BZ_APP_PERMISSIONS_FLAGS_FILESYSTEM_FULL) != 0,
                             BZ_IMPORTANCE_IMPORTANT,
                             "drive-harddisk-symbolic",
                             _ ("Full File System Read/Write Access"),
                             _ ("Can read and write all data on the file system"),
                             NULL, NULL, NULL);
      add_row_if_permission (store,
                             ((perm_flags & BZ_APP_PERMISSIONS_FLAGS_HOME_FULL) != 0 &&
                              !(perm_flags & BZ_APP_PERMISSIONS_FLAGS_FILESYSTEM_FULL)),
                             BZ_IMPORTANCE_IMPORTANT,
                             "user-home-symbolic",
                             _ ("Home Folder Read/Write Access"),
                             _ ("Can read and write all data in your home directory"),
                             NULL, NULL, NULL);
      add_row_if_permission (store,
                             ((perm_flags & BZ_APP_PERMISSIONS_FLAGS_FILESYSTEM_READ) != 0 &&
                              !(perm_flags & BZ_APP_PERMISSIONS_FLAGS_FILESYSTEM_FULL)),
                             BZ_IMPORTANCE_IMPORTANT,
                             "folder-symbolic",
                             _ ("Full File System Read Access"),
                             _ ("Can read all data on the file system"),
                             NULL, NULL, NULL);
      add_row_if_permission (store,
                             ((perm_flags & BZ_APP_PERMISSIONS_FLAGS_HOME_READ) != 0 &&
                              !(perm_flags & (BZ_APP_PERMISSIONS_FLAGS_FILESYSTEM_FULL |
                                              BZ_APP_PERMISSIONS_FLAGS_FILESYSTEM_READ))),
                             BZ_IMPORTANCE_IMPORTANT,
                             "user-home-symbolic",
                             _ ("Home Folder Read Access"),
                             _ ("Can read all data in your home directory"),
                             NULL, NULL, NULL);
      add_row_if_permission (store,
                             ((perm_flags & BZ_APP_PERMISSIONS_FLAGS_DOWNLOADS_FULL) != 0 &&
                              !(perm_flags & (BZ_APP_PERMISSIONS_FLAGS_FILESYSTEM_FULL |
                                              BZ_APP_PERMISSIONS_FLAGS_HOME_FULL))),
                             fs_importance,
                             "folder-download-symbolic",
                             _ ("Download Folder Read/Write Access"),
                             _ ("Can read and write all data in your downloads directory"),
                             NULL, NULL, NULL);
      add_row_if_permission (store,
                             ((perm_flags & BZ_APP_PERMISSIONS_FLAGS_DOWNLOADS_READ) != 0 &&
                              !(perm_flags & (BZ_APP_PERMISSIONS_FLAGS_FILESYSTEM_FULL |
                                              BZ_APP_PERMISSIONS_FLAGS_FILESYSTEM_READ |
                                              BZ_APP_PERMISSIONS_FLAGS_HOME_FULL |
                                              BZ_APP_PERMISSIONS_FLAGS_HOME_READ))),
                             fs_importance,
                             "folder-download-symbolic",
                             _ ("Download Folder Read Access"),
                             _ ("Can read all data in your downloads directory"),
                             NULL, NULL, NULL);

      for (i = 0; filesystem_full != NULL && i < filesystem_full->len; i++)
        {
          const BzFilesystemPath *path     = g_ptr_array_index (filesystem_full, i);
          g_autofree char        *fs_title = bz_filesystem_path_to_display_string (path);
          const char             *fs_icon  = bz_filesystem_path_to_icon_name (path);

          if (perm_flags & BZ_APP_PERMISSIONS_FLAGS_FILESYSTEM_FULL)
            continue;

          if ((perm_flags & BZ_APP_PERMISSIONS_FLAGS_HOME_FULL) &&
              path->type == BZ_FILESYSTEM_PATH_HOME_SUBDIR)
            continue;

          add_row_if_permission (store,
                                 TRUE,
                                 fs_importance,
                                 fs_icon,
                                 fs_title,
                                 _ ("Can read and write all data in the directory"),
                                 NULL, NULL, NULL);
        }

      for (i = 0; filesystem_read != NULL && i < filesystem_read->len; i++)
        {
          const BzFilesystemPath *path     = g_ptr_array_index (filesystem_read, i);
          g_autofree char        *fs_title = bz_filesystem_path_to_display_string (path);
          const char             *fs_icon  = bz_filesystem_path_to_icon_name (path);

          if (perm_flags & BZ_APP_PERMISSIONS_FLAGS_FILESYSTEM_FULL)
            continue;

          if ((perm_flags & (BZ_APP_PERMISSIONS_FLAGS_FILESYSTEM_READ |
                             BZ_APP_PERMISSIONS_FLAGS_HOME_FULL |
                             BZ_APP_PERMISSIONS_FLAGS_HOME_READ)) &&
              path->type == BZ_FILESYSTEM_PATH_HOME_SUBDIR)
            continue;

          add_row_if_permission (store,
                                 TRUE,
                                 fs_importance,
                                 fs_icon,
                                 fs_title,
                                 _ ("Can read all data in the directory"),
                                 NULL, NULL, NULL);
        }

      add_row_if_permission (store,
                             !(perm_flags & (BZ_APP_PERMISSIONS_FLAGS_FILESYSTEM_FULL |
                                             BZ_APP_PERMISSIONS_FLAGS_FILESYSTEM_READ |
                                             BZ_APP_PERMISSIONS_FLAGS_FILESYSTEM_OTHER |
                                             BZ_APP_PERMISSIONS_FLAGS_HOME_FULL |
                                             BZ_APP_PERMISSIONS_FLAGS_HOME_READ |
                                             BZ_APP_PERMISSIONS_FLAGS_DOWNLOADS_FULL |
                                             BZ_APP_PERMISSIONS_FLAGS_DOWNLOADS_READ)) &&
                                 filesystem_read == NULL && filesystem_full == NULL,
                             BZ_IMPORTANCE_UNIMPORTANT,
                             "folder-symbolic",
                             _ ("No File System Access"),
                             _ ("Cannot access the file system at all"),
                             NULL, NULL, NULL);

      add_row_if_permission (store,
                             (perm_flags & BZ_APP_PERMISSIONS_FLAGS_SYSTEM_BUS) != 0,
                             BZ_IMPORTANCE_WARNING,
                             "emblem-system-symbolic",
                             _ ("Uses System Services"),
                             _ ("Can request data from non-portal system services"),
                             NULL, NULL, NULL);
      add_row_if_permission (store,
                             (perm_flags & BZ_APP_PERMISSIONS_FLAGS_SESSION_BUS) != 0,
                             BZ_IMPORTANCE_WARNING,
                             "emblem-system-symbolic",
                             _ ("Uses Session Services"),
                             _ ("Can request data from non-portal session services"),
                             NULL, NULL, NULL);

      for (i = 0; i < n_bus_policies; i++)
        {
          const BzBusPolicy *policy           = bus_policies[i];
          const char        *well_known_title = NULL;
          const char        *well_known_desc  = NULL;
          g_autofree char   *bus_title        = NULL;
          const char        *bus_description  = NULL;
          gboolean           is_system_tray   = FALSE;

          is_system_tray = g_str_equal (policy->bus_name, "org.kde.StatusNotifierWatcher") ||
                           g_str_equal (policy->bus_name, "com.canonical.indicator.application");

          if (is_system_tray && has_system_tray) // if not filtered, then there would be 2 entries for tray icon
            continue;

          if (is_system_tray)
            has_system_tray = TRUE;

          if (lookup_well_known_bus_policy (policy->bus_name, &well_known_title, &well_known_desc))
            {
              bus_title       = g_strdup (well_known_title);
              bus_description = well_known_desc;
            }
          else
            {
              bus_title       = format_bus_policy_title (policy);
              bus_description = format_bus_policy_subtitle (policy);
            }

          add_row_if_permission (store,
                                 TRUE,
                                 BZ_IMPORTANCE_WARNING,
                                 "emblem-system-symbolic",
                                 bus_title,
                                 bus_description,
                                 NULL, NULL, NULL);
        }

      add_row_if_permission (store,
                             !(perm_flags & (BZ_APP_PERMISSIONS_FLAGS_SYSTEM_BUS |
                                             BZ_APP_PERMISSIONS_FLAGS_SESSION_BUS |
                                             BZ_APP_PERMISSIONS_FLAGS_BUS_POLICY_OTHER)) &&
                                 n_bus_policies == 0,
                             BZ_IMPORTANCE_UNIMPORTANT,
                             "emblem-system-symbolic",
                             _ ("No Service Access"),
                             _ ("Cannot access non-portal session or system services at all"),
                             NULL, NULL, NULL);
    }

  add_row_if_permission (store,
                         is_verified,
                         BZ_IMPORTANCE_UNIMPORTANT,
                         "verified-checkmark-symbolic",
                         _ ("Verified App Developer"),
                         _ ("The developer of this app has been verified to be who they say they are"),
                         NULL, NULL, NULL);

  if (!is_foss)
    {
      add_row_if_permission (store,
                             TRUE,
                             BZ_IMPORTANCE_INFORMATION,
                             "proprietary-code-symbolic",
                             _ ("Proprietary Code"),
                             _ ("The source code is not public, so it cannot be independently audited and might be unsafe"),
                             NULL, NULL, NULL);
    }
  else
    {
      add_row_if_permission (store,
                             FALSE,
                             BZ_IMPORTANCE_NEUTRAL,
                             NULL, NULL, NULL,
                             "auditable-code-symbolic",
                             _ ("Auditable Code"),
                             _ ("The source code is public and can be independently audited, which makes the app more likely to be safe"));
    }

  g_clear_object (&permissions);

  return G_LIST_MODEL (store);
}

char *
bz_safety_calculator_get_top_icon (BzEntry *entry,
                                   int      index)
{
  g_autoptr (GListModel) model = NULL;
  const char  *icons[2]        = { NULL, NULL };
  guint        icon_count      = 0;
  guint        n_items         = 0;
  BzImportance priorities[]    = { BZ_IMPORTANCE_IMPORTANT, BZ_IMPORTANCE_WARNING, BZ_IMPORTANCE_INFORMATION };

  g_return_val_if_fail (BZ_IS_ENTRY (entry), NULL);

  if (index < 0 || index > 1)
    return NULL;

  model   = bz_safety_calculator_analyze_entry (entry);
  n_items = g_list_model_get_n_items (model);

  for (guint priority_idx = 0; priority_idx < 3 && icon_count < 2; priority_idx++)
    {
      BzImportance current_priority = priorities[priority_idx];

      for (guint i = 0; i < n_items && icon_count < 2; i++)
        {
          g_autoptr (BzSafetyRow) row = g_list_model_get_item (model, i);
          BzImportance importance     = BZ_IMPORTANCE_UNIMPORTANT;
          const char  *icon_name      = NULL;
          gboolean     duplicate      = FALSE;

          g_object_get (row, "importance", &importance, "icon-name", &icon_name, NULL);

          if (importance != current_priority)
            continue;

          if (icon_name == NULL || *icon_name == '\0')
            continue;

          for (guint j = 0; j < icon_count; j++)
            {
              if (g_strcmp0 (icons[j], icon_name) == 0)
                {
                  duplicate = TRUE;
                  break;
                }
            }

          if (!duplicate)
            {
              icons[icon_count] = icon_name;
              icon_count++;
            }
        }
    }

  if (icon_count == 0 || icons[index] == NULL)
    return NULL;

  return g_strdup (icons[index]);
}

BzImportance
bz_safety_calculator_calculate_rating (BzEntry *entry)
{
  g_autoptr (GListModel) model = NULL;
  BzImportance max_rating      = BZ_IMPORTANCE_UNIMPORTANT;
  guint        n_items         = 0;
  guint        i               = 0;
  gboolean     is_foss         = FALSE;

  g_return_val_if_fail (BZ_IS_ENTRY (entry), BZ_IMPORTANCE_UNIMPORTANT);

  model   = bz_safety_calculator_analyze_entry (entry);
  n_items = g_list_model_get_n_items (model);
  is_foss = bz_entry_get_is_foss (entry);

  for (i = 0; i < n_items; i++)
    {
      g_autoptr (BzSafetyRow) row = g_list_model_get_item (model, i);
      BzImportance rating         = BZ_IMPORTANCE_UNIMPORTANT;

      g_object_get (row, "importance", &rating, NULL);
      max_rating = MAX (max_rating, rating);
    }

  if (is_foss && max_rating == BZ_IMPORTANCE_WARNING)
    {
      max_rating = BZ_IMPORTANCE_INFORMATION;
    }

  if (is_foss && max_rating == BZ_IMPORTANCE_IMPORTANT)
    {
      max_rating = BZ_IMPORTANCE_WARNING;
    }

  return max_rating;
}

BzHighRiskGroup
bz_safety_calculator_get_high_risk_groups (BzEntry *entry)
{
  BzAppPermissions     *permissions = NULL;
  BzAppPermissionsFlags perm_flags  = BZ_APP_PERMISSIONS_FLAGS_NONE;
  BzHighRiskGroup       result      = BZ_HIGH_RISK_GROUP_NONE;

  g_return_val_if_fail (BZ_IS_ENTRY (entry), BZ_HIGH_RISK_GROUP_NONE);

  g_object_get (entry, "permissions", &permissions, NULL);
  if (permissions == NULL)
    return BZ_HIGH_RISK_GROUP_NONE;

  perm_flags = bz_app_permissions_get_flags (permissions);

  if (perm_flags & BZ_APP_PERMISSIONS_FLAGS_X11)
    result |= BZ_HIGH_RISK_GROUP_X11;

  if (perm_flags & (BZ_APP_PERMISSIONS_FLAGS_FILESYSTEM_FULL |
                    BZ_APP_PERMISSIONS_FLAGS_FILESYSTEM_READ |
                    BZ_APP_PERMISSIONS_FLAGS_HOME_FULL |
                    BZ_APP_PERMISSIONS_FLAGS_HOME_READ |
                    BZ_APP_PERMISSIONS_FLAGS_ESCAPE_SANDBOX))
    result |= BZ_HIGH_RISK_GROUP_DISK;

  g_clear_object (&permissions);

  return result;
}

static char *
format_bus_policy_title (const BzBusPolicy *bus_policy)
{
  if (bus_policy->bus_type == G_BUS_TYPE_SYSTEM)
    {
      return g_strdup_printf (_ ("Use the %s System Service"), bus_policy->bus_name);
    }
  else if (bus_policy->bus_type == G_BUS_TYPE_SESSION)
    {
      return g_strdup_printf (_ ("Use the %s Session Service"), bus_policy->bus_name);
    }
  else
    {
      return g_strdup_printf (_ ("Use the %s Service"), bus_policy->bus_name);
    }
}

static const char *
format_bus_policy_subtitle (const BzBusPolicy *bus_policy)
{
  switch (bus_policy->permission)
    {
    case BZ_BUS_POLICY_PERMISSION_SEE:
      return _ ("Can see the non-portal service");
    case BZ_BUS_POLICY_PERMISSION_TALK:
      return _ ("Can talk to the non-portal service");
    case BZ_BUS_POLICY_PERMISSION_OWN:
      return _ ("Can own the non-portal service");
    case BZ_BUS_POLICY_PERMISSION_NONE:
    case BZ_BUS_POLICY_PERMISSION_UNKNOWN:
    default:
      g_assert_not_reached ();
    }
}

static gboolean
lookup_well_known_bus_policy (const char  *bus_name,
                              const char **out_title,
                              const char **out_description)
{
  if (g_str_equal (bus_name, "com.canonical.AppMenu.Registrar"))
    {
      *out_title       = _ ("Global Menu Integration");
      *out_description = _ ("Can display its menus in a global menu bar");
      return TRUE;
    }
  if (g_str_equal (bus_name, "org.kde.kconfig.notify"))
    {
      *out_title       = _ ("KDE Settings Integration");
      *out_description = _ ("Can detect when KDE desktop settings change");
      return TRUE;
    }
  if (g_str_equal (bus_name, "org.kde.KGlobalSettings"))
    {
      *out_title       = _ ("KDE Global Settings");
      *out_description = _ ("Can read KDE desktop preferences like fonts and colors");
      return TRUE;
    }
  if (g_str_equal (bus_name, "org.freedesktop.secrets"))
    {
      *out_title       = _ ("Secret Storage Service");
      *out_description = _ ("Can store and retrieve its own passwords using the system keyring");
      return TRUE;
    }
  if (g_str_equal (bus_name, "org.freedesktop.Notifications"))
    {
      *out_title       = _ ("Desktop Notifications Service");
      *out_description = _ ("Can send desktop notifications");
      return TRUE;
    }
  if (g_str_equal (bus_name, "org.kde.StatusNotifierWatcher") ||
      g_str_equal (bus_name, "com.canonical.indicator.application"))
    {
      *out_title       = _ ("System Tray Integration");
      *out_description = _ ("Can display an icon in the system tray");
      return TRUE;
    }
  if (g_str_equal (bus_name, "org.kde.kdeconnect"))
    {
      *out_title       = _ ("KDE Connect Integration");
      *out_description = _ ("Can interact with devices paired via KDE Connect");
      return TRUE;
    }

  return FALSE;
}

static void
add_row_if_permission (GListStore  *store,
                       gboolean     has_permission,
                       BzImportance item_rating,
                       const char  *icon_name_with_permission,
                       const char  *title_with_permission,
                       const char  *description_with_permission,
                       const char  *icon_name_without_permission,
                       const char  *title_without_permission,
                       const char  *description_without_permission)
{
  BzSafetyRow *row = NULL;

  if (!has_permission && title_without_permission == NULL)
    return;

  row = bz_safety_row_new ();
  g_object_set (row,
                "importance", has_permission ? item_rating : BZ_IMPORTANCE_UNIMPORTANT,
                "icon-name", has_permission ? icon_name_with_permission : icon_name_without_permission,
                "title", has_permission ? title_with_permission : title_without_permission,
                "subtitle", has_permission ? description_with_permission : description_without_permission,
                NULL);

  g_list_store_append (store, row);
  g_object_unref (row);
}
