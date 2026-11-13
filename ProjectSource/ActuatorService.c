/****************************************************************************
 Module
   ActuatorService.c

 Description
   Runs the moving parts on the schematic (ACTUATORS):
     Plate Servo      RB15 (pin 26), PWM from OC1
     Dispenser Servo  RB11 (pin 22), PWM from OC2
     Buzzer           "Bell" output on the first 74HC595 (QA), switched by
                      DisplayChainService when this service asks
   While a game is running (ES_PLATE_START to ES_PLATE_STOP) the plate servo
   follows the potentiometer (Pot Wiper RB12 / AN12, read in AnalogInputs.c).

 Notes
   Owner: Christopher.
   Both servos share Timer2 at 50 Hz (20 ms period). Timer2 runs at
   PBCLK / 8 = 2.5 MHz, so 1 count = 0.4 us.
   The pulse-width limits and the dispenser positions below are guesses:
   tune them on the real servos.

   Test keys (any time): a plate follows the pot, x plate stops,
   d dispense a TREAT, b ding, ? status.

 History
 When           Who          What/Why
 -------------- ---          --------
 11/09/26       ---          stub
 11/12/26       Christopher  keyboard-driven plate motor test (H-bridge)
 11/13/26       ---          integration: plate and dispenser are servos on
                             RB15 / RB11 per the schematic, pot control,
                             dispenser and ding. The H-bridge test used
                             RA0/RA1 (Hall South / West) and RB11/RB15
                             (the servo pins), so it was replaced.
****************************************************************************/
/*----------------------------- Include Files -----------------------------*/
#include "../ProjectHeaders/ActuatorService.h"

#include <xc.h>

#include "ES_Configure.h"
#include "ES_Framework.h"
#include "ES_Port.h"
#include "ES_Timers.h"
#include "terminal.h"
#include "dbprintf.h"
#include "AnalogInputs.h"
#include "DisplayChainService.h"

/*----------------------------- Module Defines ----------------------------*/
#define US_TO_TICKS(us)      ((uint16_t)(((uint32_t)(us) * 5u) / 2u))
#define SERVO_PERIOD_TICKS   49999u       // 20 ms

#define PLATE_MIN_US         1000u        // pulse at one end of the pot
#define PLATE_MAX_US         2000u        // pulse at the other end
#define PLATE_UPDATE_MS      20           // how often the plate follows the pot

#define DISPENSER_REST_US    1000u        // holding the TREATs in
#define DISPENSER_DROP_US    2000u        // letting one TREAT out
#define DISPENSE_TIME_MS     700          // time at the drop position
#define DING_TIME_MS         400          // how long the buzzer sounds

#define OC_PPS_CODE          0b0101       // RPB15R = OC1, RPB11R = OC2

/*---------------------------- Module Functions ---------------------------*/
static void setPlatePulse(uint16_t Us);
static void setDispenserPulse(uint16_t Us);
static uint16_t potToPlateUs(uint16_t Pot);
static void setBell(bool On);

/*---------------------------- Module Variables ---------------------------*/
static uint8_t MyPriority;
static bool PlateFollowsPot = false;
static bool Dispensing = false;
static uint16_t PlateUs = (PLATE_MIN_US + PLATE_MAX_US) / 2;

/*------------------------------ Module Code ------------------------------*/
bool InitActuatorService(uint8_t Priority)
{
  ES_Event_t ThisEvent = { ES_INIT, 0 };

  MyPriority = Priority;
  PlateFollowsPot = false;
  Dispensing = false;

  // RB15 and RB11 are digital outputs driven by OC1 / OC2
  ANSELBbits.ANSB15 = 0;
  TRISBbits.TRISB15 = 0;
  TRISBbits.TRISB11 = 0;
  RPB15R = OC_PPS_CODE;
  RPB11R = OC_PPS_CODE;

  // Timer2: PBCLK / 8, 20 ms period
  T2CON = 0;
  T2CONbits.TCKPS = 0b011;              // 1:8
  TMR2 = 0;
  PR2 = SERVO_PERIOD_TICKS;

  // OC1 and OC2: PWM mode (fault pin off), 16-bit, from Timer2
  OC1CON = 0;
  OC1CONbits.OCTSEL = 0;
  OC1CONbits.OCM = 0b110;
  OC2CON = 0;
  OC2CONbits.OCTSEL = 0;
  OC2CONbits.OCM = 0b110;
  setPlatePulse(PlateUs);
  setDispenserPulse(DISPENSER_REST_US);

  T2CONbits.ON = 1;
  OC1CONbits.ON = 1;
  OC2CONbits.ON = 1;

  return ES_PostToService(MyPriority, ThisEvent);
}

bool PostActuatorService(ES_Event_t ThisEvent)
{
  return ES_PostToService(MyPriority, ThisEvent);
}

ES_Event_t RunActuatorService(ES_Event_t ThisEvent)
{
  ES_Event_t ReturnEvent = { ES_NO_EVENT, 0 };

  // the test keys become the same events GameService sends
  if (ES_NEW_KEY == ThisEvent.EventType)
  {
    switch (ThisEvent.EventParam)
    {
      case 'a': ThisEvent.EventType = ES_PLATE_START;    break;
      case 'x': ThisEvent.EventType = ES_PLATE_STOP;     break;
      case 'd': ThisEvent.EventType = ES_DISPENSE_TREAT; break;
      case 'b': ThisEvent.EventType = ES_PLAY_DING;      break;
      case '?':
        DB_printf("Plate %s, pot %d, pulse %d us, dispenser %s\r\n",
                  PlateFollowsPot ? "follows pot" : "holding",
                  GetPotReading(), PlateUs, Dispensing ? "dropping" : "resting");
        return ReturnEvent;
      default:
        return ReturnEvent;
    }
  }

  switch (ThisEvent.EventType)
  {
    case ES_INIT:
      DB_printf("ActuatorService ready (service %d). Test keys: a x d b ?\r\n", MyPriority);
      break;

    case ES_PLATE_START:
      PlateFollowsPot = true;
      ES_Timer_InitTimer(PLATE_SAMPLE_TIMER, PLATE_UPDATE_MS);
      break;

    case ES_PLATE_STOP:
      PlateFollowsPot = false;            // the servo holds where it is
      ES_Timer_StopTimer(PLATE_SAMPLE_TIMER);
      break;

    case ES_DISPENSE_TREAT:
      if (!Dispensing)
      {
        Dispensing = true;
        setDispenserPulse(DISPENSER_DROP_US);
        ES_Timer_InitTimer(SERVO_TIMER, DISPENSE_TIME_MS);
      }
      break;

    case ES_PLAY_DING:
      setBell(true);
      ES_Timer_InitTimer(DING_TIMER, DING_TIME_MS);
      break;

    case ES_TIMEOUT:
      if ((PLATE_SAMPLE_TIMER == ThisEvent.EventParam) && PlateFollowsPot)
      {
        PlateUs = potToPlateUs(GetPotReading());
        setPlatePulse(PlateUs);
        ES_Timer_InitTimer(PLATE_SAMPLE_TIMER, PLATE_UPDATE_MS);
      }
      else if (SERVO_TIMER == ThisEvent.EventParam)
      {
        setDispenserPulse(DISPENSER_REST_US);
        Dispensing = false;
      }
      else if (DING_TIMER == ThisEvent.EventParam)
      {
        setBell(false);
      }
      break;

    default:
      break;
  }

  return ReturnEvent;
}

/***************************************************************************
 private functions
 ***************************************************************************/
static void setPlatePulse(uint16_t Us)
{
  OC1RS = US_TO_TICKS(Us);
}

static void setDispenserPulse(uint16_t Us)
{
  OC2RS = US_TO_TICKS(Us);
}

// pot 0..1023 -> PLATE_MIN_US..PLATE_MAX_US
static uint16_t potToPlateUs(uint16_t Pot)
{
  if (Pot > 1023u)
  {
    Pot = 1023u;
  }
  return (uint16_t)(PLATE_MIN_US + ((uint32_t)Pot * (PLATE_MAX_US - PLATE_MIN_US)) / 1023u);
}

// the buzzer is on the 595 chain, which DisplayChainService owns
static void setBell(bool On)
{
  ES_Event_t BellEvent;
  BellEvent.EventType = ES_CHAIN_SET_BELL;
  BellEvent.EventParam = On ? 1 : 0;
  PostDisplayChainService(BellEvent);
}
/*------------------------------ End of file ------------------------------*/
