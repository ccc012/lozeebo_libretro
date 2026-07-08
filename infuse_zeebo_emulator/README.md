# Infuse

Infuse is a BREW subsystem reimplementation and Zeebo high-level emulator written from scratch, based purely on clean reverse engineering attempts. Currently it is using dynarmic ARM JIT core and runs three Zeebo commercial games in fully playable state which is Double Dragon, Crash Nitro Kart 3D and Zeebo Family Pack as well as some of the BREW samples.

## Version

Infuse A1 (development preview) released on 18th May 2024.

## Author

Written by Przemyslaw 'Tuxality' Skryjomski.

## Contact

Webpage: <http://tuxality.net/projects/infuse_zeebo_emulator>

YouTube: <https://www.youtube.com/Tuxality>

Twitter: <https://twitter.com/Tuxality>

# Please read before use

Please check LICENSE file inside archive before usage as well as read short version available below.

> Copyright (C) 2018, 2023-2024 Przemyslaw 'Tuxality' Skryjomski
>
> This software is provided 'as-is', without any express or implied warranty. In no event will the author be held liable for any damages arising from the use of this software.
>
> Permission is granted to anyone to use this software for individual purpose and is subject to the following restrictions:
>
> 1.  The origin of this software must not be misrepresented, you must not claim that you wrote the original software nor suggest that you were part of such.
> 2.  Altered versions of source code must be plainly marked as such, must not be misrepresented as being the original software.
> 3.  Altered versions of binary distribution and redistributing of such is not allowed in any way.
> 4.  This notice cannot be removed or altered in any way from any source as well as binary distribution.
> 5.  This software cannot be redistributed in form of unofficial bundle or package as well as it cannot be publicly hosted by the third parties, its origin must not be misrepresented.
> 6.  This software is intended to be used by the end-user, it cannot be sold nor be part of any commercial product.
> 7.  Above restrictions are subject to change in the future by the original project owner only, which is Tuxality.

For more detailed information about licensing please check LICENSE file which also includes information about components used in this project.

If you do not agree with the license conditions, **please do not use this software nor redistribute it in any way**.

There is no support available in case of any issues nor any responsibility taken by the developer when software is misused by the end user so please use this software at your own risk.

Abusing or not complying with above license terms **will** harm further Infuse development and Zeebo emulation progress. Thank you for understanding.

# Installation

Infuse can be installed depending on the users will and selected port, there is no setup program nor automated installation of such.

### macOS (arm64)

Extract archive and place _Infuse.app_ application directly into user _Applications_ directory or execute from preferred directory.

When upgrading Infuse to newer version, replace _Infuse.app_ with a newer one.

User content will remain in user configuration directory as explained in later sections.

### GNU/Linux (x86_64)

Extract archive and place Infuse in preferred directory.

When upgrading Infuse to newer version, replace completely Infuse directory with a newer one.

User content will remain in user configuration directory as explained in later sections.

### Steam Deck (x86_64)

Use desktop mode or enable SSH access in order to extract archive and place Infuse in preferred directory.

Then create shortcut for non-Steam application inside Steam Deck UI pointing directly to the _Infuse_ binary with Infuse directory being set as working path.

When upgrading Infuse to newer version, replace completely Infuse directory with a newer one.

User content will remain in user configuration directory as explained in later sections.

### Haiku OS (x86_64)

Extract archive and place Infuse in preferred directory.

When upgrading Infuse to newer version, replace completely Infuse directory with a newer one.

User content will remain in user configuration directory as explained in later sections.

### Windows (x86_64)

Extract archive and place Infuse in preferred directory.

When upgrading Infuse to newer version, replace completely Infuse directory with a newer one.

User content will remain in user configuration directory as explained in later sections.

### RG353V stock native (armv7)

Extract archive into _ports_ directory depending on currently used memory card scheme.

System card only (TF1):

> /roms/ports/

System and game card (TF1 + TF2):

> /roms2/ports/

Reboot _Emulation Station_ in order for Infuse to pop-up in _Ports_ section.

When upgrading Infuse to newer version, replace completely Infuse directory with a newer one.

User content will remain in user configuration directory as explained in later sections.

### R35s / R36s ArkOS native (armv7)

Extract archive into _ports_ directory depending on currently used memory card scheme.

System card only (TF1):

> /roms/ports/

System and game card (TF1 + TF2):

> /roms2/ports/

Reboot _Emulation Station_ in order for Infuse to pop-up in _Ports_ section.

When upgrading Infuse to newer version, replace completely Infuse directory with a newer one.

User content will remain in user configuration directory as explained in later sections.

### Raspberry PI native (armv7)

Extract archive and place Infuse in preferred directory.

When upgrading Infuse to newer version, replace completely Infuse directory with a newer one.

User content will remain in user configuration directory as explained in later sections.

# First usage

Executing Infuse for the first time will end up with _BREW applets not found_ message.

During such first boot Infuse will create its own directory structure in user configuration directory to which Zeebo / BREW software needs to be copied over.

Infuse user configuration path depends on the selected port as shown below.

No matter which port is used, path will be referred to as _Infuse user configuration directory_ in the remaining sections of the README file.

### macOS (arm64)

> ~/Library/Application Support/Tuxality/Infuse

### GNU/Linux (x86_64)

> ~/.Tuxality/Infuse

### Steam Deck (x86_64)

> ~/.Tuxality/Infuse

### Haiku OS (x86_64)

> ~/.Tuxality/Infuse

### Windows (x86_64)

> %LOCALAPPDATA%\\Tuxality\\Infuse

### RG353V stock native (armv7)

System card only (TF1):

> /roms/Tuxality/Infuse

System and game card (TF1 + TF2):

> /roms2/Tuxality/Infuse

### R35s /R36s ArkOS native (armv7)

System card only (TF1):

> /roms/Tuxality/Infuse

System and game card (TF1 + TF2):

> /roms2/Tuxality/Infuse

### Raspberry PI native (armv7)

> ~/.Tuxality/Infuse

# Zeebo / BREW software installation

Zeebo and BREW software such as games can be installed by copying over MIF file into the _mif_ directory as well as copying over directory with BREW module content into the _mod_ directory. Both directories are part of _brew_ subdirectory which can be found inside _Infuse user configuration directory_ that is created during first boot.

In order for BREW applet to be found by Infuse a signature file needs to be provided with the same name as module file, however with _sig_ extension instead of _mod_.

Signature file can be an empty file as its existence is only important not its content, there is no signature validation check implemented in Infuse.

### Example of proper Infuse user configuration directory structure

Below you can find an example directory structure with Crash Nitro Kart 3D (274214), Double Dragon (274754), and Zeebo Family Pack (277229) installed in Infuse.

Please make sure that the same structure is used as well as _sig_ file provided for each module as shown below.

```
.
├── Infuse.config
└── brew
    ├── mif
    │   ├── 274214.mif
    │   ├── 274754.mif
    │   └── 277229.mif
    └── mod
        ├── 274214
        │   ├── cnk2.mod
        │   ├── cnk2.sig
        │   ├── data.vfs
        │   └── udata
        │       ├── saves
        │       └── settings.dat
        ├── 274754
        │   ├── data.ggz
        │   ├── ddragonz.mod
        │   ├── ddragonz.sig
        │   ├── sound.ggz
        │   └── udata
        │       └── ddz.sav
        └── 277229
            ├── data.vfs
            ├── game.mod
            ├── game.sig
            └── udata
                └── highscore.dat
```

Some directories such as _saves_ and _udata_ are created automatically by Zeebo software and/or Infuse.

# Save games

In case of Zeebo software, save games are stored inside _udata_ directory.

Infuse automatically creates _udata_ directory if one does not exist during startup in order to allow game to store user saves.

Saved game progress can be backed up by copying _udata_ directory of each game.

# Input mapping

Infuse supports both, keyboard and gamepad input depending on currently executed applet.

In case of BREW applets being executed, only keyboard input is supported in Infuse emulated version.

However, Zeebo software is directly controlled by connected gamepad with possibility for connecting for up to two controllers.

Additional alternative keyboard to gamepad input mapping is available if no controller is connected.

Infuse supports connecting and disconnecting gamepads during runtime, however such functionality may be limited on some systems.

## BREW

When running BREW applets, following keyboard input scheme is used:

```
AVK_CLR    - Backspace
AVK_SELECT - Enter
AVK_LEFT   - Left
AVK_RIGHT  - Right
AVK_UP     - Up
AVK_DOWN   - Down
AVK_0      - 0
AVK_1      - 1
AVK_2      - 2
AVK_3      - 3
AVK_4      - 4
AVK_5      - 5
AVK_6      - 6
AVK_7      - 7
AVK_8      - 8
AVK_9      - 9
AVK_STAR   - Z
AVK_POUND  - X
AVK_SOFT1  - C
AVK_SOFT2  - V
AVK_END    - B
AVK_SEND   - N
```

There is _currently_ no gamepad input mapping due to the insufficient number of buttons available on such.

Gamepad support for BREW applets will be added in the future Infuse versions when BREW software support will be more mature.

## Zeebo

When running Zeebo games, following gamepad input scheme is used with _XInput_ compatible controller used as a reference:

```
Button 1      - A
Button 2      - X
Button 3      - Y
Button 4      - B
Home          - Start
AVK_CLR       - Back
Up            - Up
Down          - Down
Left          - Left
Right         - Right
Left trigger  - LT
Right trigger - RT
Left analog   - Left analog
Right analog  - Right analog
```

In case of _Steam Deck_ version same scheme as above is used with possibility of using touchpad as a directional pad.

There is a possibility to use alternative keyboard input that is enabled by default with such input scheme being used:

```
Button 1                  - K
Button 2                  - J
Button 3                  - I
Button 4                  - L
Home                      - E
AVK_CLR                   - Backspace
Up                        - W
Down                      - S
Left                      - A
Right                     - D
Left trigger              - U
Right trigger             - O
Left analog               - Not available, please use directional pad mapping
Right analog X axis left  - Numeric keypad 4
Right analog X axis right - Numeric keypad 6
Right analog Y axis up    - Numeric keypad 8
Right analog Y axis down  - Numeric keypad 5
```

Availability of alternative keyboard input depends on Infuse input backend being used by selected port.

# Input mapping on native port

Input mapping in native port differs due to the form-factor limitations of such handheld devices.

## R35s / R36s and RG353V

Currently following gamepad input scheme is used in native port for both, ArkOS and Anbernic stock operating system:

```
Button 1      - B
Button 2      - Y
Button 3      - X
Button 4      - A
Home          - Start
AVK_CLR       - Select
Up            - Up
Down          - Down
Left          - Left
Right         - Right
Left trigger  - L1
Right trigger - R1
Left analog   - Left analog
Right analog  - Right analog
```

Please note that _AVK_CLR_ is used for sending event request related to closing currently executed BREW applet, which means that even in case of Zeebo software being executed a sudden exit back to the Infuse menu will be performed during gameplay.

Additionally, keyboard is mapped to the same gamepad input when BREW applet is executed:

```
AVK_CLR    - Select
AVK_SELECT - Start
AVK_LEFT   - Left
AVK_RIGHT  - Right
AVK_UP     - Up
AVK_DOWN   - Down
AVK_0      - L1
AVK_1      - L2
AVK_2      - R2
AVK_3      - R1
AVK_4      - Y
AVK_5      - X
AVK_6      - A
AVK_7      - B
```

However, due to the nature of such devices, remaining BREW input buttons are not available thus such functionality is currently quite limited.

BREW key to gamepad input mapping will be improved in the future Infuse versions.

## Raspberry PI

Input mapping remains the same for native Raspberry PI port as on emulated platforms.

# Configuration with use of Infuse menu

Infuse configuration is stored in _Infuse.config_ file in Infuse user configuration directory.

It is not recommended to manually change values in configuration file, most of the functionality can be configured with use of _Settings_ activity of the Infuse menu.

### BREW system language

Allows setting device language reported by BREW implementation to currently executed applet.

Available values: _English, Polish, Spanish, Spanish (M.), Japanese, Chinese, Chinese (S.) and Chinese (T.)_

Default value: _English_

### Fullscreen rendering mode

When enabled enters fullscreen mode with mouse cursor disabled, if disabled switches back to the windowed mode with mouse cursor being shown.

Available values: _Disabled, Enabled_

Default value: _Disabled_

Settings entry is not available on Steam Deck and R35s / R36s / RG353v devices as fullscreen mode is enabled by default.

### Restore last window size during startup

When enabled restores last known window size during next Infuse startup.

Available values: _Disabled, Enabled_

Default value: _Disabled_

Settings entry is not available on Steam Deck and R35s / R36s / RG353v devices as fullscreen mode is enabled by default.

### Keep aspect ratio while resizing window

When enabled keeps Zeebo native aspect ratio while resizing window.

Available values: _Disabled, Enabled_

Default value: _Disabled_

Settings entry is not working on Haiku OS port due to the system limitations.

Settings entry is not available on Steam Deck and R35s / R36s / RG353v devices as fullscreen mode is enabled by default.

### Ignore aspect ratio

When enabled ignores Zeebo native aspect ratio stretching output to the host screen aspect ratio, if disabled provides top/bottom or left/right black borders depending on current window size or fullscreen resolution.

Available values: _Disabled, Enabled_

Default value: _Disabled_

Settings entry is not available on R35s / R36s / RG353v devices due to the screen aspect ratio matching Zeebo video output and no scaling being performed.

### Force nearest filtering on output scaling

When enabled forces nearest neighbour filtering during output scaling. This allows for sharper image especially in case of 2D based games, however 3D games and UI elements will suffer in terms of graphics fidelity.

Available values: _Disabled, Enabled_

Default value: _Disabled_

Settings entry is not available on R35s / R36s / RG353v devices due to the screen aspect ratio matching Zeebo video output and no scaling being performed.

### Force bilinear filtering on textures

When enabled forces bilinear filtering on *all* textures. Not recommended when games were written strictly with nearest neighbor filtered textures in mind, enabling such will produce graphical glitches due to the incorrect UV texture mapping.

Available values: _Disabled, Enabled_

Default value: _Disabled_

### Invert first controller right stick Y axis

When enabled inverts right stick Y axis for the first controller.

Available values: _Disabled, Enabled_

Default value: _Disabled_

### Invert second controller right stick Y axis

When enabled inverts right stick Y axis for the second controller.

Available values: _Disabled, Enabled_

Default value: _Disabled_

### Ignore first controller alternative input

When enabled first controller alternative input allowing to control user interface elements and Zeebo software with use of a keyboard instead of controller is ignored.

Available values: _Disabled, Enabled_

Default value: _Disabled_

Settings entry is not available on Steam Deck and R35s / R36s / RG353v devices as no keyboard input is available in such ports.

### Skip first controller during enumeration

When enabled skips first controller during enumeration i.e. when controller connect / disconnect event occurs as well as during input configuration being changed. Useful for discarding dummy controller that may exist on some systems.

Available values: _Disabled, Enabled_

Default value: _Disabled_

Settings entry is not available on Steam Deck and R35s / R36s / RG353v devices in order to avoid possible issues of losing controller input.

### Switch first controller with second one

When enabled switches first controller with second one allowing to change controller order in an easy way without the need for reconnecting.

Available values: _Disabled, Enabled_

Default value: _Disabled_

Settings entry is not available on Steam Deck and R35s / R36s / RG353v devices in order to avoid possible issues of losing controller input.

### Ignore audio events

When enabled ignores *all* audio events created by BREW and Zeebo software which results in no sound being generated as well as no processing of such being done during BREW applet execution. Please be aware that disabling such events changes behavior of BREW applets which in the end may result in undefined behavior.

Available values: _Disabled, Enabled_

Default value: _Disabled_

### MIDI soundfont

Selects soundfont that will be used for MIDI music rendering during BREW and Zeebo software execution. Infuse automatically searches for soundfonts inside _soundfont_ directory which are then added to the settings entry as available options.

_GeneralUser GS v1.471.sf2_ is the default soundfont which will be used as a fallback in case of incorrect soundfont being provided. Default soundfont name will be abbreviated in the settings menu as _GeneralUser_.

Additionally in the package _GMGSx.sf2_ public domain soundfont is provided as an alternative available as _GMGSx_ in the settings menu.

Available values: _GeneralUser_, _GMGSx_

Optional values: _stem of the .sf2 filenames found inside brew/soundfont directory_

Default value: _GeneralUser_

### Audio volume

Sets audio volume that Infuse outputs affecting both, emulator user interface and BREW / Zeebo software.

Available values: _0 - 100 with 5 increments_

Default value: _100_

### Play menu music

When enabled plays background music during Infuse menu, if disabled no background music is played.

Available values: _Disabled, Enabled_

Default value: _Enabled_

### Menu color theme

Sets Infuse menu color theme.

Available values: _Blue, Purple, Solid black and Solid blue_

Default value: _Purple_

# Configuration tags

User configuration is saved by Infuse to the _Infuse.config_ file within user configuration directory with use of configuration tags.

Possible configuration tags are listed below with explanation and information regarding possible values that can be set.

Some of the configuration tag values are automatically updated and will be overwritten when starting and/or closing Infuse.

If inproper value is set a default value will be restored for each configuration tag besides ones pointing to specific paths.

If inproper configuration tag is set or one is unvailable in currently used Infuse port such entry will be removed from the config file.

### infuse.audio.midi.soundfont

Please refer to _MIDI soundfont_ menu settings entry.

Specifies name of the soundfont being used for MIDI rendering.

Default value: _GeneralUser GS v1.471.sf2_

Setting incorrect value or providing not supported soundfont will end up with Infuse defaulting to the _GeneralUser_ soundfont.

### infuse.renderer.font

Specifies name of the true type font being used for font rendering.

Default value: _Noto Sans JP 400.otf_

Setting incorrect value or providing not supported soundfont may end up with Infuse crashing during startup and/or undefined behavior during gameplay.

### infuse.renderer.last_window_width

Stores last width of window, used for restoring window size when _infuse.renderer.restore_window_size_ is set to Enabled.

If incorrect value is provided configuration tag is reverted to 640 value.

Always overwritten by Infuse.

### infuse.renderer.last_window_height

Stores last height of window, used for restoring window size when _infuse.renderer.restore_window_size_ is set to Enabled.

If incorrect value is provided configuration tag is reverted to 480 value.

Always overwritten by Infuse.

### brew.language

Please refer to _BREW system language_ menu settings entry.

### infuse.renderer.fullscreen

Please refer to _Fullscreen rendering mode_ menu settings entry.

### infuse.renderer.restore_window_size

Please refer to _Restore last window size during startup_ menu settings entry.

### infuse.renderer.resize_keep_aspect_ratio

Please refer to _Keep aspect ratio while resizing window_ menu settings entry.

### infuse.renderer.ignore_aspect_ratio

Please refer to _Ignore aspect ratio_ menu settings entry.

### infuse.renderer.force_nearest_output_scaling

Please refer to _Force nearest filtering on output scaling_ menu settings entry.

### infuse.renderer.force_bilinear

Please refer to _Force bilinear filtering on textures_ menu settings entry.

### infuse.input.invert_first_right_y_axis

Please refer to _Invert first controller right stick Y axis_ menu settings entry.

### infuse.input.invert_second_right_y_axis

Please refer to _Invert second controller right stick Y axis_ menu settings entry.

### infuse.input.first_ignore_alternative_input

Please refer to _Ignore first controller alternative input_ menu settings entry.

### infuse.input.skip_first_controller

Please refer to _Skip first controller during enumeration_ menu settings entry.

### infuse.input.switch_controllers

Please refer to _Switch first controller with second one_ menu settings entry.

### infuse.audio.ignore_events

Please refer to _Ignore audio events_ menu settings entry.

### infuse.audio.volume

Please refer to _Audio volume_ menu settings entry.

### infuse.audio.bgm

Please refer to _Play menu music_ menu settings entry.

### infuse.menu.color.theme

Please refer to _Menu color theme_ menu settings entry.

### infuse.menu.framerate_cap

Defines framerate cap for Infuse user interface, does not affect executed BREW applets or Zeebo software.

Default value: _240_

Setting to 0 will remove framerate cap which will end up with Infuse consuming all available resources during user interface rendering.

In case of vertical sync being enabled framerate will be capped to such even when 0 value is set depending on the platform Infuse is executed on.

# Zeebo software compatibility

Compatibility list of Zeebo software can be found below.

## Fully working

Currently fully compatible commercial software:

- Crash Bandicoot Nitro Kart 3D
- Double Dragon
- Zeebo Family Pack

## Partially working

Currently tested and partially working commercial software:

- Raging Thunder II
- Reckless Racing

Please note that partially working software may work in debug builds only, it is not guaranteed that software will run and/or be stable in release builds.

## Not working

Currently tested and not working commercial software:

- Action Hero 3D: Wild Dog
- Quake
- Quake II
- Rally Master Pro
- Zuma's Revenge

Remaining software is untested and should be considered as currently not working as well as not supported in any way.

# BREW software compatibility

Compatibility list of BREW software can be found below.

## Fully working

Currently fully compatible homebrew software:

- OpenGL ES Demo 02
- OpenGL ES Demo 03

## Partially working

Currently tested and partially working homebrew software:

- OpenGL ES Demo 01

## Not working

Currently tested and not working commercial software:

- Devil May Cry: Dante X Vergil
- Kingdom Hearts: V Cast

Remaining software is untested and should be considered as currently not working as well as not supported in any way.

# Limitations of Infuse ports

Implementation limitations of each Infuse emulated port are listed below.

## macOS (arm64)

No limitations known.

## GNU/Linux (x86_64)

Sound backend requires PulseAudio to be installed and properly configured. Please refer to your GNU/Linux distribution documentation for further information.

Infuse may in _very rare_ instances crash during startup or shutdown due to the PulseAudio library, please try to execute Infuse again in case of startup issue.

## Steam Deck (x86_64)

No limitations known.

## Haiku OS (x86_64)

Port is considered highly experimental due to the current state of Haiku OS itself.

Controller support is *very* limited due to the system restrictions.

While Device Kit backend was written with generic controller support in mind, only wired Xbox 360 controller was fully tested and confirmed working.

Disconnecting controllers during gameplay may end up in Haiku Kernel Debugging Land. :)

Controllers are enumerated during startup as well as when changing controller related options in the settings during runtime only.

Simultaneous use of two controllers may not be supported due to the Haiku OS internal issue, this issue may be fixed on the Haiku end in the future.

Please use alternative keyboard input in case of having issues with the controller support.

Resizing window with aspect ratio kept is not available in this port due to the system restrictions.

## Windows (x86_64)

No limitations known.

# Limitations of native ports

Native implementation limitations of each Infuse port are listed below.

Please note that the native version is in **very early experimental** stage and is not on par with emulated version in terms of performance nor considered stable or supported in any way.

## RG353V stock (armv7)

Port is considered higly experimental and unstable, please consider it as a **proof-of-concept** only.

Very low performance may be observed in some software due to the both, hardware and current port limitations.

Memory fragmentation may occur when transitioning in game menus multiple times such as changing game in Zeebo Family Pack resulting in software being executed slower each time.

## R35s /R36s ArkOS (armv7)

Port is considered higly experimental and unstable, please consider it as a **proof-of-concept** only.

Extremely low performance may be observed in some software due to the both, hardware and current port limitations.

Memory fragmentation may occur when transitioning in game menus multiple times such as changing game in Zeebo Family Pack resulting in software being executed slower each time.

## Raspberry PI (armv7)

Port is considered higly experimental and unstable, please consider it as a **proof-of-concept** only.

Very low performance may be observed in some software due to the both, hardware and current port limitations.

Memory fragmentation may occur when transitioning in game menus multiple times such as changing game in Zeebo Family Pack resulting in software being executed slower each time.

Despite being native port functionality as well as general behavior is to be expected the same as in case of the generic GNU/Linux port.

# Special thanks

I would like to thank following people:

- **KogarashiDS** for all time spent on testing, ideas and support
- **Merryhime** for dynarmic ARM JIT which was a real pleasure to integrate and work with

# Information

For more information please check project website: <http://tuxality.net/projects/infuse_zeebo_emulator>

Thank you for using Infuse, I hope you will have great time enjoying Zeebo games!

- Tuxality
