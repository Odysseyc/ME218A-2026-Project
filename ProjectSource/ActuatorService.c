/****************************************************************************
 Module
   ActuatorService.c

 Description
   Makes the plate follow the potentiometer's target position using its
   encoder. Motor direction is sent through DisplayChainService.

   Plate motor (L298N channel B):
     ENB       RB15 / pin 26, OC1 PWM
     IN3/IN4   second 74HC595 QF/QG
     Encoder A RB0 / pin 4; encoder B RB1 / pin 5
     Pot       RB12 / pin 23, read by AnalogInputs
   Dispenser (L298N channel A): ENA RB11 / pin 22, OC2 PWM;
     IN1 = 3.3 V, IN2 = GND. Runs for 10 seconds after a win.

 Notes
   Follows the knob only during a game; aligns without moving at each start.
   Keys: x stops both motors, a resumes the plate within a game, v status.
   Timer2 supplies 1 kHz PWM; the position controller runs every 20 ms.
   Waits for the encoder to stop changing before starting or reversing.
   GameService sends ES_PLATE_START/STOP and ES_DISPENSE_TREAT on a win.

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

#define PLATE_UPDATE_MS      20           // how often the plate follows the pot
#define COUNTS_PER_CLOCKWISE_REV 681
#define FOLLOW_POT_MIN          0
#define FOLLOW_POT_MAX          1023
#define FORWARD_COUNT_SIGN     -1     // Raw count sign when IN3 is high, IN4 low

#define FOLLOW_MIN_DUTY       45u   // Running duty near the target
#define FOLLOW_RUN_MAX_DUTY   50u   // Running duty far from the target
#define FOLLOW_MAX_DUTY       60u   // Startup ceiling until movement is detected
#define FOLLOW_DUTY_STEP  16u   // Rise by up to 16 percentage points per 20 ms

#define STOP_BAND_COUNTS        4     // About 2 degrees
#define RESTART_BAND_COUNTS     6     // About 3 degrees
#define COAST_MS                60u
#define NO_PROGRESS_MS          300u
#define MAX_DRIVE_MS            3000u
#define PWM_TIMEOUT_TICKS       100u  // About 100 ms at 1 kHz

#define DISPENSER_DUTY          75u
#define DISPENSER_RUN_TICKS     10000u // 10 seconds on the 1 kHz Timer2

#define OC_PPS_CODE          0b0101       // RPB15R = OC1; RPB11R = OC2

typedef enum { DirStop = 0, DirForward = 1, DirReverse = 2 } MotorDirection_t;

typedef enum {
    Pickup, Holding, Coasting, AwaitDirection, Driving, Paused, PlateFault
} FollowState;

/*---------------------------- Module Functions ---------------------------*/
static void updatePlate(uint16_t now);
static void setPlateDuty(uint8_t duty);
static void setDispenser(bool running);
static void plateOff(FollowState next, uint16_t now);
static void failPlate(const char *reason, uint16_t now);
static unsigned scaledPot(uint16_t pot, unsigned fullScale);
static int32_t targetFromPot(uint16_t pot);
static void requestDirection(int sign, uint16_t now);

/*---------------------------- Module Variables ---------------------------*/
static uint8_t MyPriority;
static volatile int32_t encoderCount;
static uint8_t CurrentDuty = 0;

static FollowState followState = Paused;
static bool gameActive = false;
static bool referenceAligned = false;
static bool dispenserReady = false;
static volatile uint16_t dispenserTicksLeft;

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

/*------------------------------ Module Code ------------------------------*/
static void updatePlate(uint16_t now)
{
    int32_t position = encoderCount;
    int64_t moved = (int64_t)position - previousPosition;
    previousPosition = position;
    if (CurrentDuty == 0u && moved != 0) {
        offSince = now;
    }

    if (!gameActive || followState == Paused || followState == PlateFault) {
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

    // Align once per game without moving. Resume keeps the same reference.
    if (followState == Pickup) {
        if (!referenceAligned) {
            int32_t startPosition = targetFromPot(pot);
            uint32_t encoderEnabled = IEC0 & _IEC0_INT1IE_MASK;

            IEC0CLR = _IEC0_INT1IE_MASK;
            encoderCount = startPosition;
            previousPosition = startPosition;
            progressPosition = startPosition;
            IEC0SET = encoderEnabled;

            referenceAligned = true;
            offSince = now;
            plateOff(Holding, now);
        } else if (distance <= RESTART_BAND_COUNTS) {
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

    // Wait for a quiet encoder window before starting or reversing.
    if (followState != Driving || sign != driveSign ||
        (CurrentDuty == 0u && (uint16_t)(now - offSince) < COAST_MS)) {
        plateOff(Coasting, now);

        if ((uint16_t)(now - offSince) >= COAST_MS) {
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

        if ((uint16_t)(now - progressSince) >= NO_PROGRESS_MS) {
            failPlate("no encoder progress", now);
            return;
        }
        if ((uint16_t)(now - driveSince) >= MAX_DRIVE_MS) {
            failPlate("drive timeout", now);
            return;
        }
    }

    // Request less PWM near the target and ramp up gradually.
    unsigned scale = distance < 90 ? (unsigned)distance : 90u;
    unsigned duty = FOLLOW_MIN_DUTY +
                scale * (FOLLOW_RUN_MAX_DUTY - FOLLOW_MIN_DUTY) / 90u;
    // Ramp toward the maximum until the encoder confirms movement.
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
    if (dispenserTicksLeft != 0u && --dispenserTicksLeft == 0u) {
        OC2RS = 0;
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
    uint8_t duty = CurrentDuty;
    plateOff(PlateFault, now);
    DB_printf("Plate fault: %s; count=%d target=%d duty=%u progress_ms=%u drive_ms=%u\n",
              reason, (int)encoderCount, (int)targetFromPot(GetPotReading()),
              (unsigned)duty, (unsigned)(uint16_t)(now - progressSince),
              (unsigned)(uint16_t)(now - driveSince));
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
    // ENB (RB15 / pin 26) uses OC1; ENA (RB11 / pin 22) uses OC2.
    IEC0CLR = _IEC0_T2IE_MASK;
    T2CON = 0;
    OC1CON = 0;
    OC2CON = 0;
    TMR2 = 0;
    PR2 = PWM_PERIOD_TICKS;          // 1 kHz with the project's 20 MHz PBCLK
    OC1R = 0;
    OC1RS = 0;
    RPB15R = OC_PPS_CODE;
    OC1CONbits.OCM = 0b110;
    OC2R = 0;
    OC2RS = 0;
    RPB11R = OC_PPS_CODE;
    OC2CONbits.OCM = 0b110;

    IPC2bits.T2IP = 2;
    IPC2bits.T2IS = 0;
    IFS0CLR = _IFS0_T2IF_MASK;

    pwmTicksLeft = 0;
    pwmTimedOut = false;
    CurrentDuty = 0;
    previousPosition = encoderCount;
    offSince = ES_Timer_GetTime();
    followState = Paused;
    gameActive = false;
    referenceAligned = false;
    dispenserReady = false;
    dispenserTicksLeft = 0;
    followReason = "none";

    IEC0SET = _IEC0_T2IE_MASK;
    OC1CONbits.ON = 1;
    OC2CONbits.ON = 1;
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
    else if (ThisEvent.EventType == ES_PLATE_START) {
        if (!gameActive) {
            setDispenser(false);
            dispenserReady = true;
            gameActive = true;
            referenceAligned = false;
            if (followState != PlateFault) {
                plateOff(Pickup, now);
            }
        }
    }
    else if (ThisEvent.EventType == ES_PLATE_STOP) {
        gameActive = false;
        plateOff(followState == PlateFault ? PlateFault : Paused, now);
    }
    else if (ThisEvent.EventType == ES_DISPENSE_TREAT) {
        // GameService stops the plate first. Dispense only once per game.
        if (!gameActive && dispenserReady) {
            dispenserReady = false;
            setDispenser(true);
            DB_printf("Dispenser: start 10 s at %u%%; ENA map=%u OC2=0x%x PWM=%u ticks=%u\n",
                      (unsigned)DISPENSER_DUTY, (unsigned)RPB11R,
                      (unsigned)OC2CON, (unsigned)OC2RS,
                      (unsigned)dispenserTicksLeft);
        } else {
            DB_printf("Dispenser: request ignored; game=%u ready=%u\n",
                      (unsigned)gameActive, (unsigned)dispenserReady);
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
    else if (ThisEvent.EventType == ES_CHAIN_SET_MOTOR_DIR && gameActive &&
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
                setDispenser(false);
                if (!gameActive) {
                    dispenserReady = false;
                }
                if (followState != PlateFault) {
                    plateOff(Paused, now);
                }
                break;

            case 'a':
                if (gameActive && (followState == Paused || followState == PlateFault)) {
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
                DB_printf("mode=%s duty=%u reason=%s game=%s dispenser=%s\n",
                          followNames[followState],
                          (unsigned)(pwmTimedOut ? 0u : CurrentDuty),
                          followReason, gameActive ? "on" : "off",
                          dispenserTicksLeft != 0u ? "on" : "off");
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
static void setPlateDuty(uint8_t duty)
{
    IEC0CLR = _IEC0_T2IE_MASK;

    if (!gameActive || pwmTimedOut) {
        duty = 0;
    }

    OC1RS = DUTY_TO_OCRS(duty);
    CurrentDuty = duty;
    pwmTicksLeft = duty != 0u ? PWM_TIMEOUT_TICKS : 0u;

    IEC0SET = _IEC0_T2IE_MASK;
}

static void setDispenser(bool running)
{
    uint32_t timerEnabled = IEC0 & _IEC0_T2IE_MASK;
    IEC0CLR = _IEC0_T2IE_MASK;

    OC2RS = running ? DUTY_TO_OCRS(DISPENSER_DUTY) : 0u;
    dispenserTicksLeft = running ? DISPENSER_RUN_TICKS : 0u;

    IEC0SET = timerEnabled;
}

/*------------------------------ End of file ------------------------------*/
