emerge_sources = [
  'main.c',
  'emerge-window.c',
  'emerge-application.c',
]

# Compile resources
emerge_resources = gnome.compile_resources(
  'emerge-resources',
  'emerge.gresource.xml',
  c_name: 'emerge'
)

emerge_deps = [
  dependency('gtk4'),
  dependency('libadwaita-1'),
  declare_dependency(
    include_directories: sd_inc,
    dependencies: [sd_lib, ggml_lib, ggml_vulkan_lib]
  )
]

executable('emerge', 
  emerge_sources + emerge_resources,
  dependencies: emerge_deps,
  install: true,
)

# Install desktop file
install_data(
  'com.github.emerge.desktop',
  install_dir: join_paths(get_option('datadir'), 'applications')
) 