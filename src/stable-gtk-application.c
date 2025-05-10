#include "stable-gtk-application.h"
#include "stable-gtk-window.h"

struct _StableGtkApplication
{
  AdwApplication parent_instance;
};

G_DEFINE_TYPE (StableGtkApplication, stable_gtk_application, ADW_TYPE_APPLICATION)

static void
stable_gtk_application_finalize (GObject *object)
{
  StableGtkApplication *self = STABLE_GTK_APPLICATION (object);

  G_OBJECT_CLASS (stable_gtk_application_parent_class)->finalize (object);
}

static void
stable_gtk_application_activate (GApplication *app)
{
  GtkWindow *window;

  g_assert (STABLE_IS_GTK_APPLICATION (app));

  window = gtk_application_get_active_window (GTK_APPLICATION (app));
  if (window == NULL)
    window = g_object_new (STABLE_TYPE_GTK_WINDOW,
                           "application", app,
                           NULL);

  gtk_window_present (window);
}

static void
stable_gtk_application_class_init (StableGtkApplicationClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GApplicationClass *app_class = G_APPLICATION_CLASS (klass);

  object_class->finalize = stable_gtk_application_finalize;

  app_class->activate = stable_gtk_application_activate;
}

static void
stable_gtk_application_init (StableGtkApplication *self)
{
}

StableGtkApplication *
stable_gtk_application_new (const char        *application_id,
                          GApplicationFlags  flags)
{
  return g_object_new (STABLE_TYPE_GTK_APPLICATION,
                       "application-id", application_id,
                       "flags", flags,
                       NULL);
} 