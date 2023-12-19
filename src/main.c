/* main.c
 *
 * Copyright 2022 Guido Günther
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <glib/gi18n.h>

#include "phosh-tour-config.h"
#include "phosh-tour-application.h"

int
main (int argc, char *argv[])
{
  g_autoptr (PhoshTourApplication) app = NULL;
  int ret;

  /* Set up gettext translations */
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  app = phosh_tour_application_new (PHOSH_TOUR_APP_ID,
#if GLIB_CHECK_VERSION(2,74,0)
                                         G_APPLICATION_DEFAULT_FLAGS);
#else
                                         G_APPLICATION_FLAGS_NONE);
#endif
  ret = g_application_run (G_APPLICATION (app), argc, argv);

  return ret;
}
