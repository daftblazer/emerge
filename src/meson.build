stable_gtk_sources = [
  'main.c',
  'stable-gtk-window.c',
  'stable-gtk-application.c',
]

# Compile resources
stable_gtk_resources = gnome.compile_resources(
  'stable-gtk-resources',
  'stable-gtk.gresource.xml',
  c_name: 'stable_gtk'
)

stable_gtk_deps = [
  dependency('gtk4'),
  dependency('libadwaita-1'),
]

executable('stable-gtk', 
  stable_gtk_sources + stable_gtk_resources,
  dependencies: stable_gtk_deps,
  install: true,
)

# Install desktop file
install_data(
  'com.github.stable-gtk.desktop',
  install_dir: join_paths(get_option('datadir'), 'applications')
) 