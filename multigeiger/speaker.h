// speaker / sound related code
// also handles the onboard LED, which lights up while speaker ticks.

#ifndef _SPEAKER_H_
#define _SPEAKER_H_

void setup_speaker(bool playSound, bool speaker_tick);
void tick_enable(bool enable);
void apply_switches_tick(bool speaker_sw, bool want_speaker_tick);
void tick(bool high);
void alarm();

#endif // _SPEAKER_H_
