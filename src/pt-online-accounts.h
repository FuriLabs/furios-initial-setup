#pragma once

#include <adwaita.h>

G_BEGIN_DECLS

#define PT_TYPE_ONLINE_ACCOUNTS (pt_online_accounts_get_type ())
G_DECLARE_DERIVABLE_TYPE (PtOnlineAccounts, pt_online_accounts, PT, ONLINE_ACCOUNTS, AdwBin)

struct _PtOnlineAccountsClass
{
  AdwBinClass parent_class;
};

PtOnlineAccounts          *pt_online_accounts_new               (void);

G_END_DECLS
