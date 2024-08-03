/* GStreamer
 * Copyright (C) 2024 Jan Schmidt <jan@centricular.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/*
 * This example uses splitmuxsrc to play a set of splitmuxed-files,
 * by reading the set of files and their playback offsets from a CSV
 * file generated by splitmuxsink-fragment-info or splitmuxsrc-extract
 * and providing them to splitmuxsrc via the `add-fragment` signal.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <gst/gst.h>

GMainLoop *loop;
gchar **fragment_lines;

static gboolean
message_handler (GstBus * bus, GstMessage * message, gpointer data)
{
  if (message->type == GST_MESSAGE_ERROR) {
    GError *err;
    gchar *debug_info;

    gst_message_parse_error (message, &err, &debug_info);
    g_printerr ("Error received from element %s: %s\n",
        GST_OBJECT_NAME (message->src), err->message);
    g_printerr ("Debugging information: %s\n",
        debug_info ? debug_info : "none");
    g_main_loop_quit (loop);
  }
  if (message->type == GST_MESSAGE_EOS) {
    g_main_loop_quit (loop);
  }
  return TRUE;
}

static void
setup_splitmuxsrc (GstElement * playbin, GstElement * src, gpointer userdata)
{
  /* Read fragment info from csv lines and add to splitmuxsrc */
  gsize i = 0;
  for (i = 0; fragment_lines[i] != NULL; i++) {
    gchar *fragment = fragment_lines[i];
    if (fragment[0] == '\0')
      continue;

    gchar *fname = NULL;
    GstClockTime start_offset, duration;
    const char *fmt = "\"%m[^\"]\",%" G_GUINT64_FORMAT ",%" G_GUINT64_FORMAT;
    int ret = sscanf (fragment, fmt, &fname, &start_offset, &duration);
    if (ret != 3) {
      g_printerr ("failed to parse line %" G_GSIZE_FORMAT ": %s\n", i,
          fragment);
      g_main_loop_quit (loop);
      return;
    }
#if 0
    g_print ("Adding fragment \"%s\",%" G_GUINT64_FORMAT ",%" G_GUINT64_FORMAT
        "\n", fname, start_offset, duration);
#endif
    gboolean add_result = FALSE;

    g_signal_emit_by_name (G_OBJECT (src), "add-fragment", fname, start_offset,
        duration, &add_result);
    if (!add_result) {
      g_printerr ("Failed to add fragment %" G_GSIZE_FORMAT ": %s\n", i, fname);
      g_main_loop_quit (loop);
      return;
    }
    free (fname);
  }
}

int
main (int argc, char *argv[])
{
  GstElement *pipe;
  GstBus *bus;

  gst_init (&argc, &argv);

  if (argc < 2) {
    g_printerr
        ("Usage: %s fragments.csv\n  Pass a fragment info csv (from splitmuxsrc-extract) with fragment info to load\n",
        argv[0]);
    return 1;
  }

  GError *err = NULL;
  gchar *fragment_info;

  if (!g_file_get_contents (argv[1], &fragment_info, NULL, &err)) {
    g_printerr ("Failed to open fragment info file %s. Error %s", argv[1],
        err->message);
    g_clear_error (&err);
    return 2;
  }
  fragment_lines = g_strsplit (fragment_info, "\n", 0);
  g_free (fragment_info);

  pipe = gst_element_factory_make ("playbin3", NULL);

  /* Connect to source-setup to set fragments on splitmuxsrc */
  g_signal_connect (pipe, "source-setup", G_CALLBACK (setup_splitmuxsrc), NULL);
  g_object_set (pipe, "uri", "splitmux://", NULL);

  bus = gst_element_get_bus (pipe);
  gst_bus_add_watch (bus, message_handler, NULL);
  gst_object_unref (bus);

  gst_element_set_state (pipe, GST_STATE_PLAYING);

  loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (loop);

  gst_element_set_state (pipe, GST_STATE_NULL);

  gst_object_unref (pipe);

  return 0;
}