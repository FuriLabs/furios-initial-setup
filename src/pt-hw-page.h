/*
 * Copyright (C) 2024 Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <pt-page.h>

G_BEGIN_DECLS

#define PT_TYPE_HW_PAGE (pt_hw_page_get_type ())

G_DECLARE_FINAL_TYPE (PtHwPage, pt_hw_page, PT, HW_PAGE, PtPage)

PtHwPage *pt_hw_page_new (void);

G_END_DECLS
