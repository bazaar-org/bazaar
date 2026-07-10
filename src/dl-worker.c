/* dl-worker.c
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

#define G_LOG_DOMAIN "BAZAAR::DL-WORKER-SUBPROCESS"

#include "env.h"
#include "global-net.h"
#include "util.h"

BZ_DEFINE_DATA (
    main,
    Main,
    {
      GMainLoop  *loop;
      GIOChannel *stdout_channel;
    },
    BZ_RELEASE_DATA (loop, g_main_loop_unref);
    BZ_RELEASE_DATA (stdout_channel, g_io_channel_unref));

BZ_DEFINE_DATA (
    download,
    Download,
    {
      char       *src;
      char       *dest;
      GIOChannel *stdout_channel;
    },
    BZ_RELEASE_DATA (src, g_free);
    BZ_RELEASE_DATA (dest, g_free);
    BZ_RELEASE_DATA (stdout_channel, g_io_channel_unref));

static DexFuture *
read_stdin (MainData *data);

static DexFuture *
download_fiber (DownloadData *data);

int
main (int   argc,
      char *argv[])
{
  g_autoptr (GIOChannel) stdout_channel = NULL;
  g_autoptr (GMainLoop) main_loop       = NULL;
  g_autoptr (MainData) data             = NULL;
  g_autoptr (DexFuture) future          = NULL;

  g_log_writer_default_set_use_stderr (TRUE);
  dex_init ();

  stdout_channel = g_io_channel_unix_new (STDOUT_FILENO);
  g_assert (g_io_channel_set_encoding (stdout_channel, NULL, NULL));
  g_io_channel_set_buffered (stdout_channel, FALSE);

  main_loop = g_main_loop_new (NULL, FALSE);

  data                 = main_data_new ();
  data->loop           = g_main_loop_ref (main_loop);
  data->stdout_channel = g_io_channel_ref (stdout_channel);

  future = dex_scheduler_spawn (
      dex_thread_pool_scheduler_get_default (),
      bz_get_dex_stack_size (),
      (DexFiberFunc) read_stdin,
      main_data_ref (data), main_data_unref);
  g_main_loop_run (main_loop);

  return EXIT_SUCCESS;
}

static DexFuture *
read_stdin (MainData *data)
{
  g_autoptr (GIOChannel) stdin_channel = NULL;

  stdin_channel = g_io_channel_unix_new (STDIN_FILENO);
  for (;;)
    {
      g_autoptr (GError) local_error   = NULL;
      g_autofree char *string          = NULL;
      char            *newline         = NULL;
      g_autoptr (GVariant) variant     = NULL;
      g_autofree char *src_uri         = NULL;
      g_autofree char *dest_path       = NULL;
      g_autoptr (DownloadData) dl_data = NULL;

      g_io_channel_read_line (
          stdin_channel, &string, NULL, NULL, &local_error);
      if (string == NULL)
        {
          if (local_error != NULL)
            g_warning ("FATAL: Failure reading stdin channel: %s", local_error->message);
          g_main_loop_quit (data->loop);
          return NULL;
        }

      newline = g_utf8_strchr (string, -1, '\n');
      if (newline != NULL)
        *newline = '\0';

      variant = g_variant_parse (
          G_VARIANT_TYPE ("(ss)"),
          string, NULL, NULL,
          &local_error);
      if (variant == NULL)
        {
          g_warning ("Failure parsing variant text '%s' into structure: %s\n",
                     string, local_error->message);
          g_main_loop_quit (data->loop);
          continue;
        }

      g_variant_get (variant, "(ss)", &src_uri, &dest_path);

      dl_data                 = download_data_new ();
      dl_data->src            = g_steal_pointer (&src_uri);
      dl_data->dest           = g_steal_pointer (&dest_path);
      dl_data->stdout_channel = g_io_channel_ref (data->stdout_channel);

      dex_future_disown (dex_scheduler_spawn (
          dex_scheduler_get_default (),
          bz_get_dex_stack_size (),
          (DexFiberFunc) download_fiber,
          download_data_ref (dl_data), download_data_unref));
    }

  return NULL;
}

static DexFuture *
download_fiber (DownloadData *data)
{
  gboolean success                          = FALSE;
  g_autoptr (GError) local_error            = NULL;
  g_autoptr (GFile) dest_file               = NULL;
  g_autoptr (GFileOutputStream) dest_output = NULL;
  g_autoptr (SoupMessage) message           = NULL;
  SoupMessageHeaders *headers               = NULL;
  g_autoptr (GVariant) variant              = NULL;
  g_autofree char *output                   = NULL;
  g_autofree char *output_plus_nl           = NULL;

  dest_file   = g_file_new_for_path (data->dest);
  dest_output = g_file_replace (
      dest_file, NULL, FALSE,
      G_FILE_CREATE_REPLACE_DESTINATION,
      NULL, &local_error);
  if (dest_output == NULL)
    {
      g_warning ("%s", local_error->message);
      goto done;
    }

  message = soup_message_new (SOUP_METHOD_GET, data->src);

  headers = soup_message_get_request_headers (message);
  soup_message_headers_append (headers, "Accept", "*/*");
  soup_message_headers_append (headers, "User-Agent", "Bazaar");

  success = dex_await (bz_send_with_global_http_session_then_splice_into (
                           message, G_OUTPUT_STREAM (dest_output)),
                       &local_error);
  if (!success)
    {
      g_warning ("%s", local_error->message);
      goto done;
    }

done:
  variant        = g_variant_new ("(sb)", data->dest, success);
  output         = g_variant_print (variant, TRUE);
  output_plus_nl = g_strdup_printf ("%s\n", output);

  g_io_channel_write_chars (data->stdout_channel, output_plus_nl, -1, NULL, NULL);

  return dex_future_new_true ();
}
