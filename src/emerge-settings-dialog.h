#pragma once

#include <adwaita.h>

G_BEGIN_DECLS

#define EMERGE_TYPE_SETTINGS_DIALOG (emerge_settings_dialog_get_type())

G_DECLARE_FINAL_TYPE (EmergeSettingsDialog, emerge_settings_dialog, EMERGE, SETTINGS_DIALOG, AdwPreferencesDialog)

EmergeSettingsDialog *emerge_settings_dialog_new (GtkWindow *parent);

G_END_DECLS 