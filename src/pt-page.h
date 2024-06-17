/*
 * Copyright (C) 2022 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <adwaita.h>

G_BEGIN_DECLS

#define PT_TYPE_PAGE (pt_page_get_type ())
G_DECLARE_DERIVABLE_TYPE (PtPage, pt_page, PT, PAGE, AdwBin)

typedef void (*ApplyCallback) (GObject *self, gboolean ok, gpointer user_data);

struct _PtPageClass
{
  AdwBinClass parent_class;
};

PtPage          *pt_page_new               (void);
void             pt_page_set_summary       (PtPage *self, const char *summary);
void             pt_page_set_explanation   (PtPage *self, const char *explanation);
void             pt_page_set_image_uri     (PtPage *self, const char *uri);
void             pt_page_set_widget        (PtPage *self, GtkWidget *widget);
GtkWidget       *pt_page_get_widget        (PtPage *self);
void             pt_page_set_can_proceed   (PtPage *self, gboolean can_proceed);
gboolean         pt_page_get_can_proceed   (PtPage *self);
void             pt_page_set_subpage       (PtPage *self, PtPage *subpage);
PtPage          *pt_page_get_subpage       (PtPage *self);
void             pt_page_switch_to_subpage (PtPage *self);

G_END_DECLS
