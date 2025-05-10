#include <adwaita.h>
#include <libintl.h>

#include "stable-gtk-application.h"

#define GETTEXT_PACKAGE "stable-gtk"
#define LOCALEDIR "/usr/local/share/locale"

int
main (int argc, char *argv[])
{
  g_autoptr (StableGtkApplication) app = NULL;
  int ret;

  /* Set up gettext translations */
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  app = stable_gtk_application_new ("com.github.stable-gtk", G_APPLICATION_DEFAULT_FLAGS);
  ret = g_application_run (G_APPLICATION (app), argc, argv);

  return ret;
} 