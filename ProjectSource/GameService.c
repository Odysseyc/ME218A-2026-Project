/****************************************************************************
 Module
   GameService.c

 Description
   Game state machine (Welcome, SelectTime, Cooking, Won, Lost) from the Checkpoint 1 diagram. Owns the inactivity, game-clock and result timers and decides when to send commands to the other services.

 Notes
   STUB created on main so every branch builds against the same service list.
   Owner branch: game-logic. Only edit this file on your own branch.
   Do not edit ES_Configure.h on a feature branch (see the README).

 History
 When           Who     What/Why
 -------------- ---     --------
 11/09/26       ---     stub
****************************************************************************/
/*----------------------------- Include Files -----------------------------*/
#include "../ProjectHeaders/GameService.h"

#include "ES_Configure.h"
#include "ES_Framework.h"
#include "ES_Timers.h"
#include "ES_Port.h"
#include "terminal.h"
#include "dbprintf.h"
#include <xc.h>
#include <stdlib.h>

/*---------------------------- Module Variables ---------------------------*/
static uint8_t MyPriority;

typedef enum { Welcome = 0, SelectTime, Cooking, Won, Lost } GameState_t;
static GameState_t CurrentState;

#define SR_DATA   (1u << 8)    // RB8  -> 595 pin 14
#define SR_CLOCK  (1u << 9)    // RB9  -> 595 pin 11
#define SR_LATCH  (1u << 10)   // RB10 -> 595 pin 12

#define POLL_MS             5
#define CAL_SCANS           50u
#define DETECT_COUNTS      40
#define RELEASE_COUNTS     20
#define RELIGHT_ON_RELEASE  1
#define CAL_GROUP_COUNTS 15u   // Maximum spread among sensors considered similar.
#define CAL_OUTLIER_GAP  40u   // Minimum gap from the outlier to its nearest neighbor.


static uint32_t hallSum[4];
static bool haveHallBaselines;
static bool calRetried;
static uint16_t lastCalWarning;




// Order throughout the test is North, East, South, West.
// Hall inputs are RB2/AN4, RB3/AN5, RA0/AN0, RA1/AN1.
static const uint8_t hallChannel[4] = {4, 5, 0, 1};

static uint16_t baseline[4] = { 0, 0, 0, 0 };  // N, E, S, W;
static uint8_t direction;
static uint8_t samplesLeft;
static uint8_t ledMask; 
static bool won_game;

enum { PlateN = 0, PlateE, PlateS, PlateW };

static const uint8_t benchOrder[] = {
    PlateN, PlateS, PlateE, PlateW, PlateN, PlateS, PlateE, PlateW,
    PlateE, PlateW, PlateN, PlateS, PlateW, PlateS, PlateE, PlateS, PlateW, PlateN
};

typedef struct {
    uint16_t windowMs;
    uint8_t maxTargets;
    const uint8_t *order;
} PlateLevel;

static const PlateLevel easy   = {2000, 25, benchOrder};
static const PlateLevel medium = {1500, 33, benchOrder};
static const PlateLevel hard   = {1000, 50, benchOrder};

static uint8_t selectedDifficulty = 1;  // Bench input until the buttons exist.
static const PlateLevel *roundLevel;

static uint8_t nextTarget;              // n: index of the next request.
static uint8_t hits, hitsToWin, plateScore;
static uint8_t turnOnRequest;           // One bit per plate LED.
static uint8_t hallActiveMask, sampleForTarget;
static uint16_t litSince;
static bool roundDone;

#define SHIFT_IDLE 17u
static uint8_t shiftStep;

/*---------------------------- Helper Functions  ---------------------------*/
static void Step595(void) // 
{
    if (shiftStep == 16u) {
        LATBCLR = SR_CLOCK;
        LATBSET = SR_LATCH;
        litSince = ES_Timer_GetTime();
    } else if ((shiftStep & 1u) == 0) {
        LATBCLR = SR_CLOCK | SR_LATCH;

        uint8_t bit = (uint8_t)(0x80u >> (shiftStep / 2u));
        if (ledMask & bit) {
            LATBSET = SR_DATA;
        } else {
            LATBCLR = SR_DATA;
        }
    } else {
        LATBSET = SR_CLOCK;
    }

    shiftStep++;
}

static void LED_2_Light(uint8_t n) // helper containing order of LEDs, will be difficulty dependant
{
  uint8_t index = n;

  if (n >= 8u) {
    index = (uint8_t)(8u + (n - 8u) % 10u);
  }

  uint8_t target = roundLevel->order[index];
  turnOnRequest = (uint8_t)(1u << (target + 1u));
}

static void FinishRound(void)
{
  won_game = (plateScore >= 100u);
  CurrentState = won_game ? Won : Lost;
  roundDone = true;

  // DB_printf("%s, score %u/100\n",
  //           won_game ? "Won" : "Lost", (unsigned)plateScore);

  ledMask = 0;
  shiftStep = 0;
  ES_Timer_InitTimer(GAME_CLOCK_TIMER, 1u);
}

static void LED_Hall(uint8_t sensedMask) //handles LED and Hall input, checks for hits and misses, and updates the score
{
  if (samplesLeft > 0 || roundDone) {
    return;
  }

  if (ledMask != 0) {
    uint16_t elapsed = (uint16_t)(ES_Timer_GetTime() - litSince);
    bool expired = (elapsed >= roundLevel->windowMs);
    bool hit = !expired && (sensedMask & ledMask) != 0;

  if (!hit && !expired) {
    return;
  }

  if (hit) {
    hits++;
    plateScore = (uint8_t)((100u * hits) / hitsToWin);
  }

  // Check after resolving the target, so the last LED gets its turn.
  if (plateScore >= 100u || nextTarget >= roundLevel->maxTargets) {
    FinishRound();
    return;
}
}

LED_2_Light(nextTarget);
nextTarget++;

ledMask = turnOnRequest;           // Accept the command.
turnOnRequest = 0;                 // Clear the command.
shiftStep = 0;                     // Begin sending the new LED pattern.
}

static void StartHallSample(void)
{
  sampleForTarget = (shiftStep == SHIFT_IDLE) ? nextTarget : 0;
  AD1CHSbits.CH0SA = hallChannel[direction];
  AD1CON1bits.SAMP = 1;
}

static void StartPlateTest(void)
{
  // Set up the three PIC outputs going to the 595.
  CFGCONbits.JTAGEN = 0;
  LATBCLR  = SR_DATA | SR_CLOCK | SR_LATCH;
  TRISBCLR = SR_DATA | SR_CLOCK | SR_LATCH;

  // Set up the four Hall pins as analog inputs.
  TRISBSET  = (1u << 2) | (1u << 3);   // North and East
  ANSELBSET = (1u << 2) | (1u << 3);
  TRISASET  = (1u << 0) | (1u << 1);   // South and West
  ANSELASET = (1u << 0) | (1u << 1);

  AD1CON1 = 0;
  AD1CON2 = 0;
  AD1CON3 = 0;
  AD1CHS  = 0;
  AD1CSSL = 0;
  AD1CON3bits.ADRC = 1;    // Use the ADC's internal clock
  AD1CON3bits.SAMC = 16;   // Allow time to sample each Hall
  AD1CON1bits.SSRC = 7;    // Convert automatically after sampling
  AD1CON1bits.ON = 1;

 uint8_t difficulty = selectedDifficulty;

if (difficulty == 2) {
roundLevel = &medium;
} else if (difficulty == 3) {
roundLevel = &hard;
} else {
roundLevel = &easy;
}

unsigned required = 4u * roundLevel->maxTargets;
if (difficulty == 3) {
required += 4u;                    // Round 80% up for Hard.
}
hitsToWin = (uint8_t)(required / 5u);

nextTarget = 0;
hits = 0;
plateScore = 0;
turnOnRequest = 0;
hallActiveMask = 0;
roundDone = false;
ledMask = 0;
shiftStep = 0;                         // Start with the plate dark.
won_game = false;
direction = 0;

for (unsigned i = 0; i < 4u; i++) {
    hallSum[i] = 0;
}
calRetried = false;
samplesLeft = 4u * CAL_SCANS;
StartHallSample();
}

enum { CalClear = -1, CalAmbiguous = -2 };

static int FindStartupOutlier(const unsigned mean[4])
{
    uint8_t order[4] = { PlateN, PlateE, PlateS, PlateW };

    // Four directions require only six comparisons.
    for (unsigned i = 0; i < 3u; i++) {
        for (unsigned j = i + 1u; j < 4u; j++) {
            if (mean[order[j]] < mean[order[i]]) {
                uint8_t temp = order[i];
                order[i] = order[j];
                order[j] = temp;
            }
        }
    }

    unsigned low     = mean[order[0]];
    unsigned midLow  = mean[order[1]];
    unsigned midHigh = mean[order[2]];
    unsigned high    = mean[order[3]];

    if (high - low <= CAL_GROUP_COUNTS) {
        return CalClear;
    }

    // The bottom three agree and the highest is separated from them.
    if (midHigh - low <= CAL_GROUP_COUNTS &&
        high - midHigh >= CAL_OUTLIER_GAP) {
        return order[3];
    }

    // The top three agree and the lowest is separated from them.
    if (high - midLow <= CAL_GROUP_COUNTS &&
        midLow - low >= CAL_OUTLIER_GAP) {
        return order[0];
    }

    return CalAmbiguous;
}

static void FinishHallCalibration(void)
{
  static const char *const estimateName[4] = {
  "estimated N", "estimated E", "estimated S", "estimated W"
};

unsigned mean[4];
unsigned total = 0;

for (unsigned i = 0; i < 4u; i++) {
  mean[i] = (unsigned)(hallSum[i] / CAL_SCANS);
  total += mean[i];
  hallSum[i] = 0;
}

int outlier = FindStartupOutlier(mean);

if (outlier == CalAmbiguous && (!calRetried || !haveHallBaselines)) {
  uint16_t now = ES_Timer_GetTime();

    if (!calRetried || (uint16_t)(now - lastCalWarning) >= 1000u) {
      lastCalWarning = now;
      // DB_printf("Unclear N/E/S/W %u %u %u %u. Retrying. Move magnet if needed.\n",
      //           mean[0], mean[1], mean[2], mean[3]);
    }

  calRetried = true;
  samplesLeft = 4u * CAL_SCANS;
  return;
}

const char *source = "previous round";

if (outlier != CalAmbiguous) {
  for (unsigned i = 0; i < 4u; i++) {
    baseline[i] = (uint16_t)mean[i];
  }

  source = "all measured";
  if (outlier >= 0) {
    // Subtract the outlier, then average the other three.
    // Adding 1 rounds the result to the nearest ADC count.
    baseline[outlier] = (uint16_t)((total - mean[outlier] + 1u) / 3u);
    source = estimateName[outlier];
  }

  haveHallBaselines = true;
}

// DB_printf("Measured N/E/S/W %u %u %u %u\n", mean[0], mean[1], mean[2], mean[3]);
// DB_printf("Baseline N/E/S/W %u %u %u %u (%s)\n",
//           (unsigned)baseline[0], (unsigned)baseline[1],
//           (unsigned)baseline[2], (unsigned)baseline[3], source);
}

static void PollOneHall(void)
{
    if (!AD1CON1bits.DONE) {
        LED_Hall(0);  // Check the LED timeout while the ADC is busy.
        return;
    }

    uint16_t reading = (uint16_t)ADC1BUF0;
    uint8_t sensedMask = 0;

    if (samplesLeft > 0) {
        hallSum[direction] += reading;
        samplesLeft--;

        if (samplesLeft == 0) {
            FinishHallCalibration();
        }
    } else {
        uint8_t bit = (uint8_t)(1u << (direction + 1u));
        int change = abs((int)reading - (int)baseline[direction]);

        if (change >= DETECT_COUNTS) {
            hallActiveMask |= bit;
        } else if (change <= RELEASE_COUNTS) {
            hallActiveMask &= (uint8_t)~bit;
        }

        if (sampleForTarget == nextTarget) {
            sensedMask = hallActiveMask & bit;
        }
    }

    LED_Hall(sensedMask);

    if (!roundDone) {
        direction = (direction + 1u) % 4u;
        StartHallSample();
    }
}

/*------------------------------ Module Code ------------------------------*/
bool InitGameService(uint8_t Priority)
{
  ES_Event_t ThisEvent;

  MyPriority = Priority;
  CurrentState = Cooking;

  ThisEvent.EventType = ES_INIT;
  return ES_PostToService(MyPriority, ThisEvent);
}

bool PostGameService(ES_Event_t ThisEvent)
{
  return ES_PostToService(MyPriority, ThisEvent);
}

ES_Event_t RunGameService(ES_Event_t ThisEvent)
{
  ES_Event_t ReturnEvent;
  ReturnEvent.EventType = ES_NO_EVENT;

  switch (CurrentState)
  {
    case Welcome:
    {
      if (ES_INIT == ThisEvent.EventType)
      {
        // DB_printf("GameService ready (service %d)\r\n", MyPriority);
      }
      // ES_FOOD_REMOVED -> SelectTime
    }
    break;
    case SelectTime:
    {
      // ES_TIME_SELECTED -> Cooking, ES_TIMEOUT (inactivity) -> Welcome
    }
    break;

    case Cooking:
{
  if (ThisEvent.EventType == ES_INIT) {
    StartPlateTest();
    ES_Timer_InitTimer(GAME_CLOCK_TIMER, POLL_MS);
  }
  else if (ThisEvent.EventType == ES_GAME_CLOCK_ELAPSED) {
    FinishRound();
}
  else if (ThisEvent.EventType == ES_TIMEOUT &&
         ThisEvent.EventParam == GAME_CLOCK_TIMER) {
    if (shiftStep < SHIFT_IDLE) {
        Step595();
    } else if (!roundDone) {
        PollOneHall();
    }

    // Schedule the next shift-register step or Hall reading.
    if (!roundDone) {
        uint16_t delay = (shiftStep < SHIFT_IDLE) ? 1u : POLL_MS;
        ES_Timer_InitTimer(GAME_CLOCK_TIMER, delay);
    }
}
}
break;

    case Won:
    case Lost:
    {
{
    if (ThisEvent.EventType == ES_TIMEOUT &&
        ThisEvent.EventParam == GAME_CLOCK_TIMER &&
        shiftStep < SHIFT_IDLE) {

        Step595();

        if (shiftStep < SHIFT_IDLE) {
            ES_Timer_InitTimer(GAME_CLOCK_TIMER, 1u);
        }
    }
}
break;
      // ES_TIMEOUT (result timer) -> Welcome
    }
    break;
    default:
    {}
    break;
  }

  return ReturnEvent;
}
