/* Playing a downloaded game: the launcher EmuDeck generated, with the game's
 * file. RommDeck downloads and keeps saves in step; it has never started an
 * emulator, and this is the smallest thing that changes that: find the game on
 * disk, find the launcher for the emulator it came down for, hand it the path.
 */
#ifndef ROMM_PLAY_H
#define ROMM_PLAY_H

#include <stddef.h>

/* Starts the game's emulator, detached, and returns 0. -1 with err set when the
 * game isn't on the machine, when its emulator has no launcher, or when the
 * launcher won't start. */
int play_launch(int rom_id, char *err, size_t errlen);

/* The launcher this game's emulator names, as a path, without checking it
 * exists. Empty when the emulator is unknown. */
void play_launcher_for(int rom_id, char *out, size_t outlen);

/* Where EmuDeck writes its launchers: $ROMM_PLAY_LAUNCHERS, else
 * ~/Emulation/tools/launchers. */
void play_launcher_dir(char *out, size_t outlen);

#endif
