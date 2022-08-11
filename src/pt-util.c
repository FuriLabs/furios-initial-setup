/*
 * Copyright (C) 2022 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include <pt-util.h>
#include <glib/gi18n.h>

guint
pt_string_replace (GString     *string,
                   const gchar *find,
                   const gchar *replace,
                   guint        limit)
{
#if GLIB_CHECK_VERSION(2, 68, 0)
  return g_string_replace (string, find, replace, limit);
#else
  /* from glib until we can requirek 2.68 */
  gsize f_len, r_len, pos;
  gchar *cur, *next;
  guint n = 0;

  g_return_val_if_fail (string != NULL, 0);
  g_return_val_if_fail (find != NULL, 0);
  g_return_val_if_fail (replace != NULL, 0);

  f_len = strlen (find);
  r_len = strlen (replace);
  cur = string->str;

  while ((next = strstr (cur, find)) != NULL)
  {
    pos = next - string->str;
    g_string_erase (string, pos, f_len);
    g_string_insert (string, pos, replace);
    cur = string->str + pos + r_len;
    n++;
    /* Only match the empty string once at any given position, to
     * avoid infinite loops */
    if (f_len == 0) {
      if (cur[0] == '\0')
        break;
      else
        cur++;
    }
    if (n == limit)
      break;
  }

  return n;
#endif
}
