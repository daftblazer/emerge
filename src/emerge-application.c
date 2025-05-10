#include "emerge-application.h"
#include "emerge-window.h"

struct _EmergeApplication
{
  AdwApplication parent_instance;
};

G_DEFINE_TYPE (EmergeApplication, emerge_application, ADW_TYPE_APPLICATION)

static void
emerge_application_finalize (GObject *object)
{
  EmergeApplication *self G_GNUC_UNUSED = EMERGE_APPLICATION (object);

  G_OBJECT_CLASS (emerge_application_parent_class)->finalize (object);
}

static void
emerge_application_activate (GApplication *app)
{
  GtkWindow *window;

  g_assert (EMERGE_IS_APPLICATION (app));

  window = gtk_application_get_active_window (GTK_APPLICATION (app));
  if (window == NULL)
    window = g_object_new (EMERGE_TYPE_WINDOW,
                           "application", app,
                           NULL);

  gtk_window_present (window);
}

static void
emerge_application_class_init (EmergeApplicationClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GApplicationClass *app_class = G_APPLICATION_CLASS (klass);

  object_class->finalize = emerge_application_finalize;

  app_class->activate = emerge_application_activate;
}

static void
emerge_application_init (EmergeApplication *self G_GNUC_UNUSED)
{
}

EmergeApplication *
emerge_application_new (const char        *application_id,
                          GApplicationFlags  flags)
{
  return g_object_new (EMERGE_TYPE_APPLICATION,
                       "application-id", application_id,
                       "flags", flags,
                       NULL);
} 