# Stable GTK

A modern GTK4 Libadwaita user interface for [Stable-Diffusion.cpp](https://github.com/leejet/stable-diffusion.cpp).

## Features

- Text-to-Image (txt2img) generation
- Image-to-Image (img2img) generation
- Control over all important Stable Diffusion generation parameters:
  - Prompt and negative prompt
  - Width and height
  - Steps
  - CFG Scale
  - Sampling method
  - Seed
- Support for different model formats (ckpt, safetensors, gguf)
- Clean, modern Libadwaita interface

## Requirements

- GTK 4
- Libadwaita
- Stable-Diffusion.cpp (compiled and in your PATH)

## Building

```bash
meson setup build
cd build
ninja
```

## Running

```bash
./build/src/stable-gtk
```

## Installation

```bash
ninja install
```

## License

MIT License 