cmake_minimum_required(VERSION 3.10)
project(emerge VERSION 0.1.0)

# Include meson build
add_custom_target(build_meson ALL
    COMMAND meson setup build
    COMMAND cd build && ninja
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Install targets
install(DIRECTORY ${CMAKE_SOURCE_DIR}/build/src/ 
        DESTINATION bin
        FILES_MATCHING PATTERN "emerge"
        PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
                    GROUP_EXECUTE GROUP_READ
                    WORLD_EXECUTE WORLD_READ)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/lib/
        DESTINATION lib)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/data/
        DESTINATION share/emerge)

# CPack configuration
set(CPACK_PACKAGE_NAME "emerge")
set(CPACK_PACKAGE_VENDOR "Emerge")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A modern GTK4 Libadwaita user interface for Stable-Diffusion.cpp")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_CONTACT "your-email@example.com")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/yourusername/emerge")

# RPM specific
set(CPACK_RPM_PACKAGE_REQUIRES "gtk4, libadwaita")
set(CPACK_RPM_PACKAGE_GROUP "Applications/Graphics")
set(CPACK_RPM_PACKAGE_LICENSE "MIT")
set(CPACK_RPM_PACKAGE_URL ${CPACK_PACKAGE_HOMEPAGE_URL})
set(CPACK_RPM_PACKAGE_DESCRIPTION "Emerge is a modern GTK4 Libadwaita interface for Stable-Diffusion.cpp, allowing users to generate images using Stable Diffusion models locally on their computer.")

# Desktop integration
set(CPACK_RPM_PACKAGE_REQUIRES_POST "gtk-update-icon-cache, desktop-file-utils")
set(CPACK_RPM_POST_INSTALL_SCRIPT_FILE "${CMAKE_CURRENT_BINARY_DIR}/rpm-post.sh")
set(CPACK_RPM_POST_UNINSTALL_SCRIPT_FILE "${CMAKE_CURRENT_BINARY_DIR}/rpm-postun.sh")

# Create post-install script
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/rpm-post.sh"
"gtk-update-icon-cache -f -t /usr/share/icons/hicolor
update-desktop-database -q
")

# Create post-uninstall script
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/rpm-postun.sh"
"gtk-update-icon-cache -f -t /usr/share/icons/hicolor
update-desktop-database -q
")

# Enable only RPM generator
set(CPACK_GENERATOR "RPM")
include(CPack) 