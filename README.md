<h1 align="center">VoiceMeeter Chroma</h1>

<p align="center">
  <br>
  <em>A mod that adds theme support and quality-of-life features, such as window resizing, to Voicemeeter.</em>
  <br>
</p>

<p align="center">
   <img alt="GitHub Release" src="https://img.shields.io/github/v/release/emkaix/voicemeeter-chroma?style=flat&colorB=f38ba8">
   <img alt="Github Downloads" src="https://img.shields.io/github/downloads/emkaix/voicemeeter-chroma/total?style=flat&colorB=a6e3a1" />
   <img alt="GitHub License" src="https://img.shields.io/github/license/emkaix/voicemeeter-chroma?style=flat&colorB=89b4fa" />
</p>


<br>

![UI](https://raw.githubusercontent.com/emkaix/voicemeeter-theme-catppuccin-mocha/refs/heads/main/potato.png)

<br>

![UI](https://raw.githubusercontent.com/emkaix/voicemeeter-theme-catppuccin-mocha/refs/heads/main/banana.png)

<br>

> [!CAUTION]
> **This project is an unofficial, third-party, independent modification of Voicemeeter. It is not affiliated with, endorsed by, or in any way officially connected to VB-Audio Software or any of its subsidiaries or affiliates. All trademarks, service marks, and copyrights related to Voicemeeter are the property of VB-Audio Software. If you have any problems related to or caused by this mod, you will not get any support whatsoever on the Voicemeeter forums or the Voicemeeter Discord.**

<a name="support"></a>
## 🩹 Support

For any problems or bug reports, please create an issue here on Github or ask on the dedicated [Discord server](https://discord.gg/VSwsqXZaTD).

<a name="overview"></a>
## 🗺️ Overview

This mod replaces default UI elements and colors of Voicemeeter by hooking certain Windows API and Voicemeeter functions responsible for drawing on the application window surface.

It creates a copy of the Voicemeeter executable and patches its import table so that it loads the mod DLL on start-up, which performs the theme initialization.

[Supported themes](#-supported-themes) are placed in the `Documents/Voicemeeter/themes` directory and consist of bitmap images for the UI elements and a single `.yaml` file for color mappings.
Themes can be easily created, modified and adapted using regular image editing software.

### Files

#### vmchroma32.dll | vmchroma64.dll

Contains the core functionality of theme initialization by hooking theming related functions using the [Microsoft Detours library](https://github.com/microsoft/Detours).
It is copied to the `C:\Program Files (x86)\VB\Voicemeeter` directory and loaded by Voicemeeter on every start-up.

#### addimport32.exe | addimport64.exe

Patches the import table of the Voicemeeter executable by adding an entry for `vmchroma32.dll` / `vmchroma64.dll`. This is only done once, when you run the patching script. Voicemeeter will from then on load the DLL when it starts.

#### vmchroma_patcher.ps1

Runs `addimport32.exe` and `addimport64.exe` to patch the 32bit and 64bit versions of Voicemeeter.
It creates new executables with `_vmchroma` suffix and only patches these copies.
It then copies `vmchroma32.dll` and `vmchroma64.dll` into the Voicemeeter installation directory.

<a name="status"></a>
## 🚦 Status

Tested on Windows 10 & Windows 11 for the following Voicemeeter versions:

- ✅ `Default 1.1.1.9`
- ✅ `Banana 2.1.1.9`
- ✅ `Potato 3.1.1.9`

(Older versions may work as well).

<a name="getting-started"></a>
## 🚀 Getting Started

(For more detailed instructions with screenshots, check out the [wiki](https://github.com/emkaix/voicemeeter-chroma/wiki/Getting-Started))

1. Download the mod from the [Release page](https://github.com/emkaix/voicemeeter-themes-mod/releases) and extract the .zip folder.
2. Open the extracted folder and `Shift + right-click` somewhere inside the folder.
3. Select `Open PowerShell window here`.
4. Run the following command to unblock and execute the script:

```pwsh
powershell -ExecutionPolicy Bypass -File .\vmchroma_patcher.ps1
```

5. Press enter to run the script. You should see `Voicemeeter patching complete!` at the end.
6. Choose a [supported theme](#-supported-themes), copy it to `C:\Users\<USER>\Documents\Voicemeeter\themes\` and set the name in the config file as described in the theme readme.
7. Change other settings in the config file to your liking.
8. Start Voicemeeter VMChroma using the newly created shortcuts in the windows start menu (press Windows key and search for `VMChroma`) 

<a name="supported-themes"></a>
## 🎨 Supported Themes

[Catppuccin Mocha (Banana & Potato)](https://github.com/emkaix/voicemeeter-theme-catppuccin-mocha)

[Catppuccin Macchiato (currently only Banana)](https://github.com/emkaix/voicemeeter-theme-catppuccin-macchiato)

<a name="build-from-source"></a>
## 🛠️ Build From Source

Install the following toolchain if you don't have it installed already (older versions of the toolchain may work as well).

1. Download and run [Build Tools for Visual Studio 2022](https://visualstudio.microsoft.com/downloads/?q=build+tools#build-tools-for-visual-studio-2022).
2. Under "Individual components" search for and select the following:
    - ☑️ `MSVC v143 - VS 2022 C++ x64/x86 build tools (Latest)`
    - ☑️ `Windows 11 SDK (10.0.22621.0)`
    - ☑️ `C++ CMake tools for Windows`
3. Make sure only these 3 options are selected and then click install.
4. [Download](https://github.com/emkaix/voicemeeter-themes-mod/archive/refs/heads/master.zip) and unzip or clone the repository.
5. In the start menu, search and run `Developer PowerShell for VS 2022` and navigate to the downloaded folder using `cd`.
6. Run the following command:
      ```pwsh
      cmake -S . -B build32 -G "Visual Studio 17 2022" -A Win32; `
      cmake -S . -B build64 -G "Visual Studio 17 2022" -A x64; `
      cmake --build build32 --config Release; `
      cmake --build build64 --config Release
      ```
8. The build artifacts are now located in the `out` folder.

<a name="faq"></a>
## 🤔 Frequently Asked Questions

### I have problems using this, can I go to the Voicemeeter Discord / forum and ask for help there?

No, this is a completely unofficial mod that is not affiliated with Voicemeeter or VB-Audio Software in any shape or form, the moderators won't be able to help you, see [Support](#-support).

### Can I use this mod without a theme?

Yes, you can use this mod without a theme. For example, you might want to keep the default UI but still be able to resize the window or change the mouse scroll behaviour. Simply leave the `theme` setting in the config file blank for all three Voicemeeter versions.

### How does it work?

It patches the Voicemeeter executable so that it loads the mod DLL on start-up. The mod then hooks (= intercepts) some Windows API functions responsible for drawing on the application surface. This includes: [CreatePen](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-createpen), [CreateBrushIndirect](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-createbrushindirect) and [SetTextColor](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-settextcolor), among others.

To make the window resizable, VMChroma modifies the rendering logic by tricking the application to use a Direct2D compatible, fixed-size memory device context, instead of rendering to the window directly. The content of the memory device context is then scaled and rendered to the actual window using Direct2D with high quality cubic interpolation. The WndProc function is hooked
to intercept messages containing mouse coordinates, which are mapped to the original window size, so that Voicemeeter can process them normally.

Hooking is done using the Microsoft Detours library: https://github.com/microsoft/Detours/wiki

### Does it modify the Voicemeeter executable on disk?

It doesn't modify or delete the original executable. It creates a copy with `_vmchroma` appended to its name that loads the mod when executed.

### I have Voicemeeter configured to run on Windows startup, how can I autostart Voicemeeter with the theme applied?

#### Option 1

Create a backup of the original executable and rename the mod executable by removing the `_vmchroma` suffix.

#### Option 2

1. Untick `Run on Windows Startup` in the Voicemeeter menu
2. Create a shortcut to the mod executable in `C:\Users\<USER>\AppData\Roaming\Microsoft\Windows\Start Menu\Programs\Startup`

### Does this mod give me a themed Voicemeeter on it's own?

No, this mod only enables theme support. You also need to download a supported theme separately, see [Supported themes](#-supported-themes).

### How do I make my own theme?

Get a [supported theme](#-supported-themes) and edit the bitmaps with image editing software. You'll also need to adapt the color mapping in the `colors.yaml` file to match the background bitmaps.
If you want the original background images embedded in the Voicemeeter executable, you need to extract them yourself, as described in [this guide I wrote on the official Voicemeeter Discord](https://discord.com/channels/755690270795890739/1369370435187380304).

<a name="dependencies"></a>
## 🔗 Dependencies

- Microsoft Detours [https://github.com/microsoft/Detours](https://github.com/microsoft/Detours)
    - For hooking Windows API and custom functions.
- spdlog [https://github.com/gabime/spdlog](https://github.com/gabime/spdlog)
    - For log file functionality.
- yaml-cpp [https://github.com/jbeder/yaml-cpp](https://github.com/jbeder/yaml-cpp)
    - for parsing configuration files.
- capstone [https://github.com/capstone-engine/capstone](https://github.com/capstone-engine/capstone)
    - to find some instructions needed for patching 

The dependencies are pulled and built from source in the build step via the CMakeLists.txt file.
