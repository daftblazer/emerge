#pragma once

#include <adwaita.h>

G_BEGIN_DECLS

#define EMERGE_TYPE_WINDOW (emerge_window_get_type())

G_DECLARE_FINAL_TYPE (EmergeWindow, emerge_window, EMERGE, WINDOW, AdwApplicationWindow)

G_END_DECLS 