/****************************************************************************
 Module
   HallSensors.c

 Description
   Reads the 4 Hall sensors at the plate LEDs (North, East, South, West) one
   at a time with the ADC, and posts ES_FOOD_PASSED to GameService when the
   magnet under the food arrives at one of them. GameService decides whether
   that was the lit LED (a hit) or not.

   At power-up it measures each sensor's resting value (baseline). If one
   sensor reads very differently from the other three (the magnet is sitting
   next to it), its baseline is estimated from the other three. If the
   readings are unclear it measures again.

 Notes
   Owner: Jeremy (hall / plate LED code, first written in GameService.c on
   the game-logic branch). Moved here during integration so GameService only
   makes game decisions.

   Pins:  North RB2 / AN4   East RB3 / AN5   South RA0 / AN0   West RA1 / AN1
   The terminal uses RB6 / RB7, so RB2 / RB3 are free for analog input.

   Each sensor is read every 4 x POLL_MS = 20 ms. Nothing here blocks.
   While the magnet stays at a sensor the event is repeated every REPEAT_MS,
   so an LED that lights up right where the food is parked still counts.

 History
 When           Who     What/Why
 -------------- ---     --------
 11/11/26       Jeremy  Hall sampling, calibration, hit detection
 11/13/26       ---     moved out of GameService into its own module
****************************************************************************/
/*----------------------------- Include Files -----------------------------*/
#include "../ProjectHeaders/HallSensors.h"

#include <xc.h>
#include <stdlib.h>

#include "ES_Configure.h"
#include "ES_Framework.h"
#include "ES_Timers.h"
#include "dbprintf.h"
#include "GameService.h"

/*----------------------------- Module Defines ----------------------------*/
#define NUM_HALLS          4
#define POLL_MS            5      // time between ADC readings
#define CAL_SCANS          50u    // readings per sensor for the baseline
#define DETECT_COUNTS      40     // change from baseline that means "magnet here"
#define RELEASE_COUNTS     20     // change that means "magnet gone" (hysteresis)
#define CAL_GROUP_COUNTS   15u    // max spread among sensors considered similar
#define CAL_OUTLIER_GAP    40u    // min gap from the outlier to its nearest neighbor
#define CAL_WARNING_MS     1000u  // how often to repeat the "unclear" message
#define REPEAT_MS          200u   // repeat ES_FOOD_PASSED while the magnet stays

enum { CalClear = -1, CalAmbiguous = -2 };

/*---------------------------- Module Functions ---------------------------*/
static void startSample(void);
static void finishCalibration(void);
static int findStartupOutlier(const unsigned Mean[NUM_HALLS]);

/*---------------------------- Module Variables ---------------------------*/
// ADC channel for N, E, S, W
static const uint8_t HallChannel[NUM_HALLS] = {4, 5, 0, 1};

static uint16_t Baseline[NUM_HALLS];
static uint32_t HallSum[NUM_HALLS];
static uint16_t SamplesLeft;            // > 0 while calibrating
static bool HaveBaselines = false;
static bool CalRetried = false;
static uint16_t LastCalWarning;

static uint8_t Direction;               // sensor being read now (0 to 3)
static uint8_t ActiveMask;              // bit n set = magnet at sensor n
static uint16_t LastPollTime;
static uint16_t LastPostTime[NUM_HALLS];

/*------------------------------ Module Code ------------------------------*/
void InitHallSensors(void)
{
  // North and East: RB2 / RB3 as analog inputs
  TRISBSET  = (1u << 2) | (1u << 3);
  ANSELBSET = (1u << 2) | (1u << 3);
  // South and West: RA0 / RA1 as analog inputs
  TRISASET  = (1u << 0) | (1u << 1);
  ANSELASET = (1u << 0) | (1u << 1);

  AD1CON1 = 0;
  AD1CON2 = 0;
  AD1CON3 = 0;
  AD1CHS  = 0;
  AD1CSSL = 0;
  AD1CON3bits.ADRC = 1;     // ADC's internal clock
  AD1CON3bits.SAMC = 16;    // sample time for each Hall
  AD1CON1bits.SSRC = 7;     // convert automatically after sampling
  AD1CON1bits.ON = 1;

  for (uint8_t i = 0; i < NUM_HALLS; i++)
  {
    HallSum[i] = 0;
  }
  ActiveMask = 0;
  Direction = 0;
  CalRetried = false;
  SamplesLeft = NUM_HALLS * CAL_SCANS;
  LastPollTime = 0;
  startSample();
}

bool Check4PlateHalls(void)
{
  bool ReturnVal = false;
  uint16_t Now = ES_Timer_GetTime();

  if ((uint16_t)(Now - LastPollTime) < POLL_MS)
  {
    return false;
  }
  if (!AD1CON1bits.DONE)
  {
    return false;                       // conversion still running
  }
  LastPollTime = Now;

  uint16_t Reading = (uint16_t)ADC1BUF0;

  if (SamplesLeft > 0)
  {
    HallSum[Direction] += Reading;
    SamplesLeft--;
    if (0 == SamplesLeft)
    {
      finishCalibration();
    }
  }
  else
  {
    uint8_t Bit = (uint8_t)(1u << Direction);
    int Change = abs((int)Reading - (int)Baseline[Direction]);

    bool Arrived = (Change >= DETECT_COUNTS) && !(ActiveMask & Bit);
    bool StillHere = (ActiveMask & Bit) && (Change > RELEASE_COUNTS) &&
                     ((uint16_t)(Now - LastPostTime[Direction]) >= REPEAT_MS);

    if (Arrived || StillHere)
    {
      ActiveMask |= Bit;
      LastPostTime[Direction] = Now;
      ES_Event_t ThisEvent;
      ThisEvent.EventType = ES_FOOD_PASSED;
      ThisEvent.EventParam = Direction;
      PostGameService(ThisEvent);
      ReturnVal = true;
    }
    else if (Change <= RELEASE_COUNTS)
    {
      ActiveMask &= (uint8_t)~Bit;
    }
  }

  Direction = (uint8_t)((Direction + 1u) % NUM_HALLS);
  startSample();
  return ReturnVal;
}

/***************************************************************************
 private functions
 ***************************************************************************/
static void startSample(void)
{
  AD1CHSbits.CH0SA = HallChannel[Direction];
  AD1CON1bits.SAMP = 1;
}

static void finishCalibration(void)
{
  static const char *const EstimateName[NUM_HALLS] =
  {
    "estimated N", "estimated E", "estimated S", "estimated W"
  };
  unsigned Mean[NUM_HALLS];
  unsigned Total = 0;

  for (uint8_t i = 0; i < NUM_HALLS; i++)
  {
    Mean[i] = (unsigned)(HallSum[i] / CAL_SCANS);
    Total += Mean[i];
    HallSum[i] = 0;
  }

  int Outlier = findStartupOutlier(Mean);

  // unclear readings: measure again (once if we already have baselines,
  // until clear if we don't)
  if ((CalAmbiguous == Outlier) && (!CalRetried || !HaveBaselines))
  {
    uint16_t Now = ES_Timer_GetTime();
    if (!CalRetried || ((uint16_t)(Now - LastCalWarning) >= CAL_WARNING_MS))
    {
      LastCalWarning = Now;
      DB_printf("Hall readings unclear N/E/S/W %d %d %d %d, measuring again. Move the magnet if needed.\r\n",
                Mean[0], Mean[1], Mean[2], Mean[3]);
    }
    CalRetried = true;
    SamplesLeft = NUM_HALLS * CAL_SCANS;
    return;
  }

  const char *Source = "previous";
  if (CalAmbiguous != Outlier)
  {
    for (uint8_t i = 0; i < NUM_HALLS; i++)
    {
      Baseline[i] = (uint16_t)Mean[i];
    }
    Source = "all measured";
    if (Outlier >= 0)
    {
      // average the other three, rounded to the nearest count
      Baseline[Outlier] = (uint16_t)((Total - Mean[Outlier] + 1u) / 3u);
      Source = EstimateName[Outlier];
    }
    HaveBaselines = true;
  }

  DB_printf("Hall baselines N/E/S/W %d %d %d %d (%s)\r\n",
            Baseline[0], Baseline[1], Baseline[2], Baseline[3], Source);
}

// Returns CalClear if all four agree, the index of a single sensor that
// stands apart from the other three, or CalAmbiguous.
static int findStartupOutlier(const unsigned Mean[NUM_HALLS])
{
  uint8_t Order[NUM_HALLS] = {HALL_NORTH, HALL_EAST, HALL_SOUTH, HALL_WEST};

  // sort the indices by reading (four values need only six comparisons)
  for (uint8_t i = 0; i < 3u; i++)
  {
    for (uint8_t j = (uint8_t)(i + 1u); j < 4u; j++)
    {
      if (Mean[Order[j]] < Mean[Order[i]])
      {
        uint8_t Temp = Order[i];
        Order[i] = Order[j];
        Order[j] = Temp;
      }
    }
  }

  unsigned Low     = Mean[Order[0]];
  unsigned MidLow  = Mean[Order[1]];
  unsigned MidHigh = Mean[Order[2]];
  unsigned High    = Mean[Order[3]];

  if ((High - Low) <= CAL_GROUP_COUNTS)
  {
    return CalClear;
  }
  // the bottom three agree and the highest stands apart
  if (((MidHigh - Low) <= CAL_GROUP_COUNTS) && ((High - MidHigh) >= CAL_OUTLIER_GAP))
  {
    return Order[3];
  }
  // the top three agree and the lowest stands apart
  if (((High - MidLow) <= CAL_GROUP_COUNTS) && ((MidLow - Low) >= CAL_OUTLIER_GAP))
  {
    return Order[0];
  }
  return CalAmbiguous;
}
/*------------------------------ End of file ------------------------------*/
