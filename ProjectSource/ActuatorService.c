/****************************************************************************
 Module
   ActuatorService.c

 Description
   Runs the moving parts: two DC gearmotors on one dual H-bridge (ENA/IN1/IN2
   for channel A, ENB/IN3/IN4 for channel B), and the buzzer.

     Plate motor (channel B)
       ENB  RB15 (pin 26, the schematic's "Plate Servo" pin)  PWM from OC1
       IN3 / IN4  second 74HC595 QF / QG (set through DisplayChainService)
       During a game the potentiometer sets speed and direction: centre =
       stopped, turn one way = spin one way, faster the further you turn.
     TREAT dispenser (RB11, pin 22, the schematic's "Dispenser Servo" pin,
     PWM from OC2). Either of two parts, chosen with DISPENSER_IS_SERVO:
       0: the small DC gearmotor on H-bridge channel A. RB11 drives ENA,
          IN1 is wired to 3.3 V and IN2 to ground. ES_DISPENSE_TREAT runs it
          at DISPENSE_DUTY for DISPENSE_TIME_MS.
       1: a hobby servo with its signal wire on RB11. ES_DISPENSE_TREAT
          moves it to DISPENSER_DROP_US for DISPENSE_TIME_MS, then back to
          DISPENSER_REST_US. Uses Timer3 at 50 Hz.
     Buzzer  "Bell" on the first 74HC595 QA (DisplayChainService)

 Notes
   Owner: Christopher. Keyboard test and direction-change handling are his.
   The plate PWM (and the dispenser motor's) comes from Timer2 at 1 kHz
   (PBCLK 20 MHz, 1:1, PR2 = 19999). A dispenser servo uses Timer3 instead.
   The plate motor's encoder isn't used (no free pins, and the pot sets
   speed, not position).
   The direction is changed only while the plate's PWM is at 0, and the new
   speed is applied one update later, after DisplayChainService has shifted
   the new direction out.
   The duty and time numbers below are starting values: tune them on the
   real mechanism.

   Test keys (any time): a plate follows the pot, j forward and k reverse at
   TEST_DUTY, x stop, d dispense a TREAT, b ding, ? status.

 History
 When           Who          What/Why
 -------------- ---          --------
 11/09/26       ---          stub
 11/12/26       Christopher  plate motor test: start/stop, direction
 11/13/26       Christopher  pot direction control with a centre stop (tested)
 11/13/26       ---          integration: ENA/ENB on the schematic's RB11 /
                             RB15 with PWM, IN3/IN4 moved off RA0/RA1
                             (Hall South / West) to spare 595 outputs, pot
                             speed control, dispenser and ding; option for
                             a servo dispenser
****************************************************************************/
/*----------------------------- Include Files -----------------------------*/
#include "../ProjectHeaders/ActuatorService.h"

#include <xc.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/attribs.h>

#include "ES_Configure.h"
#include "ES_Framework.h"
#include "ES_Port.h"
#include "ES_Timers.h"
#include "terminal.h"
#include "dbprintf.h"
#include "AnalogInputs.h"
#include "DisplayChainService.h"

/*----------------------------- Module Defines ----------------------------*/
#define PWM_PERIOD_TICKS     19999u       // 1 kHz at 20 MHz
#define DUTY_TO_OCRS(pct)    ((uint16_t)(((uint32_t)(PWM_PERIOD_TICKS + 1u) * (pct)) / 100u))

#define POT_CENTER           512
#define POT_DEADBAND         64           // +/- counts around the centre = stopped (Christopher's tested value)
#define PLATE_MIN_DUTY       30           // % just past the dead band (enough to move)
#define PLATE_MAX_DUTY       70           // % at the end of the pot travel
#define PLATE_UPDATE_MS      20           // how often the plate follows the pot
#define TEST_DUTY            50           // % for the j / k test keys
#define COUNTS_PER_CLOCKWISE_REV 681
#define FOLLOW_POT_MIN          0
#define FOLLOW_POT_MAX          1023
#define FORWARD_COUNT_SIGN      1     // Raw count sign when IN3 is high, IN4 low

#define FOLLOW_MAX_DUTY   60u   // Maximum plate PWM
#define FOLLOW_MIN_DUTY   30u   // Running duty near the target
#define FOLLOW_DUTY_STEP   8u   // Rise by up to 8 percentage points per 20 ms

#define STOP_BAND_COUNTS        4     // About 2 degrees
#define RESTART_BAND_COUNTS     8     // About 4 degrees
#define COAST_MS                60u
#define NO_PROGRESS_MS          300u
#define MAX_DRIVE_MS            3000u
#define PWM_TIMEOUT_TICKS       100u  // About 100 ms at 1 kHz

#define DISPENSER_IS_SERVO   0            // 0 = small DC gearmotor, 1 = hobby servo

#if DISPENSER_IS_SERVO
#define SERVO_PERIOD_TICKS   49999u       // Timer3 at 2.5 MHz: 20 ms
#define US_TO_SERVO_TICKS(us) ((uint16_t)(((uint32_t)(us) * 5u) / 2u))
#define DISPENSER_REST_US    1000u        // holding the TREATs in
#define DISPENSER_DROP_US    2000u        // letting one TREAT out
#define DISPENSE_TIME_MS     700          // time at the drop position
#else
#define DISPENSE_DUTY        60           // %
#define DISPENSE_TIME_MS     1000         // how long the motor runs per TREAT
#endif
#define DING_TIME_MS         400          // how long the buzzer sounds

#define OC_PPS_CODE          0b0101       // RPB15R = OC1, RPB11R = OC2

typedef enum { DirStop = 0, DirForward = 1, DirReverse = 2 } MotorDirection_t;

/*---------------------------- Module Functions ---------------------------*/
static void drivePlate(MotorDirection_t Dir, uint8_t Duty);
static void setPlateDuty(uint8_t Duty);
static void runDispenser(bool On);
static void setDirection(MotorDirection_t Dir);
static void setBell(bool On);
static void stopPlate(void);

/*---------------------------- Module Variables ---------------------------*/
static uint8_t MyPriority;
static volatile int32_t encoderCount;
static bool PlateFollowsPot = false;
static bool Dispensing = false;
static MotorDirection_t CurrentDirection = DirStop;
static uint8_t CurrentDuty = 0;
static uint8_t PendingDuty = 0;
static bool DirectionSettling = false;
typedef enum {
    Pickup, Holding, Coasting, AwaitDirection, Driving, Paused, PlateFault
} FollowState;

static FollowState followState = Pickup;

static const char *const followNames[] = {
    "match knob", "holding", "coasting", "await 595",
    "driving", "paused", "fault"
};
static const char *followReason = "none";

static volatile uint8_t pwmTicksLeft;
static volatile bool pwmTimedOut;

static uint16_t offSince, requestSince, progressSince, driveSince;
static uint16_t directionRequest;
static int32_t previousPosition, progressPosition;
static int driveSign;
static void failPlate(const char *reason, uint16_t now);
static int32_t targetFromPot(uint16_t pot);
static void plateOff(FollowState next, uint16_t now);
static void requestDirection(int sign, uint16_t now);

/*------------------------------ Module Code ------------------------------*/
static void updatePlate(uint16_t now)
{
    int32_t position = encoderCount;
    int64_t moved = (int64_t)position - previousPosition;
    previousPosition = position;

    if (followState == Paused || followState == PlateFault) {
        return;
    }

    uint16_t pot;
    if (!TryGetPotReading(&pot)) {
        if (followState != Pickup) {
            failPlate("pot sample stale", now);
        }
        return;
    }

    int64_t error = (int64_t)targetFromPot(pot) - position;
    int64_t distance = error >= 0 ? error : -error;

    // At startup, engage only after the knob matches the plate position.
    if (followState == Pickup) {
        if (distance <= RESTART_BAND_COUNTS) {
            followState = Holding;
        }
        return;
    }

    // Stop near the target. Allow a little drift before restarting.
    if (distance <= STOP_BAND_COUNTS ||
        (followState == Holding && distance <= RESTART_BAND_COUNTS)) {
        plateOff(Holding, now);
        return;
    }

    int sign = (error > 0) ? 1 : -1;

    if (followState == AwaitDirection && sign == driveSign) {
        if ((uint16_t)(now - requestSince) >= 150u) {
            failPlate("595 timeout", now);
        }
        return;
    }

    // Coast before reversing. Check that the shaft has slowed sufficiently.
    bool movingAgainst = sign * moved < -2;
    if (followState != Driving || sign != driveSign ||
        (CurrentDuty == 0u && movingAgainst)) {
        plateOff(Coasting, now);

        if ((uint16_t)(now - offSince) >= COAST_MS && !movingAgainst) {
            requestDirection(sign, now);
        }
        return;
    }

    if (CurrentDuty == 0u) {
        driveSince = now;
        progressSince = now;
        progressPosition = position;
    } else {
        int64_t progress = sign * ((int64_t)position - progressPosition);

        if (progress <= -4) {
            failPlate("wrong direction", now);
            return;
        }

        if (progress >= 2) {
            progressPosition = position;
            progressSince = now;
        }

        if ((uint16_t)(now - progressSince) >= NO_PROGRESS_MS ||
            (uint16_t)(now - driveSince) >= MAX_DRIVE_MS) {
            failPlate("stall or drive timeout", now);
            return;
        }
    }

    // Request less PWM near the target and ramp up gradually.
    unsigned scale = distance < 90 ? (unsigned)distance : 90u;
    unsigned duty = FOLLOW_MIN_DUTY +
                    scale * (FOLLOW_MAX_DUTY - FOLLOW_MIN_DUTY) / 90u;
  // Ramp toward 60% until the encoder confirms the plate has started moving.
    if (progressSince == driveSince) {
    duty = FOLLOW_MAX_DUTY;
  }
    if (duty > CurrentDuty + FOLLOW_DUTY_STEP) {
        duty = CurrentDuty + FOLLOW_DUTY_STEP;
    }

    setPlateDuty((uint8_t)duty);
}

void __ISR(_TIMER_2_VECTOR, IPL2SOFT) PlatePwmTimeoutISR(void)
{
    IFS0CLR = _IFS0_T2IF_MASK;

    if (pwmTicksLeft != 0u && --pwmTicksLeft == 0u) {
        OC1RS = 0;
        pwmTimedOut = true;
    }
}

static void plateOff(FollowState next, uint16_t now)
{
    if (CurrentDuty != 0u) {
        offSince = now;
    }
    setPlateDuty(0);
    followState = next;
}

static void failPlate(const char *reason, uint16_t now)
{
    plateOff(PlateFault, now);
    followReason = reason;
}

static unsigned scaledPot(uint16_t pot, unsigned fullScale)
{
    if (pot <= FOLLOW_POT_MIN) return 0;
    if (pot >= FOLLOW_POT_MAX) return fullScale;

    unsigned span = FOLLOW_POT_MAX - FOLLOW_POT_MIN;
    return ((uint32_t)(pot - FOLLOW_POT_MIN) * fullScale + span / 2u) / span;
}

static int32_t targetFromPot(uint16_t pot)
{
    int32_t counts = (int32_t)scaledPot(pot, (unsigned)abs(COUNTS_PER_CLOCKWISE_REV));
    return COUNTS_PER_CLOCKWISE_REV > 0 ? counts : -counts;
}

static void requestDirection(int sign, uint16_t now)
{
    // Number each request so an old reply cannot start the motor.
    uint8_t tag = (uint8_t)((directionRequest >> 8) + 1u);
    if (tag == 0u) {
        tag = 1u;
    }

    MotorDirection_t dir = (sign == FORWARD_COUNT_SIGN) ? DirForward : DirReverse;
    directionRequest = ((uint16_t)tag << 8) | (uint16_t)dir;

    ES_Event_t command = { ES_CHAIN_SET_MOTOR_DIR, directionRequest };
    if (!PostDisplayChainService(command)) {
        failPlate("595 queue full", now);
        return;
    }

    driveSign = sign;
    requestSince = now;
    followState = AwaitDirection;
}

void __ISR(_EXTERNAL_1_VECTOR, IPL2SOFT) PlateEncoderISR(void)
{
  IFS0CLR = _IFS0_INT1IF_MASK;

  // A just rose. B tells us which direction the shaft was turning.
  if ((PORTB & (1u << 1)) != 0u) {
    if (encoderCount < INT32_MAX) {
      encoderCount++;
    }
  } else if (encoderCount > -INT32_MAX) {
    encoderCount--;
  }
}

bool InitActuatorService(uint8_t Priority)
{
  ES_Event_t init = { ES_INIT, 0 };
  const uint32_t motorEnables = (1u << 11) | (1u << 15);
  const uint32_t encoderPins  = (1u << 0)  | (1u << 1);

  MyPriority = Priority;

  // Hold ENA (pin 22) and ENB (pin 26) low.
  LATBCLR = motorEnables;
  ANSELBCLR = (1u << 15);
  TRISBCLR = motorEnables;

  // Read encoder A on RB0 (pin 4) and B on RB1 (pin 5).
  TRISBSET = encoderPins;
  ANSELBCLR = encoderPins;
  IEC0CLR = _IEC0_INT1IE_MASK;   // Keep INT1 off during setup
encoderCount = 0;

INT1R = 2u;                    // Connect RB0 (encoder A) to INT1
INTCONbits.INT1EP = 1;         // Trigger on a rising edge
IPC1bits.INT1IP = 2;           // Match IPL2 in the handler above
IPC1bits.INT1IS = 0;
IFS0CLR = _IFS0_INT1IF_MASK;   // Discard any flag left from setup

if (INT1R == 2u) {
  IEC0SET = _IEC0_INT1IE_MASK;
}
// Dispenser ENA: hold RB11 / PIC pin 22 low.
LATBCLR = (1u << 11);
OC2CONbits.ON = 0;
RPB11R = 0;
TRISBCLR = (1u << 11);

// Plate ENB: connect RB15 / PIC pin 26 to OC1 PWM.
IEC0CLR = _IEC0_T2IE_MASK;
T2CON = 0;
OC1CON = 0;
TMR2 = 0;
PR2 = PWM_PERIOD_TICKS;          // 1 kHz with the project's 20 MHz PBCLK
OC1R = 0;
OC1RS = 0;
RPB15R = OC_PPS_CODE;
OC1CONbits.OCM = 0b110;

IPC2bits.T2IP = 2;
IPC2bits.T2IS = 0;
IFS0CLR = _IFS0_T2IF_MASK;

pwmTicksLeft = 0;
pwmTimedOut = false;
CurrentDuty = 0;
previousPosition = encoderCount;
offSince = ES_Timer_GetTime();
followState = Pickup;
followReason = "none";

IEC0SET = _IEC0_T2IE_MASK;
OC1CONbits.ON = 1;
T2CONbits.ON = 1;
  return ES_PostToService(MyPriority, init);
}

bool PostActuatorService(ES_Event_t ThisEvent)
{
  return ES_PostToService(MyPriority, ThisEvent);
}

ES_Event_t RunActuatorService(ES_Event_t ThisEvent)
{
    ES_Event_t result = { ES_NO_EVENT, 0 };
    uint16_t now = ES_Timer_GetTime();

    if (pwmTimedOut && followState != PlateFault) {
        failPlate("control timeout", now);
    }

    if (ThisEvent.EventType == ES_INIT) {
        if (INT1R != 2u || RPB15R != OC_PPS_CODE) {
            failPlate("pin mapping", now);
        }

        if (ES_Timer_InitTimer(PLATE_SAMPLE_TIMER, PLATE_UPDATE_MS) != ES_Timer_OK) {
            failPlate("control timer", now);
        }
    }
    else if (ThisEvent.EventType == ES_TIMEOUT &&
             ThisEvent.EventParam == PLATE_SAMPLE_TIMER) {
        if (ES_Timer_InitTimer(PLATE_SAMPLE_TIMER, PLATE_UPDATE_MS) != ES_Timer_OK) {
            failPlate("control timer", now);
        } else {
            updatePlate(now);
        }
    }
    else if (ThisEvent.EventType == ES_CHAIN_SET_MOTOR_DIR &&
             followState == AwaitDirection &&
             ThisEvent.EventParam == directionRequest) {
        if ((uint16_t)(now - requestSince) >= 150u) {
            failPlate("late 595 reply", now);
        } else {
            followState = Driving;
        }
    }
    else if (ThisEvent.EventType == ES_NEW_KEY) {
        switch (ThisEvent.EventParam) {
            case 'x':
                if (followState != PlateFault) {
                    plateOff(Paused, now);
                }
                break;

            case 'a':
                if (followState == Paused || followState == PlateFault) {
                    plateOff(Pickup, now);
                    pwmTimedOut = false;
                    followReason = "none";

                    if (INT1R != 2u || RPB15R != OC_PPS_CODE) {
                        failPlate("pin mapping", now);
                    }
                    if (ES_Timer_InitTimer(PLATE_SAMPLE_TIMER, PLATE_UPDATE_MS)
                        != ES_Timer_OK) {
                        failPlate("control timer", now);
                    }
                }
                break;

            case 'v': {
                uint32_t pins = PORTB;
                unsigned a = (unsigned)(pins & 1u);
                unsigned b = (unsigned)((pins >> 1) & 1u);
                int32_t count = encoderCount;
                uint16_t pot = GetPotReading();
                unsigned targetDeg = scaledPot(pot, 360u);
                int plateDeg = (int)(((int64_t)count * 360) / COUNTS_PER_CLOCKWISE_REV);

                DB_printf("A%u B%u count=%d pot=%u target=%u deg plate=%d deg\n",
                          a, b, (int)count, (unsigned)pot, targetDeg, plateDeg);
                DB_printf("mode=%s duty=%u reason=%s\n",
                          followNames[followState],
                          (unsigned)(pwmTimedOut ? 0u : CurrentDuty),
                          followReason);
                break;
            }

            default:
                break;
        }
    }

    return result;
}

/***************************************************************************
 private functions
 ***************************************************************************/
// Speed changes in the same direction happen at once. A direction change
// stops the plate first, sends the new direction, and applies the speed on
// the next PLATE_SAMPLE_TIMER tick.
static void drivePlate(MotorDirection_t Dir, uint8_t Duty)
{
  if (DirStop == Dir)
  {
    Duty = 0;
  }
  if (Dir == CurrentDirection)
  {
    if (!DirectionSettling)
    {
      setPlateDuty(Duty);
    }
    else
    {
      PendingDuty = Duty;
    }
    return;
  }

  setPlateDuty(0);                      // bridge off before IN3/IN4 change
  setDirection(Dir);
  CurrentDirection = Dir;
  PendingDuty = Duty;
  if (Duty > 0)
  {
    DirectionSettling = true;
    ES_Timer_InitTimer(PLATE_SAMPLE_TIMER, PLATE_UPDATE_MS);
  }
}

static void stopPlate(void)
{
  PlateFollowsPot = false;
  ES_Timer_StopTimer(PLATE_SAMPLE_TIMER);
  DirectionSettling = false;
  setPlateDuty(0);
  setDirection(DirStop);
  CurrentDirection = DirStop;
}

static void setPlateDuty(uint8_t duty)
{
    IEC0CLR = _IEC0_T2IE_MASK;

    if (pwmTimedOut) {
        duty = 0;
    }

    OC1RS = DUTY_TO_OCRS(duty);
    CurrentDuty = duty;
    pwmTicksLeft = duty != 0u ? PWM_TIMEOUT_TICKS : 0u;

    IEC0SET = _IEC0_T2IE_MASK;
}

// motor: on at DISPENSE_DUTY or off. Servo: drop position or rest position.
static void runDispenser(bool On)
{
#if DISPENSER_IS_SERVO
  OC2RS = US_TO_SERVO_TICKS(On ? DISPENSER_DROP_US : DISPENSER_REST_US);
#else
  OC2RS = DUTY_TO_OCRS(On ? DISPENSE_DUTY : 0);
#endif
}

// IN3 / IN4 are on the 595 chain, which DisplayChainService owns
static void setDirection(MotorDirection_t Dir)
{
  ES_Event_t DirEvent;
  DirEvent.EventType = ES_CHAIN_SET_MOTOR_DIR;
  DirEvent.EventParam = (uint16_t)Dir;
  PostDisplayChainService(DirEvent);
}

static void setBell(bool On)
{
  ES_Event_t BellEvent;
  BellEvent.EventType = ES_CHAIN_SET_BELL;
  BellEvent.EventParam = On ? 1 : 0;
  PostDisplayChainService(BellEvent);
}
/*------------------------------ End of file ------------------------------*/
