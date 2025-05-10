#pragma once

#include <adwaita.h>

G_BEGIN_DECLS

#define EMERGE_TYPE_APPLICATION (emerge_application_get_type())

G_DECLARE_FINAL_TYPE (EmergeApplication, emerge_application, EMERGE, APPLICATION, AdwApplication)

EmergeApplication *emerge_application_new (const char *application_id,
                                                GApplicationFlags  flags);

G_END_DECLS 