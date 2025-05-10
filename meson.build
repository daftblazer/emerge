project('stable-gtk', 'c',
        version: '0.1.0',
        license: 'MIT',
        meson_version: '>= 0.59.0',
        default_options: [
          'warning_level=2',
          'c_std=gnu11',
        ]
)

# Set some global variables
prefix = get_option('prefix')
datadir = join_paths(prefix, get_option('datadir'))
localedir = join_paths(prefix, get_option('localedir'))

# Add compiler args
add_project_arguments([
  '-DGETTEXT_PACKAGE="stable-gtk"',
  '-DLOCALEDIR="' + localedir + '"',
], language: 'c')

gnome = import('gnome')
i18n = import('i18n')

subdir('src')

gnome.post_install(
  glib_compile_schemas: true,
  gtk_update_icon_cache: true,
  update_desktop_database: true,
) 