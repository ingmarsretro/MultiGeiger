// code related to the geiger-mueller tube hw interface
// - high voltage generation
// - GM pulse counting

#include <Arduino.h>
#if MULTIGEIGER_RECHARGE_FROM_TASK
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#endif

#include "log.h"
#include "speaker.h"
#include "timers.h"
#include "tube.h"

// The test pin, if enabled, is high while isr_GMC_count is active.
// -1 == disabled, otherwise pin 13 might be an option.
#define PIN_TEST_OUTPUT -1

// Wifi_LorA-32_V2: pin23=HV_FET_OUT, pin22=HV_CAP_FUL, pin2=GMZ_COUNT
#define PIN_HV_FET_OUTPUT 23
#define PIN_HV_CAP_FULL_INPUT 22  // !! has to be capable of "interrupt on change"
#define PIN_GMC_COUNT_INPUT 2     // !! has to be capable of "interrupt on change"
// GM count: original multigeiger-project = FALLING edge (idle HIGH, pulse LOW), plain INPUT (no internal pull).
#define GMC_COUNT_EDGE FALLING

// Dead Time of the Geiger Counter. [usec]
// Has to be longer than the complete pulse generated on the Pin PIN_GMC_COUNT_INPUT.
#define GMC_DEAD_TIME 190

TUBETYPE tubes[] = {
  // use 0.0 conversion factor for unknown tubes, so it computes an "obviously-wrong" 0.0 uSv/h value rather than a confusing one.
  {"Radiation unknown", 0, 0.0},
  // The conversion factors for SBM-20 and SBM-19 are taken from the datasheets (according to Jürgen)
  {"Radiation SBM-20", 20, 1 / 2.47},
  {"Radiation SBM-19", 19, 1 / 9.81888},
  // The Si22G conversion factor was determined by Juergen Boehringer like this:
  // Set up a Si22G based MultiGeiger close to the official odlinfo.bfs.de measurement unit in Sindelfingen.
  // Determine how many counts the Si22G gives within the same time the odlinfo unit needs for 1uSv.
  // Result: 44205 counts on the Si22G for 1 uSv.
  // So, to convert from cps to uSv/h, the calculation is: uSvh = cps * 3600 / 44205 = cps / 12.2792
  {"Radiation Si22G", 22, 1 / 12.2792}
};

volatile bool isr_GMC_cap_full;

volatile unsigned long isr_hv_pulses;
volatile bool isr_hv_charge_error;

volatile unsigned int isr_GMC_counts;
volatile unsigned long isr_count_timestamp;
volatile unsigned long isr_count_time_between;

// MUX (mutexes used for mutual exclusive access to isr variables)
portMUX_TYPE mux_cap_full = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE mux_GMC_count = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE mux_hv = portMUX_INITIALIZER_UNLOCKED;

// Maximum amount of HV capacitor charge pulses to generate in one charge cycle.
#define MAX_CHARGE_PULSES 3333

// hw timer period and microseconds -> periods conversion
#define PERIOD_DURATION_US 100
#define PERIODS(us) ((us) / PERIOD_DURATION_US)

#if MULTIGEIGER_RECHARGE_FROM_TASK
// Run recharge state machine from a task; timer ISR only wakes the task (avoids LoadProhibited when ISR calls flash code).
static SemaphoreHandle_t recharge_sem = NULL;

void IRAM_ATTR isr_recharge_wake(void) {  // minimal ISR: only wake task, no flash code
  BaseType_t woken = 0;
  if (recharge_sem != NULL)
    xSemaphoreGiveFromISR(recharge_sem, &woken);
  portYIELD_FROM_ISR(woken);
}

static void recharge_state_machine_step(void) {
  static unsigned int current = 0;
  static unsigned int next_state = 0;
  static unsigned int next_charge = PERIODS(1000000);
  enum State { init, pulse_h, pulse_l, check_full, is_full, charge_fail };
  static State state = init;
  static int charge_pulses;

  if (++current < next_state)
    return;
  current = 0;

  if (state == init) {
    charge_pulses = 0;
    portENTER_CRITICAL(&mux_cap_full);
    isr_GMC_cap_full = 0;
    portEXIT_CRITICAL(&mux_cap_full);
    state = pulse_h;
  }
  while (state < is_full) {
    if (state == pulse_h) {
      digitalWrite(PIN_HV_FET_OUTPUT, HIGH);
      state = pulse_l;
      next_state = PERIODS(1500);
      return;
    }
    if (state == pulse_l) {
      digitalWrite(PIN_HV_FET_OUTPUT, LOW);
      state = check_full;
      next_state = PERIODS(1000);
      return;
    }
    if (state == check_full) {
      charge_pulses++;
      if (isr_GMC_cap_full)
        state = is_full;
      else if (charge_pulses < MAX_CHARGE_PULSES)
        state = pulse_h;
      else
        state = charge_fail;
    }
  }
  if (state == is_full) {
    portENTER_CRITICAL(&mux_hv);
    isr_hv_charge_error = false;
    isr_hv_pulses += charge_pulses;
    portEXIT_CRITICAL(&mux_hv);
    state = init;
    if (charge_pulses <= 1)
      next_charge = next_charge * 5 / 4;
    else
      next_charge = next_charge * 2 / charge_pulses;
    if (next_charge < PERIODS(1000))
      next_charge = PERIODS(1000);
    else if (next_charge > PERIODS(10000000))
      next_charge = PERIODS(10000000);
    next_state = next_charge;
    return;
  }
  if (state == charge_fail) {
    portENTER_CRITICAL(&mux_hv);
    isr_hv_charge_error = true;
    isr_hv_pulses += charge_pulses;
    portEXIT_CRITICAL(&mux_hv);
    state = init;
    next_charge = PERIODS(1000000);
    // Retry after 30 s (was 10 min). If HV_CAP_FUL (pin 22) never fires, check wiring or use FALLING if circuit is active-low.
#ifndef MULTIGEIGER_HV_CHARGE_FAIL_RETRY_MS
#define MULTIGEIGER_HV_CHARGE_FAIL_RETRY_MS (30 * 1000)
#endif
    next_state = PERIODS((unsigned long)MULTIGEIGER_HV_CHARGE_FAIL_RETRY_MS * 1000);
  }
}

static void recharge_task(void *arg) {
  for (;;) {
    if (recharge_sem != NULL && xSemaphoreTake(recharge_sem, portMAX_DELAY) == pdTRUE)
      recharge_state_machine_step();
  }
}
#else
void IRAM_ATTR isr_recharge() {
  static unsigned int current = 0;
  static unsigned int next_state = 0;
  static unsigned int next_charge = PERIODS(1000000);
  if (++current < next_state)
    return;
  current = 0;

  enum State { init, pulse_h, pulse_l, check_full, is_full, charge_fail };
  static State state = init;
  static int charge_pulses;
  if (state == init) {
    charge_pulses = 0;
    portENTER_CRITICAL_ISR(&mux_cap_full);
    isr_GMC_cap_full = 0;
    portEXIT_CRITICAL_ISR(&mux_cap_full);
    state = pulse_h;
  }
  while (state < is_full) {
    if (state == pulse_h) {
      digitalWrite(PIN_HV_FET_OUTPUT, HIGH);
      state = pulse_l;
      next_state = PERIODS(1500);
      return;
    }
    if (state == pulse_l) {
      digitalWrite(PIN_HV_FET_OUTPUT, LOW);
      state = check_full;
      next_state = PERIODS(1000);
      return;
    }
    if (state == check_full) {
      charge_pulses++;
      if (isr_GMC_cap_full)
        state = is_full;
      else if (charge_pulses < MAX_CHARGE_PULSES)
        state = pulse_h;
      else
        state = charge_fail;
    }
  }
  if (state == is_full) {
    portENTER_CRITICAL_ISR(&mux_hv);
    isr_hv_charge_error = false;
    isr_hv_pulses += charge_pulses;
    portEXIT_CRITICAL_ISR(&mux_hv);
    state = init;
    if (charge_pulses <= 1)
      next_charge = next_charge * 5 / 4;
    else
      next_charge = next_charge * 2 / charge_pulses;
    if (next_charge < PERIODS(1000))
      next_charge = PERIODS(1000);
    else if (next_charge > PERIODS(10000000))
      next_charge = PERIODS(10000000);
    next_state = next_charge;
    return;
  }
  if (state == charge_fail) {
    portENTER_CRITICAL_ISR(&mux_hv);
    isr_hv_charge_error = true;
    isr_hv_pulses += charge_pulses;
    portEXIT_CRITICAL_ISR(&mux_hv);
    state = init;
    next_charge = PERIODS(1000000);
    next_state = PERIODS(10 * 60 * 1000000);
  }
}
#endif

void IRAM_ATTR isr_GMC_capacitor_full() {
  portENTER_CRITICAL_ISR(&mux_cap_full);
  isr_GMC_cap_full = 1;
  portEXIT_CRITICAL_ISR(&mux_cap_full);
}

void read_hv(bool *hv_error, unsigned long *pulses) {
  portENTER_CRITICAL(&mux_hv);
  *pulses = isr_hv_pulses;
  *hv_error = isr_hv_charge_error;
  portEXIT_CRITICAL(&mux_hv);
}

void IRAM_ATTR isr_GMC_count() {
  unsigned long now;
  static unsigned long last;
  portENTER_CRITICAL_ISR(&mux_GMC_count);
  #if PIN_TEST_OUTPUT >= 0
  digitalWrite(PIN_TEST_OUTPUT, HIGH);
  #endif
  now = micros();  // unsigned long == uint32_t == 32bit -> overflows after ~72 minutes.
  // safely compute dt, even if <now> has overflowed and <last> not [yet]:
  unsigned long dt = (now >= last) ? (now - last) : ((now + (1 << 30)) - (last + (1 << 30)));
  if (dt > GMC_DEAD_TIME) {
    // We only consider a pulse valid if it happens more than GMC_DEAD_TIME after the last valid pulse.
    // Reason: Pulses occurring short after a valid pulse are false pulses generated by the rising edge on the PIN_GMC_COUNT_INPUT.
    //         This happens because we don't have a Schmitt trigger on this controller pin.
    isr_count_timestamp = millis();        // remember (system) time of the pulse
    isr_count_time_between = dt;           // save for statistics debuging
    isr_GMC_counts++;                      // count the pulse
    last = now;                            // remember timestamp of last **valid** pulse
    // Tick gated by speaker_tick_wanted (set from DIP switches in loop). Rate-limit: max 20/s.
    static unsigned long last_tick_us = 0;
    unsigned long elapsed = (now >= last_tick_us) ? (now - last_tick_us) : (now + (1ul << 31)) - (last_tick_us + (1ul << 31));
    if (elapsed >= 50000) {
      last_tick_us = now;
      tick(true);
    }
  }
  #if PIN_TEST_OUTPUT >= 0
  digitalWrite(PIN_TEST_OUTPUT, LOW);
  #endif
  portEXIT_CRITICAL_ISR(&mux_GMC_count);
}

void read_GMC(unsigned long *counts, unsigned long *timestamp, unsigned int *between) {
  portENTER_CRITICAL(&mux_GMC_count);
  *counts += isr_GMC_counts;
  isr_GMC_counts = 0;
  *timestamp = isr_count_timestamp;
  *between = isr_count_time_between;
  portEXIT_CRITICAL(&mux_GMC_count);
}

void setup_tube(void) {
#if PIN_TEST_OUTPUT >= 0
  pinMode(PIN_TEST_OUTPUT, OUTPUT);
#endif
  pinMode(PIN_HV_FET_OUTPUT, OUTPUT);
  pinMode(PIN_HV_CAP_FULL_INPUT, INPUT);
  pinMode(PIN_GMC_COUNT_INPUT, INPUT);  // original multigeiger: no pull-up/pull-down

#if PIN_TEST_OUTPUT >= 0
  digitalWrite(PIN_TEST_OUTPUT, LOW);
#endif
  digitalWrite(PIN_HV_FET_OUTPUT, LOW);

  // note: we do not need to get the portMUX here as we did not yet enable interrupts.
  isr_count_timestamp = 0;
  isr_count_time_between = 0;
  isr_GMC_cap_full = 0;
  isr_GMC_counts = 0;
  isr_hv_pulses = 0;
  isr_hv_charge_error = true;  // assume no tube until first successful charge (avoids tick sound from noise at boot)

  attachInterrupt(digitalPinToInterrupt(PIN_HV_CAP_FULL_INPUT), isr_GMC_capacitor_full, RISING);  // capacitor full
  attachInterrupt(digitalPinToInterrupt(PIN_GMC_COUNT_INPUT), isr_GMC_count, GMC_COUNT_EDGE);    // GMC pulse detected

#if MULTIGEIGER_RECHARGE_FROM_TASK
  recharge_sem = xSemaphoreCreateBinary();
  if (recharge_sem != NULL) {
    xTaskCreate(recharge_task, "recharge", 2048, NULL, 2, NULL);
    setup_recharge_timer(isr_recharge_wake, PERIOD_DURATION_US);
  } else {
    setup_recharge_timer(isr_recharge_wake, PERIOD_DURATION_US);  // no task, HV won't run
  }
#else
  setup_recharge_timer(isr_recharge, PERIOD_DURATION_US);
#endif
}
