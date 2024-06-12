/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Red Hat
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Written by:
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 *     Matthias Clasen <mclasen@redhat.com>
 */

#pragma once

#include <gtk/gtk.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define CC_TYPE_LANGUAGE_CHOOSER            (cc_language_chooser_get_type ())

G_DECLARE_FINAL_TYPE (CcLanguageChooser, cc_language_chooser, CC, LANGUAGE_CHOOSER, GtkBox)


const gchar * cc_language_chooser_get_language (CcLanguageChooser *chooser);
void          cc_language_chooser_set_language (CcLanguageChooser *chooser,
                                                const gchar        *language);
gboolean      cc_language_chooser_get_showing_extra (CcLanguageChooser *chooser);
void          cc_language_chooser_set_showing_extra (CcLanguageChooser *chooser,
                                                     gboolean            showing_extra);
void          cc_language_chooser_apply (CcLanguageChooser *chooser);

G_END_DECLS
