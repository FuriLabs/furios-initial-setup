/*
 * Copyright (C) 2022 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "pt-accounts"

#include "furios-initial-setup-config.h"
#include "pt-online-accounts.h"
#include "pt-page.h"
#include "cc-online-account-provider-row.h"
#include "cc-online-account-row.h"

#define GOA_API_IS_SUBJECT_TO_CHANGE
#include <goa/goa.h>

#include <adwaita.h>
#include <glib/gi18n.h>


typedef struct _PtOnlineAccountsPrivate
{
  GtkListBox *accounts_listbox;
  GtkListBox *providers_listbox;
  GtkFrame   *accounts_frame;

  GoaClient *client;
} PtOnlineAccountsPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (PtOnlineAccounts, pt_online_accounts, ADW_TYPE_BIN)

static void
pt_online_accounts_finalize (GObject *object)
{
  G_OBJECT_CLASS (pt_online_accounts_parent_class)->finalize (object);
}

static int
goa_provider_priority (const char *provider_type)
{
  static const char *goa_priority[] = {
    "owncloud",     /* Nextcloud */
    "google",       /* Google */
    "windows_live", /* Microsoft Personal */
    "ms_graph",     /* Microsoft 365 */
    "exchange",     /* Microsoft Exchange */
    "imap_smtp",    /* Email (IMAP and SMTP) */
    "webdav",       /* Calendars, Contacts, Files (WebDAV) */
    "kerberos",     /* Enterprise Login (Kerberos) */
    "fedora",       /* Fedora */
  };

  for (size_t i = 0; i < G_N_ELEMENTS (goa_priority); i++) {
    if (g_str_equal (goa_priority[i], provider_type))
      return i;
  }

  /* New or unknown providers are sorted last */
  return G_N_ELEMENTS (goa_priority) + 1;
}

static int
sort_accounts_func (GtkListBoxRow *a,
                    GtkListBoxRow *b,
                    gpointer user_data)
{
  GoaAccount *a_account, *b_account;
  GoaObject *a_object, *b_object;
  const char *a_name, *b_name;

  a_object = cc_online_account_row_get_object (CC_ONLINE_ACCOUNT_ROW (a));
  a_account = goa_object_peek_account (a_object);
  a_name = goa_account_get_provider_type (a_account);

  b_object = cc_online_account_row_get_object (CC_ONLINE_ACCOUNT_ROW (b));
  b_account = goa_object_peek_account (b_object);
  b_name = goa_account_get_provider_type (b_account);

  return goa_provider_priority (a_name) - goa_provider_priority (b_name);
}

static int
sort_providers_func (GtkListBoxRow *a,
                     GtkListBoxRow *b,
                     gpointer user_data)
{
  GoaProvider *a_provider, *b_provider;
  const char *a_name, *b_name;

  a_provider = cc_online_account_provider_row_get_provider (CC_ONLINE_ACCOUNT_PROVIDER_ROW (a));
  a_name = goa_provider_get_provider_type (a_provider);

  b_provider = cc_online_account_provider_row_get_provider (CC_ONLINE_ACCOUNT_PROVIDER_ROW (b));
  b_name = goa_provider_get_provider_type (b_provider);

  return goa_provider_priority (a_name) - goa_provider_priority (b_name);
}

static void
add_account (PtOnlineAccounts *self,
             GoaObject *object)
{
  CcOnlineAccountRow *row;
  PtOnlineAccountsPrivate *priv = pt_online_accounts_get_instance_private (self);

  row = cc_online_account_row_new (object);
  gtk_list_box_append (priv->accounts_listbox, GTK_WIDGET (row));
  gtk_widget_set_visible (GTK_WIDGET (priv->accounts_frame), TRUE);
}

typedef void (*RowForAccountCallback) (PtOnlineAccounts *self, GtkWidget *row, GList *other_rows);

static void
modify_row_for_account (PtOnlineAccounts      *self,
                        GoaObject             *object,
                        RowForAccountCallback  callback)
{
  GtkWidget *child;
  GList *l;
  PtOnlineAccountsPrivate *priv = pt_online_accounts_get_instance_private (self);
  GList *children = NULL;

  for (child = gtk_widget_get_first_child (GTK_WIDGET (priv->accounts_listbox));
       child;
       child = gtk_widget_get_next_sibling (child))
    {
      children = g_list_prepend (children, child);
    }

  children = g_list_reverse (children);

  for (l = children; l != NULL; l = l->next)
    {
      GoaObject *row_object;

      row_object = cc_online_account_row_get_object (CC_ONLINE_ACCOUNT_ROW (l->data));
      if (row_object == object)
        {
          GtkWidget *row = GTK_WIDGET (l->data);

          children = g_list_remove_link (children, l);
          callback (self, row, children);
          g_list_free (l);
          break;
        }
    }

  g_list_free (children);
}

static void
add_provider (PtOnlineAccounts *self,
              GoaProvider *provider)
{
  CcOnlineAccountProviderRow *row;
  PtOnlineAccountsPrivate *priv = pt_online_accounts_get_instance_private (self);

  row = cc_online_account_provider_row_new (provider);
  gtk_list_box_append (priv->providers_listbox, GTK_WIDGET (row));
}

static void
on_account_added_cb (PtOnlineAccounts *self,
                     GoaObject *object)
{
  add_account (self, object);
}

static void
remove_row_for_account_cb (PtOnlineAccounts      *self,
                           GtkWidget             *row,
                           GList                 *other_rows)
{
  PtOnlineAccountsPrivate *priv = pt_online_accounts_get_instance_private (self);

  gtk_list_box_remove (priv->accounts_listbox, row);
  gtk_widget_set_visible (GTK_WIDGET (priv->accounts_frame), other_rows != NULL);
}

static void
on_account_removed_cb (PtOnlineAccounts *self,
                       GoaObject *object)
{
  modify_row_for_account (self, object, remove_row_for_account_cb);
}

static void
create_account_cb (GoaProvider *provider,
                   GAsyncResult *result,
                   PtOnlineAccounts *self)
{
  g_autoptr (GoaObject) object = NULL;
  g_autoptr (GError) error = NULL;

  object = goa_provider_add_account_finish (provider, result, &error);
  if (error != NULL) {
    if (!g_error_matches (error, GOA_ERROR, GOA_ERROR_DIALOG_DISMISSED) && !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      g_warning ("Error creating account: %s", error->message);

    return;
  }

  // show_account (self, object);
}

static void
create_account (PtOnlineAccounts *self,
                GoaProvider *provider)
{
  GtkRoot *parent;
  PtOnlineAccountsPrivate *priv;

  g_return_if_fail (GOA_IS_PROVIDER (provider));

  priv = pt_online_accounts_get_instance_private (self);
  parent = gtk_widget_get_root (GTK_WIDGET (self));
  goa_provider_add_account (provider,
                            priv->client,
                            GTK_WINDOW (parent),
                            NULL,
                            (GAsyncReadyCallback) create_account_cb,
                            self);
}

static void
on_provider_row_activated_cb (PtOnlineAccounts *self,
                              GtkListBoxRow *activated_row)
{
  GoaProvider *provider = cc_online_account_provider_row_get_provider (CC_ONLINE_ACCOUNT_PROVIDER_ROW (activated_row));

  create_account (self, provider);
}

static void
goa_provider_get_all_cb (GObject *object,
                         GAsyncResult *res,
                         gpointer user_data)
{
  g_autoptr (PtOnlineAccounts) self = PT_ONLINE_ACCOUNTS (user_data);
  g_autolist (GoaProvider) providers = NULL;
  g_autolist (GoaAccount) accounts = NULL;
  g_autoptr (GError) error = NULL;
  PtOnlineAccountsPrivate *priv = pt_online_accounts_get_instance_private (self);

  if (!goa_provider_get_all_finish (&providers, res, &error)) {
    g_warning ("Error listing providers: %s", error->message);
    return;
  }

  for (const GList *iter = providers; iter != NULL; iter = iter->next)
    add_provider (self, GOA_PROVIDER (iter->data));

  /* Load existing accounts */
  accounts = goa_client_get_accounts (priv->client);

  for (const GList *iter = accounts; iter != NULL; iter = iter->next)
    add_account (self, GOA_OBJECT (iter->data));

  g_signal_connect_swapped (priv->client,
                            "account-added",
                            G_CALLBACK (on_account_added_cb),
                            self);

  g_signal_connect_swapped (priv->client,
                            "account-removed",
                            G_CALLBACK (on_account_removed_cb),
                            self);
}

static void
goa_client_new_cb (GObject *object,
                   GAsyncResult *res,
                   gpointer user_data)
{
  PtOnlineAccounts *self = PT_ONLINE_ACCOUNTS (user_data);
  g_autoptr (GError) error = NULL;
  PtOnlineAccountsPrivate *priv = pt_online_accounts_get_instance_private (self);

  priv->client = goa_client_new_finish (res, &error);
  if (priv->client == NULL) {
    g_warning ("Error connect to service: %s", error->message);
    gtk_widget_set_sensitive (GTK_WIDGET (self), FALSE);
    return;
  }

  goa_provider_get_all (goa_provider_get_all_cb, g_object_ref (self));
}

static void
pt_online_accounts_class_init (PtOnlineAccountsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = pt_online_accounts_finalize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                              "/mobi/phosh/PhoshTour/ui/pt-online-accounts.ui");

  gtk_widget_class_bind_template_child_private (widget_class, PtOnlineAccounts, accounts_frame);
  gtk_widget_class_bind_template_child_private (widget_class, PtOnlineAccounts, accounts_listbox);
  gtk_widget_class_bind_template_child_private (widget_class, PtOnlineAccounts, providers_listbox);

  gtk_widget_class_bind_template_callback (widget_class, on_provider_row_activated_cb);
}

static void
pt_online_accounts_init (PtOnlineAccounts *self)
{
  PtOnlineAccountsPrivate *priv;
  gtk_widget_init_template (GTK_WIDGET (self));

  priv = pt_online_accounts_get_instance_private (self);

  gtk_list_box_set_sort_func (priv->accounts_listbox, sort_accounts_func, self, NULL);
  gtk_list_box_set_sort_func (priv->providers_listbox, sort_providers_func, self, NULL);

  goa_client_new (NULL, goa_client_new_cb, self);
}

PtOnlineAccounts *
pt_online_accounts_new (void)
{
  return PT_ONLINE_ACCOUNTS (g_object_new (PT_TYPE_ONLINE_ACCOUNTS, NULL));
}
