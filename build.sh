#!/bin/bash
set -e  # Exit immediately if a command exits with a non-zero status

echo "Setting up meson build directory..."
if [ ! -d "build" ]; then
  meson setup build
else
  echo "Build directory already exists, reconfiguring..."
  meson setup --reconfigure build
fi

echo "Changing to build directory..."
cd build

echo "Running ninja to build the project..."
ninja

echo "Build completed successfully!" 