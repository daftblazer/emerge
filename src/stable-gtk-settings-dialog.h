#pragma once

#include <adwaita.h>

G_BEGIN_DECLS

#define STABLE_GTK_TYPE_SETTINGS_DIALOG (stable_gtk_settings_dialog_get_type())

G_DECLARE_FINAL_TYPE (StableGtkSettingsDialog, stable_gtk_settings_dialog, STABLE_GTK, SETTINGS_DIALOG, AdwPreferencesDialog)

StableGtkSettingsDialog *stable_gtk_settings_dialog_new (GtkWindow *parent);

G_END_DECLS 