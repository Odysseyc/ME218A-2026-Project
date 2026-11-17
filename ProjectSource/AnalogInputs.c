/****************************************************************************
 Module
   AnalogInputs.c

 Description
   Reads every analog input on the schematic, one channel at a time with the
   ADC: the 4 Hall sensors at the plate LEDs (North, East, South, West), the
   Hall sensor at the food's start spot and the potentiometer.

   Posts ES_FOOD_PASSED to GameService when the magnet under the food is at a
   plate Hall sensor. GameService decides whether that was the lit LED (a
   hit) or not. Posts ES_USER_ACTIVITY when the pot is turned, so turning the
   knob counts as input for the 20 s timeout. Check4FoodSensor uses
   IsFoodAtStart() and ActuatorService uses GetPotReading().

   At power-up it measures each sensor's resting value (baseline). If one
   sensor reads very differently from the other three (the magnet is sitting
   next to it), its baseline is estimated from the other three. If the
   readings are unclear it measures again.

 Notes
   Owner: Jeremy (Hall code, first written in GameService.c on the
   game-logic branch). Moved here during integration so GameService only
   makes game decisions. The pot reading is here because there is one ADC.

   Pins (schematic):  North RB2 / AN4   East RB3 / AN5   South RA0 / AN0
                      West RA1 / AN1    Food Start RB13 / AN11
                      Pot Wiper RB12 / AN12

   Each channel is read every 6 x POLL_MS = 30 ms. Nothing here blocks.
   Food-start sensor: high (1) = magnet on it = food on its spot, low (0) =
   food lifted off. Read with fixed thresholds, so it works for a digital
   Hall switch (0 or 3.3 V) or a linear one whose output rises with the
   magnet. It must read the same way FOOD_START_STABLE_READS times in a row
   (about 90 ms) before it changes, so a wobble doesn't start a game.
   While the magnet stays at a sensor the event is repeated every REPEAT_MS,
   so an LED that lights up right where the food is parked still counts.

 History
 When           Who     What/Why
 -------------- ---     --------
 11/11/26       Jeremy  Hall sampling, calibration, hit detection
 11/13/26       ---     moved out of GameService into its own module
 11/13/26       ---     added food-start Hall and pot (schematic pins)
 11/13/26       ---     ADC clock and acquisition time from Christopher's
                        working pot code
****************************************************************************/
/*----------------------------- Include Files -----------------------------*/
#include "../ProjectHeaders/AnalogInputs.h"

#include <xc.h>
#include <stdlib.h>

#include "ES_Configure.h"
#include "ES_Framework.h"
#include "ES_Timers.h"
#include "dbprintf.h"
#include "GameService.h"

/*----------------------------- Module Defines ----------------------------*/
#define NUM_HALLS          4      // plate sensors, channels 0 to 3
#define CH_FOOD_START      4
#define CH_POT             5
#define NUM_CHANNELS       6
#define POLL_MS            5      // time between ADC readings
#define CAL_SCANS          50u    // readings per sensor for the baseline
#define DETECT_COUNTS      40     // change from baseline that means "magnet here"
#define RELEASE_COUNTS     20     // change that means "magnet gone" (hysteresis)
#define CAL_GROUP_COUNTS   15u    // max spread among sensors considered similar
#define CAL_OUTLIER_GAP    40u    // min gap from the outlier to its nearest neighbor
#define CAL_WARNING_MS     1000u  // how often to repeat the "unclear" message
#define REPEAT_MS          200u   // repeat ES_FOOD_PASSED while the magnet stays
#define FOOD_ON_COUNTS     700    // food-start reading above this = food on its spot
#define FOOD_OFF_COUNTS    600    // below this = food lifted off (gap = hysteresis)
#define FOOD_START_STABLE_READS 3
#define POT_MOVE_COUNTS    25     // pot change that counts as the player turning it

enum { CalClear = -1, CalAmbiguous = -2 };

/*---------------------------- Module Functions ---------------------------*/
static void startSample(void);
static void finishCalibration(void);
static int findStartupOutlier(const unsigned Mean[NUM_HALLS]);

/*---------------------------- Module Variables ---------------------------*/
// ADC channel (ANx) for N, E, S, W, food start, pot
static const uint8_t AdcChannel[NUM_CHANNELS] = {4, 5, 0, 1, 11, 12};

static uint16_t Baseline[NUM_HALLS];
static uint32_t HallSum[NUM_HALLS];
static uint16_t SamplesLeft;            // > 0 while calibrating
static bool HaveBaselines = false;
static bool CalRetried = false;
static uint16_t LastCalWarning;

static uint8_t Channel;                 // channel being read now (0 to 5)
static uint8_t ActiveMask;              // bit n set = magnet at plate sensor n
static bool FoodAtStart = true;         // the game assumes the food starts on its spot
static uint8_t FoodChangeReads = 0;
static uint16_t PotReading = 512;
static uint16_t PotReported = 512;
static bool PotSeen = false;
static bool HavePlatePotSample = false;
static uint16_t LastPotSampleMs;
static uint16_t LastPollTime;
static uint16_t LastPostTime[NUM_HALLS];

/*------------------------------ Module Code ------------------------------*/
void InitAnalogInputs(void)
{
  // North, East (RB2, RB3), food start (RB13), pot (RB12) as analog inputs
  TRISBSET  = (1u << 2) | (1u << 3) | (1u << 12) | (1u << 13);
  ANSELBSET = (1u << 2) | (1u << 3) | (1u << 12) | (1u << 13);
  // South and West: RA0 / RA1 as analog inputs
  TRISASET  = (1u << 0) | (1u << 1);
  ANSELASET = (1u << 0) | (1u << 1);

  // ADC timing from Christopher's working pot test (actuators branch).
  // Unlike his test, sampling is started by hand (no ASAM) because this
  // module switches between 6 channels.
  PMD1bits.AD1MD = 0;       // make sure the ADC module is powered
  AD1CON1 = 0;
  AD1CON2 = 0;              // AVDD / AVSS references, one channel, buffer 0
  AD1CON3 = 0;
  AD1CHS  = 0;
  AD1CSSL = 0;
  IEC0bits.AD1IE = 0;       // no ADC interrupt
  IFS0bits.AD1IF = 0;
  AD1CON3bits.ADCS = 19;    // 2 us ADC clock from the 20 MHz peripheral clock
  AD1CON3bits.SAMC = 10;    // 20 us acquisition time
  AD1CON1bits.SSRC = 7;     // convert automatically after sampling
  AD1CON1bits.ON = 1;

  for (uint8_t i = 0; i < NUM_HALLS; i++)
  {
    HallSum[i] = 0;
  }
  ActiveMask = 0;
  Channel = 0;
  FoodAtStart = true;
  CalRetried = false;
  SamplesLeft = NUM_HALLS * CAL_SCANS;
  LastPollTime = 0;
  HavePlatePotSample = false;
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

  if (CH_POT == Channel)
  {
    PotReading = Reading;
    LastPotSampleMs = ES_Timer_GetTime();
    HavePlatePotSample = true;  
    if (!PotSeen)
    {
      PotSeen = true;
      PotReported = Reading;
    }
    else if (abs((int)Reading - (int)PotReported) >= POT_MOVE_COUNTS)
    {
      PotReported = Reading;
      ES_Event_t ThisEvent;
      ThisEvent.EventType = ES_USER_ACTIVITY;
      ThisEvent.EventParam = 0;
      PostGameService(ThisEvent);
      ReturnVal = true;
    }
  }
  else if (CH_FOOD_START == Channel)
  {
    bool ReadsOn  = (Reading >= FOOD_ON_COUNTS);
    bool ReadsOff = (Reading <= FOOD_OFF_COUNTS);
    if ((ReadsOn && !FoodAtStart) || (ReadsOff && FoodAtStart))
    {
      if (++FoodChangeReads >= FOOD_START_STABLE_READS)
      {
        FoodAtStart = !FoodAtStart;
        FoodChangeReads = 0;
      }
    }
    else
    {
      FoodChangeReads = 0;
    }
  }
  else if (SamplesLeft > 0)
  {
    HallSum[Channel] += Reading;
    SamplesLeft--;
    if (0 == SamplesLeft)
    {
      finishCalibration();
    }
  }
  else
  {
    uint8_t Bit = (uint8_t)(1u << Channel);
    int Change = abs((int)Reading - (int)Baseline[Channel]);

    bool Arrived = (Change >= DETECT_COUNTS) && !(ActiveMask & Bit);
    bool StillHere = (ActiveMask & Bit) && (Change > RELEASE_COUNTS) &&
                     ((uint16_t)(Now - LastPostTime[Channel]) >= REPEAT_MS);

    if (Arrived || StillHere)
    {
      ActiveMask |= Bit;
      LastPostTime[Channel] = Now;
      ES_Event_t ThisEvent;
      ThisEvent.EventType = ES_FOOD_PASSED;
      ThisEvent.EventParam = Channel;
      PostGameService(ThisEvent);
      ReturnVal = true;
    }
    else if (Change <= RELEASE_COUNTS)
    {
      ActiveMask &= (uint8_t)~Bit;
    }
  }

  Channel = (uint8_t)((Channel + 1u) % NUM_CHANNELS);
  startSample();
  return ReturnVal;
}

bool IsFoodAtStart(void)
{
  return FoodAtStart;
}

uint16_t GetPotReading(void)
{
  return PotReading;
}

bool TryGetPotReading(uint16_t *value)
{
    uint16_t ageMs = (uint16_t)(ES_Timer_GetTime() - LastPotSampleMs);

    if (!HavePlatePotSample || ageMs >= 150u) {
        return false;
    }

    *value = PotReading;
    return true;
}

/***************************************************************************
 private functions
 ***************************************************************************/
static void startSample(void)
{
  AD1CHSbits.CH0SA = AdcChannel[Channel];
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
