/*
 * Copyright (C) 2026 Furi Labs
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Bardia Moshiri <bardia@furilabs.com>
 */

#pragma once

#include "pt-security-settings.h"

G_BEGIN_DECLS

typedef struct _PtSecuritySettingsPrivate
{
  AdwPasswordEntryRow *password_entry;
  AdwPasswordEntryRow *verify_entry;
  gboolean ready;
  ApplyCallback apply_cb;
  gpointer apply_user_data;

  GDBusProxy *fingerprint_proxy;
  GDBusProxy *props_proxy;
  GtkWidget *fingerprint_group;
  AdwToastOverlay *toast_overlay;
  GtkListBox *finger_list;
  GtkWidget *enroll_step;
  GtkProgressBar *enroll_progress;
} PtSecuritySettingsPrivate;

void pt_security_settings_register_fingerprint (PtSecuritySettings *self);
gboolean pt_security_settings_fingerprint_available (void);
void pt_security_settings_fingerprint_finalize (PtSecuritySettings *self);
PtSecuritySettingsPrivate *pt_security_settings_get_priv (PtSecuritySettings *self);

G_END_DECLS
