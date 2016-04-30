# svbrdf-oculus
A Direct3D 11 program to render captured materials using Oculus Rift,
using materials from
[Two-Shot SVBRDF Capture for Stationary Materials](https://mediatech.aalto.fi/publications/graphics/TwoShotSVBRDF/) by Aittala et al (2015).

## Supported features

* Rendering with a procedural quadrilateral or a loaded `.OBJ` mesh with
  texture coordinates.
* Lighting using point lights.
* Simple displacement mapping using Direct3D 11 tessellation with
  adjustable tessellation density and displacement magnitude.
* Simple shadow mapping with PCF.
* Simple tangent space normal mapping.
* Antialiasing with 2x SSAA, 4x SSAA and 4x MSAA.
* Texture space lighting for antialiasing, when rendering with the procedural quadrilateral.
* Support for saving and loading preset scenes.

# How to get started

1. Clone the `svbrdf-oculus` repository.
2. Download the captured material set from [Two-Shot SVBRDF Capture for Stationary Materials Code and data](https://mediatech.aalto.fi/publications/graphics/TwoShotSVBRDF/code_and_data.html).
  * The material set is the large 13 GB 7-zip archive [Dataset: Output texture maps](https://mediatech.aalto.fi/publications/graphics/TwoShotSVBRDF/twoshot_data_results.7z).
3. Extract the captured material set under the `bin/data` directory,
   which is the default data directory.
4. Optionally, extract the heightmaps for the captured materials under
   the `bin/data` directory. This is required for displacement mapping.
   The link for downloading the heightmaps will be provided here once
   one is available.
5. Run `bin/SVBRDFOculus.exe`.
6. The default scene should load, and an on-screen help text should be
   displayed.

A reasonably powerful GPU is required for enabling some of the supported
effects when rendering with Oculus. `svbrdf-oculus` was developed and
tested using an NVIDIA GeForce GTX 970 and an Oculus Rift DK2.

The stand-alone `.exe` should run on any Windows version that supports
Direct3D 11. Windows 10 and Windows 8.1 have been tested. For rendering
with the Oculus Rift, the Oculus runtime is required to be installed.
The software should run without the Oculus runtime, in which case VR
rendering is disabled.

# Compiling

`svbrdf-oculus` has been developed using Windows 10, Visual Studio 2015
and Oculus SDK 0.8.0. It should also compile with few or no
modifications under Visual Studio 2013, although Visual Studio 2015 is
recommended.

For compiling, a Visual Studio 2015 solution file is provided under
`SVBRDFOculus/SVBRDFOculus.sln`. Additionally, the Oculus SDK is
required, and the `$OVRSDKROOT` environment variable should be set to
point to the Oculus SDK directory.

In order to run properly, the program needs to be pointed to a data
directory which it recursively scans for materials, height maps, and
meshes. This is done with the `--data <data-directory>` command line
switch. If the switch is omitted, `./data` is used as the default.

# License

All source code is fully open source for both noncommercial and
commercial usage, and is licensed under the MIT license.

The example meshes under the `bin/data` directory are copyright by their
respective authors. Each directory contains a more detailed license
file.
* `cube` is in Public Domain, and was created by Morgan McGuire. It was
  downloaded from [McGuire Graphics Data](http://graphics.cs.williams.edu/data/meshes.xml).
* `head` was created by Lee Perry-Smith and is licensed under a Creative
  Commons Attribution 3.0 Unported License. It was downloaded from [McGuire Graphics Data](http://graphics.cs.williams.edu/data/meshes.xml).
* `teapot` was created by Martin Newell. It was downloaded from [McGuire Graphics Data](http://graphics.cs.williams.edu/data/meshes.xml).
* `toaster` was created by Andrew Kensler. It was downloaded from [The Utah 3D Animation Repository](http://www.sci.utah.edu/~wald/animrep/).

