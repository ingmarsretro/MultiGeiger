// speaker / sound related code

#include <Arduino.h>
#include <driver/mcpwm.h>
#if MULTIGEIGER_RECHARGE_FROM_TASK
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#endif

#include "hal/heltecv2.h"
#include "speaker.h"
#include "timers.h"

// Wifi_LorA-32_V2: speaker between GPIO0 and GPIO13 (MCPWM0A, MCPWM0B). Idle = both LOW (0 V across speaker).
// Note: GPIO0 is strapping pin; keep floating or high at boot for normal start. No series R needed if only driven after boot.
#define PIN_SPEAKER_OUTPUT_P 0
#define PIN_SPEAKER_OUTPUT_N 13

// shall the speaker "tick"?
static volatile bool speaker_tick;  // current state
static bool speaker_tick_wanted;  // state wanted by user

// MUX (mutexes used for mutual exclusive access to isr variables)
portMUX_TYPE mux_audio = portMUX_INITIALIZER_UNLOCKED;

volatile int *isr_audio_sequence = NULL;
volatile int *isr_tick_sequence = NULL;
volatile int *isr_sequence = NULL;  // currently played sequence

static int tick_sequence[8];
// Short rising three-tone chime at boot when "Start sound" is enabled (freq_mHz, volume, led, ms)
static int startup_sound_sequence[] = {
  1600000, 1, -1, 90,
  0, 0, -1, 40,
  2200000, 1, -1, 90,
  0, 0, -1, 40,
  3000000, 1, -1, 120,
  0, 0, -1, 0
};
static int alarm_sequence[12] = {
  // "high_Pitch"
  3000000, 1, -1, 400,  // frequency_mHz, volume, LED (-1 = don't touch), duration_ms
  // "low_Pitch"
  1000000, 1, -1, 400,
  // "off"
  0, 0, -1, 0 // duration_ms = 0 --> END
};


// hw timer period and microseconds -> periods conversion
#define PERIOD_DURATION_US 1000
#define PERIODS(us) ((us) / PERIOD_DURATION_US)

#if MULTIGEIGER_RECHARGE_FROM_TASK
static SemaphoreHandle_t audio_sem = NULL;

void IRAM_ATTR isr_audio_wake(void) {
  BaseType_t woken = 0;
  if (audio_sem != NULL)
    xSemaphoreGiveFromISR(audio_sem, &woken);
  portYIELD_FROM_ISR(woken);
}

// Min silence [ms] after a tick before the next tick can start (avoids long continuous beep when many pulses).
#define TICK_COOLDOWN_MS 60

static void audio_sequencer_step(void) {
  static unsigned int current = 0;
  static unsigned int next = PERIODS(1000);
  if (++current < next)
    return;
  current = 0;

  int frequency_mHz = 0, volume = 0, led = 0, duration_ms = 0;
  static bool playing_audio = false, playing_tick = false;
  static unsigned long last_tick_end_ms = 0;

  unsigned long now_ms = millis();
  portENTER_CRITICAL(&mux_audio);
  if (!isr_sequence) {
    if (isr_audio_sequence) {
      isr_sequence = isr_audio_sequence;
      playing_audio = true;
    } else if (isr_tick_sequence && (now_ms - last_tick_end_ms) >= TICK_COOLDOWN_MS) {
      isr_sequence = isr_tick_sequence;
      playing_tick = true;
    }
  }
  volatile int *p = isr_sequence;
  if (p) {
    frequency_mHz = *p++;
    volume = *p++;
    led = *p++;
    duration_ms = *p++;
    isr_sequence = p;
  }
  portEXIT_CRITICAL(&mux_audio);

  if (!p) {
    // No sequence: 0 V across speaker (both pins same level) = silent; was A=H B=L (DC across piezo = tone).
    mcpwm_stop(MCPWM_UNIT_0, MCPWM_TIMER_0);
    mcpwm_set_signal_low(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_A);
    mcpwm_set_signal_low(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_B);
    return;
  }

  if (frequency_mHz > 0) {
    if (volume >= 1) {
      mcpwm_set_duty_type(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_A, MCPWM_DUTY_MODE_0);
      mcpwm_set_duty_type(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_B, MCPWM_DUTY_MODE_1);
    } else {
      mcpwm_set_duty_type(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_A, MCPWM_DUTY_MODE_0);
      mcpwm_set_signal_low(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_B);
    }
    mcpwm_set_frequency(MCPWM_UNIT_0, MCPWM_TIMER_0, frequency_mHz / 1000);
    mcpwm_start(MCPWM_UNIT_0, MCPWM_TIMER_0);
  } else {
    mcpwm_stop(MCPWM_UNIT_0, MCPWM_TIMER_0);
    mcpwm_set_signal_low(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_A);
    mcpwm_set_signal_low(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_B);
  }

  if (led >= 0 && HAS_LED != NOT_A_PIN)
    digitalWrite(HAS_LED, led ? HIGH : LOW);

  if (duration_ms > 0) {
    next = PERIODS(duration_ms * 1000);
  } else {
    // End of sequence: 0 V across speaker
    mcpwm_stop(MCPWM_UNIT_0, MCPWM_TIMER_0);
    mcpwm_set_signal_low(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_A);
    mcpwm_set_signal_low(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_B);
    portENTER_CRITICAL(&mux_audio);
    isr_sequence = NULL;
    if (playing_tick) {
      isr_tick_sequence = NULL;
      playing_tick = false;
      last_tick_end_ms = millis();
    } else if (playing_audio) {
      isr_audio_sequence = NULL;
      playing_audio = false;
    }
    portEXIT_CRITICAL(&mux_audio);
    next = PERIODS(1000);
  }
}

static void audio_task(void *arg) {
  for (;;) {
    if (audio_sem != NULL && xSemaphoreTake(audio_sem, portMAX_DELAY) == pdTRUE)
      audio_sequencer_step();
  }
}
#else
void IRAM_ATTR isr_audio() {
  static unsigned int current = 0;
  static unsigned int next = PERIODS(1000);
  if (++current < next)
    return;
  current = 0;

  int frequency_mHz = 0, volume = 0, led = 0, duration_ms = 0;
  static bool playing_audio = false, playing_tick = false;

  portENTER_CRITICAL_ISR(&mux_audio);
  if (!isr_sequence) {
    if (isr_audio_sequence) {
      isr_sequence = isr_audio_sequence;
      playing_audio = true;
    } else if (isr_tick_sequence) {
      isr_sequence = isr_tick_sequence;
      playing_tick = true;
    }
  }
  volatile int *p = isr_sequence;
  if (p) {
    frequency_mHz = *p++;
    volume = *p++;
    led = *p++;
    duration_ms = *p++;
    isr_sequence = p;
  }
  portEXIT_CRITICAL_ISR(&mux_audio);

  if (!p) {
    mcpwm_stop(MCPWM_UNIT_0, MCPWM_TIMER_0);
    mcpwm_set_signal_low(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_A);
    mcpwm_set_signal_low(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_B);
    return;
  }

  if (frequency_mHz > 0) {
    if (volume >= 1) {
      mcpwm_set_duty_type(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_A, MCPWM_DUTY_MODE_0);
      mcpwm_set_duty_type(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_B, MCPWM_DUTY_MODE_1);
    } else {
      mcpwm_set_duty_type(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_A, MCPWM_DUTY_MODE_0);
      mcpwm_set_signal_low(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_B);
    }
    mcpwm_set_frequency(MCPWM_UNIT_0, MCPWM_TIMER_0, frequency_mHz / 1000);
    mcpwm_start(MCPWM_UNIT_0, MCPWM_TIMER_0);
  } else {
    mcpwm_stop(MCPWM_UNIT_0, MCPWM_TIMER_0);
    mcpwm_set_signal_low(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_A);
    mcpwm_set_signal_low(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_B);
  }

  if (led >= 0 && HAS_LED != NOT_A_PIN)
    digitalWrite(HAS_LED, led ? HIGH : LOW);

  if (duration_ms > 0) {
    next = PERIODS(duration_ms * 1000);
  } else {
    portENTER_CRITICAL_ISR(&mux_audio);
    isr_sequence = NULL;
    if (playing_tick) {
      isr_tick_sequence = NULL;
      playing_tick = false;
    } else if (playing_audio) {
      isr_audio_sequence = NULL;
      playing_audio = false;
    }
    portEXIT_CRITICAL_ISR(&mux_audio);
    next = PERIODS(1000);
  }
}
#endif

void IRAM_ATTR tick(bool high) {
  // high true: "tick" -> high frequency tick
  // high false: "tock" -> lower frequency tock
  // called from ISR!
  portENTER_CRITICAL_ISR(&mux_audio);

  int *sequence;

  if (speaker_tick) {
    sequence = tick_sequence;
    sequence[0] = high ? 5000000 : 1000000;  // frequency_mHz
    sequence[1] = 1;  // volume
    sequence[2] = -1;  // LED (unused)
    sequence[3] = 4;  // duration_ms
    sequence[4] = 0;
    sequence[5] = 0;
    sequence[6] = -1;
    sequence[7] = 0;  // END
  } else
    sequence = NULL;

  isr_tick_sequence = sequence;
  portEXIT_CRITICAL_ISR(&mux_audio);
}

void tick_enable(bool enable) {
  // true -> bring ticking into the state desired by user
  // false -> disable ticking (e.g. when accessing flash)
  if (enable) {
    speaker_tick = speaker_tick_wanted;
  } else {
    speaker_tick = false;
  }
}

void apply_switches_tick(bool speaker_sw, bool want_speaker_tick) {
  speaker_tick_wanted = want_speaker_tick && speaker_sw;
  tick_enable(true);
}

void alarm() {
  // play alarm sound, called from normal code (not ISR)
  portENTER_CRITICAL(&mux_audio);
  isr_audio_sequence = alarm_sequence;
  portEXIT_CRITICAL(&mux_audio);
}

void play(int *sequence) {
  // play a tone sequence, called from normal code (not ISR)
  portENTER_CRITICAL(&mux_audio);
  isr_audio_sequence = sequence;
  portEXIT_CRITICAL(&mux_audio);
}

#define TONE(f, v, led, t) {int(f * 0.75), v, led, int(t * 85)}

void setup_speaker(bool playSound, bool _speaker_tick) {
  if (HAS_LED != NOT_A_PIN) {
    pinMode(HAS_LED, OUTPUT);
    digitalWrite(HAS_LED, LOW);
  }

  mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM0A, PIN_SPEAKER_OUTPUT_P);
  mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM0B, PIN_SPEAKER_OUTPUT_N);

  mcpwm_config_t pwm_config;
  pwm_config.frequency = 1000;
  // set duty cycles to 50% (and never modify them again!)
  pwm_config.cmpr_a = 50.0;
  pwm_config.cmpr_b = 50.0;
  pwm_config.duty_mode = MCPWM_DUTY_MODE_0;  // active high duty
  pwm_config.counter_mode = MCPWM_UP_COUNTER;
  mcpwm_init(MCPWM_UNIT_0, MCPWM_TIMER_0, &pwm_config);

#if MULTIGEIGER_RECHARGE_FROM_TASK
  audio_sem = xSemaphoreCreateBinary();
  if (audio_sem != NULL) {
    xTaskCreate(audio_task, "audio", 2048, NULL, 1, NULL);
    setup_audio_timer(isr_audio_wake, PERIOD_DURATION_US);
  }
#else
  setup_audio_timer(isr_audio, PERIOD_DURATION_US);
#endif

  tick_enable(false);  // no ticking until we set wanted state below

  speaker_tick_wanted = _speaker_tick;
  tick_enable(true);

  // Idle output: 0 V across speaker (both pins LOW). Was A=H B=L = DC across piezo = tone.
  mcpwm_stop(MCPWM_UNIT_0, MCPWM_TIMER_0);
  mcpwm_set_signal_low(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_A);
  mcpwm_set_signal_low(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_B);

  if (playSound)
    play(startup_sound_sequence);
}
