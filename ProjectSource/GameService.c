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
#define CAL_SCANS           8
#define DETECT_COUNTS      40
#define RELEASE_COUNTS     20
#define RELIGHT_ON_RELEASE  1

// Order throughout the test is North, East, South, West.
// Hall inputs are RB2/AN4, RB3/AN5, RA0/AN0, RA1/AN1.
static const uint8_t hallChannel[4] = {4, 5, 0, 1};

static uint16_t baseline[4];
static uint8_t direction;
static uint8_t samplesLeft;
static uint8_t ledMask;

/*---------------------------- Helper Functions  ---------------------------*/
static void Pulse595(uint32_t pin)
{
  // Give the 595 time to see the data and clock.
  for (volatile unsigned i = 0; i < 4; i++) { }
  LATBSET = pin;
  for (volatile unsigned i = 0; i < 4; i++) { }
  LATBCLR = pin;
}

static void Write595(uint8_t value)
{
  for (uint8_t bit = 0x80; bit != 0; bit >>= 1) {
    if (value & bit) {
      LATBSET = SR_DATA;
    } else {
      LATBCLR = SR_DATA;
    }
    Pulse595(SR_CLOCK);
  }
  Pulse595(SR_LATCH);
}

static void StartHallSample(void)
{
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

  ledMask = 0x1E;           // QB, QC, QD, QE all on
  Write595(ledMask);

  for (unsigned i = 0; i < 4; i++) {
    baseline[i] = 0;
  }
  direction = 0;
  samplesLeft = 4 * CAL_SCANS;
  StartHallSample();
}

static void PollOneHall(void)
{
  if (!AD1CON1bits.DONE) {
    return;
  }
  uint16_t reading = (uint16_t)ADC1BUF0;

  if (samplesLeft > 0) {
    // At startup, average eight magnet-away readings for each Hall.
    baseline[direction] += reading;
    samplesLeft--;

    if (samplesLeft == 0) {
      for (unsigned i = 0; i < 4; i++) {
        baseline[i] /= CAL_SCANS;
      }
      DB_printf("Hall baselines N/E/S/W: %d %d %d %d\r\n",
                baseline[0], baseline[1], baseline[2], baseline[3]);
    }
  } else {
    // Either magnet pole can trigger the sensor.
    int change = abs((int)reading - (int)baseline[direction]);

    // North = QB (bit 1), East = QC, South = QD, West = QE.
    uint8_t bit = (uint8_t)(1u << (direction + 1));
    uint8_t previous = ledMask;

    if (change >= DETECT_COUNTS) {
      ledMask &= (uint8_t)~bit;       // Turn this LED off
    } else if (RELIGHT_ON_RELEASE && change <= RELEASE_COUNTS) {
      ledMask |= bit;                 // Turn this LED back on
    }

    if (ledMask != previous) {
      Write595(ledMask);
    }
  }

  direction = (direction + 1u) % 4u;
  StartHallSample();
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
        DB_printf("GameService ready (service %d)\r\n", MyPriority);
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
      else if (ThisEvent.EventType == ES_TIMEOUT &&
               ThisEvent.EventParam == GAME_CLOCK_TIMER) {
        PollOneHall();
        ES_Timer_InitTimer(GAME_CLOCK_TIMER, POLL_MS);
      }
    }
       
    break;


    case Won:
    case Lost:
    {
      // ES_TIMEOUT (result timer) -> Welcome
    }
    break;
    default:
    {}
    break;
  }

  return ReturnEvent;
}
