/*
 * Copyright (C) 2025 Furi labs
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Bardia Moshiri <bardia@furilabs.com>
 *         Jesus Higueras <jesus@furilabs.com>
 */

#pragma once

#include <adwaita.h>
#include <pt-page.h>

G_BEGIN_DECLS

#define PT_TYPE_UPDATE_PROGRESS (pt_update_progress_get_type ())
G_DECLARE_DERIVABLE_TYPE (PtUpdateProgress, pt_update_progress, PT, UPDATE_PROGRESS, AdwBin)

struct _PtUpdateProgressClass
{
  AdwBinClass parent_class;
};

PtUpdateProgress         *pt_update_progress_new (void);
void                      pt_update_progress_begin (PtUpdateProgress *self);
void                      pt_update_progress_skip (PtUpdateProgress *self);

G_END_DECLS
