/* main.c
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

#define G_LOG_DOMAIN "BAZAAR::MAIN"

#include "config.h"

#include <bge.h>
#include <glib/gi18n.h>
#include <libdex.h>

#include "bz-application.h"

int
main (int   argc,
      char *argv[])
{
  g_autoptr (BzApplication) app = NULL;
  int result                    = 0;

  if (argc > 1 && g_strcmp0 (argv[1], "--version") == 0)
    {
      g_print ("%s\n", PACKAGE_VCS_VERSION);
      return 0;
    }

  g_debug ("Initializing libdex...");
  dex_init ();

  g_debug ("Configuring textdomain...");
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  /* Init Bazaar GTK Extensions */
  bge_init ();

  g_debug ("Constructing main application object...");
  app = g_object_new (
      BZ_TYPE_APPLICATION,
      "application-id", "io.github.kolunmi.Bazaar",
      "flags", G_APPLICATION_HANDLES_COMMAND_LINE,
      "resource-base-path", "/io/github/kolunmi/Bazaar",
      NULL);

  g_debug ("Running!");
  result = g_application_run (G_APPLICATION (app), argc, argv);

  return result;
}
