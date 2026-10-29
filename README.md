WideOpen - The Wolf Among Us Remastered
=======================================

![The Wolf Among Us Remastered at 32:9](https://raw.githubusercontent.com/Lyrixxx/WideOpen/main/screenshots/TheWolfAmongUsRemastered.webp)

Ultrawide and wider support for The Wolf Among Us Remastered, plus hidden settings and extras.
Part of [WideOpen](https://github.com/Lyrixxx/WideOpen).

Download and install: see [Releases](../../releases). Drop `dinput8.dll` (and optionally
`TWAUWidescreen.ini`) next to `TheWolfAmongUsRemastered.exe`.
Linux / Steam Deck: Steam launch options `WINEDLLOVERRIDES="dinput8=n,b" %command%`.

Note: the game was made for 16:9. In widescreen you can see past the intended frame - characters meant
to be off screen (possible spoilers) and scenes that end at the edges. `Mode=16:9` in the ini gives the
original framing with black bars.

Build: `./build.sh` (clang, lld, llvm, python3). License: GPL-3.0-or-later, see [LICENSE](LICENSE).
