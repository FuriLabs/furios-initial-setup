#pragma once

#include <adwaita.h>
#include <pt-page.h>

G_BEGIN_DECLS

#define PT_TYPE_SECURITY_SETTINGS (pt_security_settings_get_type ())
G_DECLARE_DERIVABLE_TYPE (PtSecuritySettings, pt_security_settings, PT, SECURITY_SETTINGS, AdwBin)

struct _PtSecuritySettingsClass
{
  AdwBinClass parent_class;
};

PtSecuritySettings          *pt_security_settings_new               (void);

void pt_security_settings_apply (GObject *self, ApplyCallback cb, gpointer user_data);

G_END_DECLS
