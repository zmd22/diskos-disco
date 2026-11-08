# Boomshine 1.2.0: NeoN / Pop!

A chain-reaction game for the Snowsky Disc's round screen. Since diskOS Disco! 1.5.0 it is built into the
interface (`mq_ui --boomshine`), so there is nothing to install: add **Boomshine** to the Disco Menu, or open it
from All apps or a Shortcut, then tap **Play** on its landing page. Hold Volume Up for 3 seconds to force it
closed if it ever stops responding.

## Play

Choose NeoN or Pop! on the landing page. Both use the same twenty-level game:
tap once to begin a chain reaction and catch at least the target number of dots.
The top label shows caught / target; the bottom shows the level number.
NeoN uses bright outlined bursts on black. Pop! uses soft pastel bursts on deep
blue. The edge colour is randomly selected from the mode's palette each level.
The playing field now extends to the edge, with five pixels reserved for the
border. Levels 1-12 retain their previous settings. Levels 13-20 add five dots each,
ending at 100 dots with a 96-dot target. Burst size reduces gradually in those
later levels so the higher density does not make them easier.

Hold a still finger on the screen for 0.85 seconds to return to the landing
page. A 3.5-second toast explains this whenever you choose a mode. The release
after a long hold is ignored, so it cannot accidentally start a new game.
There is no pause control. Exit on the landing page returns to DiskOS.
Swipes do not fire. A short tap fires on release, allowing long holds to be
recognized without spending the one shot.

Successful rounds add 10 points per caught dot plus 25 per level number.
Best score and unlocked level are kept in /usr/data/boomshine/scores.conf and survive updates. Scores from
the earlier separate install (/usr/data/apps/boomshine) are copied over once.
Choosing either mode starts at level 1. The two modes share the existing score.
Music is not controlled by this app; the game is silent.

## In this repository

`main.c` and `game.c` are compiled into `mq_ui` with `-DBOOMSHINE_BUILTIN` (see `ui/Makefile`); `fonts.h`
holds the embedded glyphs, run-length encoded by `ui/tools/boomshine_font_rle.py` and drawn straight from the
encoded stream. The game writes its log to /tmp/boomshine.log.
