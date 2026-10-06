// Sound ids shared by the game logic (hatrick.c) and the audio engine (audio.c).
// The order matches SOUND_FILE in audio.c.
enum { S_COIN, S_JUMP, S_JUMP2, S_JUMP3, S_FLIP, S_LONGJ, S_SPIN, S_ROLL, S_DIVE, S_THROW, S_CATCH, S_BOUNCE,
       S_STOMP, S_BRICK, S_GPSPIN, S_GPLAND, S_SPRING, S_WALLJ, S_LAND, S_SKID, S_LEDGE,
       S_MENUMOVE, S_MENUOK, S_MENUBACK, S_PAUSE, S_CLEAR, S_DEATH, NSOUND };
// Music stems: every theme folder in assets/music/ can hold one file per stem (MODDING.md).
enum { STEM_LEAD, STEM_BASS, STEM_PERC, STEM_BELLS, STEM_FAST, STEM_ARP, STEM_BAH, NSTEM };
