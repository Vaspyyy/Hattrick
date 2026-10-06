// Hatrick audio engine (audio.c, built on miniaudio). All calls come from the game thread.
// Sound ids, themes and stems are in sound.h.

// Opens the output device and loads the sounds from dir (the assets folder). silent uses a
// null device (nothing reaches the speakers, mixing still runs); dump, if set, also writes the
// mixed output to that WAV file. Returns 0 when audio is unavailable; every call is then a no-op.
int snd_init(const char *dir, int silent, const char *dump);
void snd_quit(void);

void snd_play(int sound, float pan);          // pan -1 (left) .. 1 (right)
void snd_theme(int theme, int restart);       // switch theme, or restart it from beat 0
void snd_stem(int stem, float gain, int ms);  // fade a music stem toward gain over ms
void snd_volume(float master, int muted);     // master 0..1, smoothly applied
double snd_beat(void);                        // music position of the current loop, in beats
