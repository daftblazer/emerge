project('emerge', 'c',
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
  '-DGETTEXT_PACKAGE="emerge"',
  '-DLOCALEDIR="' + localedir + '"',
], language: 'c')

gnome = import('gnome')
i18n = import('i18n')

# Use local stable-diffusion.cpp libraries
sd_inc = include_directories('include')
sd_lib_dir = meson.current_source_dir() / 'lib'
sd_lib = meson.get_compiler('c').find_library('stable-diffusion', dirs: sd_lib_dir)
ggml_lib = meson.get_compiler('c').find_library('ggml', dirs: sd_lib_dir)
ggml_vulkan_lib = meson.get_compiler('c').find_library('ggml-vulkan', dirs: sd_lib_dir)

subdir('src')

gnome.post_install(
  glib_compile_schemas: true,
  gtk_update_icon_cache: true,
  update_desktop_database: true,
) 