#include <adwaita.h>
#include <libintl.h>

#include "emerge-application.h"

#define GETTEXT_PACKAGE "emerge"
#define LOCALEDIR "/usr/local/share/locale"

int
main (int argc, char *argv[])
{
  g_autoptr (EmergeApplication) app = NULL;
  int ret;

  /* Set up gettext translations */
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  app = emerge_application_new ("com.github.emerge", G_APPLICATION_DEFAULT_FLAGS);
  ret = g_application_run (G_APPLICATION (app), argc, argv);

  return ret;
} 