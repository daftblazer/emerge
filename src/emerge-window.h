#pragma once

#include <adwaita.h>

G_BEGIN_DECLS

#define EMERGE_TYPE_WINDOW (emerge_window_get_type())

G_DECLARE_FINAL_TYPE (EmergeWindow, emerge_window, EMERGE, WINDOW, AdwApplicationWindow)

// Config structure to store persistent settings
typedef struct {
  gchar *models_directory;
  gchar *last_model_path;
  gchar *last_save_directory;
  gchar *last_template_directory;
} EmergeConfig;

void emerge_window_save_config(EmergeWindow *self);
void emerge_window_load_config(EmergeWindow *self);

// Add template functions declaration
void save_template_to_file(EmergeWindow *self, const char *filepath);
void load_template_from_file(EmergeWindow *self, const char *filepath);

G_END_DECLS 