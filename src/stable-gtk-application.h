#pragma once

#include <adwaita.h>

G_BEGIN_DECLS

#define STABLE_TYPE_GTK_APPLICATION (stable_gtk_application_get_type())

G_DECLARE_FINAL_TYPE (StableGtkApplication, stable_gtk_application, STABLE, GTK_APPLICATION, AdwApplication)

StableGtkApplication *stable_gtk_application_new (const char *application_id,
                                                GApplicationFlags  flags);

G_END_DECLS 