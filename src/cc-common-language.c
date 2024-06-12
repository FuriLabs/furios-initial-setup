/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright 2009-2010  Red Hat, Inc,
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Written by: Matthias Clasen <mclasen@redhat.com>
 */

#include "furios-initial-setup-config.h"

#include <stdlib.h>
#include <locale.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include <fontconfig/fontconfig.h>

#define GNOME_DESKTOP_USE_UNSTABLE_API
#include <libgnome-desktop/gnome-languages.h>

#include "cc-common-language.h"

static char *current_language;

gboolean
cc_common_language_has_font (const gchar *locale)
{
  const FcCharSet *charset;
  FcPattern       *pattern;
  FcObjectSet     *object_set;
  FcFontSet       *font_set;
  gchar           *language_code;
  gboolean         is_displayable;

  is_displayable = FALSE;
  pattern = NULL;
  object_set = NULL;
  font_set = NULL;

  if (!gnome_parse_locale (locale, &language_code, NULL, NULL, NULL))
    return FALSE;

  charset = FcLangGetCharSet ((FcChar8 *) language_code);
  if (!charset) {
    /* fontconfig does not know about this language */
    is_displayable = TRUE;
  } else {
    /* see if any fonts support rendering it */
    pattern = FcPatternBuild (NULL, FC_LANG, FcTypeString, language_code, NULL);

    if (pattern == NULL)
      goto done;

    object_set = FcObjectSetCreate ();

    if (object_set == NULL)
      goto done;

    font_set = FcFontList (NULL, pattern, object_set);

    if (font_set == NULL)
      goto done;

    is_displayable = (font_set->nfont > 0);
  }

done:
  if (font_set != NULL)
    FcFontSetDestroy (font_set);

  if (object_set != NULL)
    FcObjectSetDestroy (object_set);

  if (pattern != NULL)
    FcPatternDestroy (pattern);

  g_free (language_code);

  return is_displayable;
}

gchar *
cc_common_language_get_current_language (void)
{
  g_assert (current_language != NULL);
  return g_strdup (current_language);
}

void
cc_common_language_set_current_language (const char *locale)
{
  g_clear_pointer (&current_language, g_free);
  current_language = gnome_normalize_locale (locale);
}

/*
 * Note that @lang needs to be formatted like the locale strings
 * returned by gnome_get_all_locales().
 */
static void
insert_language (GHashTable *ht,
                 const char *lang)
{
  locale_t locale;
  char *label_own_lang;
  char *label_current_lang;
  char *label_untranslated;
  char *key;

  locale = newlocale (LC_ALL_MASK, lang, (locale_t) 0);
  if (locale == (locale_t) 0) {
    g_debug ("%s: Failed to create locale %s", G_STRFUNC, lang);
    return;
  }
  freelocale (locale);


  key = g_strdup (lang);

  label_own_lang = gnome_get_language_from_locale (key, key);
  label_current_lang = gnome_get_language_from_locale (key, current_language);
  label_untranslated = gnome_get_language_from_locale (key, "C");

  /* We don't have a translation for the label in
   * its own language? */
  if (label_own_lang == NULL || g_strcmp0 (label_own_lang, label_untranslated) == 0) {
    if (g_strcmp0 (label_current_lang, label_untranslated) == 0)
      g_hash_table_insert (ht, key, g_strdup (label_untranslated));
    else
      g_hash_table_insert (ht, key, g_strdup (label_current_lang));
  } else {
    g_hash_table_insert (ht, key, g_strdup (label_own_lang));
  }

  g_free (label_own_lang);
  g_free (label_current_lang);
  g_free (label_untranslated);
}

GHashTable *
cc_common_language_get_initial_languages (void)
{
  GHashTable *ht;

  ht = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

  insert_language (ht, "en_US.UTF-8");
  insert_language (ht, "es_ES.UTF-8");
  insert_language (ht, "de_DE.UTF-8");
  insert_language (ht, "fr_FR.UTF-8");
  insert_language (ht, "zh_CN.UTF-8");
  insert_language (ht, "ja_JP.UTF-8");
  insert_language (ht, "ru_RU.UTF-8");

  return ht;
}
