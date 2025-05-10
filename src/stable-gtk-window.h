#pragma once

#include <adwaita.h>

G_BEGIN_DECLS

#define STABLE_TYPE_GTK_WINDOW (stable_gtk_window_get_type())

G_DECLARE_FINAL_TYPE (StableGtkWindow, stable_gtk_window, STABLE, GTK_WINDOW, AdwApplicationWindow)

G_END_DECLS 