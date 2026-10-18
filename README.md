# LiD Borderless

Borderless fullscreen for the offline version of Let It Die (Steam): the game
fills your monitor like fullscreen, but Alt+Tab is instant and other windows
and monitors work as they do on the desktop.

- **lid-borderless.dll** (`dll/`): a plugin for the
  [LiD Plugin Loader](https://github.com/NicoMullin/LiD-Plugin-Loader).
- **Borderless Fullscreen** (`mods/`): its switch in
  [LET IT DIE DB Mod Manager](https://github.com/NicoMullin/LiD-DB-Editor).
  Ticked, the game is borderless; unticked, it is back to normal.

## How it works

The game has a Fullscreen mode and a Windowed mode, but no borderless one. The
mod sets the game to Windowed (`mbFullScreen=False` in
`BrgGame\Config\BrgGraphicsConfig.ini`), and the plugin takes the window's
title bar and edges away and places it on the monitor it is on.

The window keeps the size the game draws at. The game holds its window to its
resolution - stretch it and the game puts its own size and frame straight
back, and the two flash between each other (0.1.0 did that). So set the game's
resolution to your monitor's and the borderless window fills the screen; at a
lower resolution it sits in the middle of the monitor.

Windowed, the game works its window out itself: the size it draws at, plus the
title bar and edges Windows says that needs, cut down to fit the screen. At
the monitor's own resolution the frame does not fit, so it drew smaller
(2490x1401 on 2560x1440) and shrank the window back whenever it was stretched.
So the plugin tells the game the truth about the window it will have: no
frame. Its own sums then come to the monitor's size. A window drawing at 90%
of the monitor or more each way is also given the whole monitor, unless the
game shrinks it back.

The game sets its window up again whenever the resolution or display mode
changes, so the plugin looks at the window four times a second and makes it
borderless again when it has a frame again. Should the game undo every change
at once anyway, the plugin leaves the window as the game wants it for 30
seconds rather than fight it. In the game's Fullscreen mode the window already
covers the monitor with no edges, and the plugin leaves it alone.

Safety rules, all in [dll/lid_borderless.c](dll/lid_borderless.c):

- It only does anything inside `BrgGame-Steam.exe`. Loaded by any other
  program, it changes nothing.
- It touches only the game's own windows - their style and position.
- Four of the game's calls to Windows are pointed at the plugin, in the game's
  import table (not its code): `AdjustWindowRect`/`AdjustWindowRectEx` answer
  "no frame", and `SetWindowLong`/`SetWindowLongPtr` leave the title bar and
  edges off its top-level windows. Nothing else in the game is changed.
- It never waits on the game: the window is moved with `SWP_ASYNCWINDOWPOS`.
- No network, no registry, no files but its own log, `lid-borderless.log`,
  next to itself.
- Delete `lid-borderless.dll` (or untick the mod) and the game is exactly
  stock again.

## Installing

The easy way: in LET IT DIE DB Mod Manager, **Get mods...**, then **Borderless
Fullscreen**. It installs the mod, the plugin and the Plugin Loader if you do
not have it. Then tick the mod and click **Save Mod List**, with the game
closed.

By hand: put `lid-borderless.dll` in `Binaries\Win64\lid-plugins` (with the
Plugin Loader), and set the game to Windowed in its options.

Then set the game's resolution to your monitor's in its options, so the window
fills the screen.

## Building it yourself

GitHub builds every release from this repository, and so can you:

    py -3 -m pip install ziglang==0.16.0
    py -3 build.py --test

`out/lid-borderless.dll` is the plugin, and its SHA-256 is printed - compare it
with the one on the release page. `--test` builds a stand-in game window and
checks it goes borderless at the game's own size, covers the monitor at the
monitor's resolution (including a stand-in that sizes its window the way the
game does), goes borderless again after the "game" sets it up again,
is then left alone, backs off from a game that undoes every change, and that
another program's window is never touched.

## Credits

The idea of restyling the game's window for borderless comes from xCENTx's
[LetItDie-BorderlessFullscreen-Mod](https://github.com/xCENTx/LetItDie-BorderlessFullscreen-Mod),
a program run once after the game starts, and S3er0i9ng's mod manager, which
does the same from outside the game. This plugin is written fresh, runs inside
the game, and keeps the window borderless for the whole session.

## License

MIT - see [LICENSE](LICENSE).
