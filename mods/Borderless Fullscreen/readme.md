# Borderless Fullscreen

The switch for [LiD Borderless](https://github.com/NicoMullin/LiD-Borderless).
Ticked, the game is borderless fullscreen; unticked, it is back to normal.

## What it does

- **Ticked and saved:** the game starts in its own Windowed mode
  (`mbFullScreen=False` in `BrgGame\Config\BrgGraphicsConfig.ini`), and
  `lid-borderless.dll` takes that window's title bar and edges away. With the
  game's resolution set to your monitor's, it fills the screen: it looks like
  fullscreen, but Alt+Tab is instant and your other windows and monitors work
  as they do on the desktop.
- **Unticked and saved:** the setting goes back to what it was, and the plugin
  is switched off (renamed to `.dll.off`, which the Plugin Loader skips).
  Nothing is deleted; ticking the mod again brings it back.

Close the game before you save the mod list.

The game sets its window up again when you change the resolution or display
mode in its options. The plugin notices within a quarter of a second and
makes it borderless again. If you pick **Fullscreen** in the game's own
options while the mod is ticked, the game goes fullscreen for that session as
usual; the next Save Mod List sets it back to Windowed.

## Set the game's resolution to your monitor's

The window keeps the size the game draws at - the game puts its own size back
straight away if it is stretched, which would make it flash. So in the game's
options, pick your monitor's resolution (2560x1440 on a 1440p monitor, for
example) and it fills the screen. At a lower resolution the borderless window
sits in the middle of the monitor.

## You also need the plugin

`lid-borderless.dll` in `Binaries\Win64\lid-plugins`, with the
[LiD Plugin Loader](https://github.com/NicoMullin/LiD-Plugin-Loader). In LiD DB
Editor, **Get mods...** installs both.

Needs LiD DB Editor 0.12.8 or newer, which switches plugins with their mods.

## Undo

Untick the mod and save: the game is back to its own fullscreen and the
plugin is switched off.
