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
     TREAT dispenser motor (channel A, one direction only)
       ENA  RB11 (pin 22, the schematic's "Dispenser Servo" pin)  PWM from OC2
       IN1 wired to 3.3 V and IN2 to ground on the board
       ES_DISPENSE_TREAT runs it for DISPENSE_TIME_MS.
     Buzzer  "Bell" on the first 74HC595 QA (DisplayChainService)

 Notes
   Owner: Christopher. Keyboard test and direction-change handling are his.
   Both PWMs come from Timer2 at 1 kHz (PBCLK 20 MHz, 1:1, PR2 = 19999).
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
 11/13/26       ---          integration: ENA/ENB on the schematic's RB11 /
                             RB15 with PWM, IN3/IN4 moved off RA0/RA1
                             (Hall South / West) to spare 595 outputs, pot
                             speed control, dispenser and ding
****************************************************************************/
/*----------------------------- Include Files -----------------------------*/
#include "../ProjectHeaders/ActuatorService.h"

#include <xc.h>
#include <stdlib.h>

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
#define POT_DEADBAND         80           // +/- counts around the centre = stopped
#define PLATE_MIN_DUTY       30           // % just past the dead band (enough to move)
#define PLATE_MAX_DUTY       70           // % at the end of the pot travel
#define PLATE_UPDATE_MS      20           // how often the plate follows the pot
#define TEST_DUTY            50           // % for the j / k test keys

#define DISPENSE_DUTY        60           // %
#define DISPENSE_TIME_MS     1000         // how long the dispenser runs per TREAT
#define DING_TIME_MS         400          // how long the buzzer sounds

#define OC_PPS_CODE          0b0101       // RPB15R = OC1, RPB11R = OC2

typedef enum { DirStop = 0, DirForward = 1, DirReverse = 2 } MotorDirection_t;

/*---------------------------- Module Functions ---------------------------*/
static void drivePlate(MotorDirection_t Dir, uint8_t Duty);
static void setPlateDuty(uint8_t Duty);
static void setDispenserDuty(uint8_t Duty);
static void setDirection(MotorDirection_t Dir);
static void setBell(bool On);
static void stopPlate(void);

/*---------------------------- Module Variables ---------------------------*/
static uint8_t MyPriority;
static bool PlateFollowsPot = false;
static bool Dispensing = false;
static MotorDirection_t CurrentDirection = DirStop;
static uint8_t CurrentDuty = 0;
static uint8_t PendingDuty = 0;
static bool DirectionSettling = false;

/*------------------------------ Module Code ------------------------------*/
bool InitActuatorService(uint8_t Priority)
{
  ES_Event_t ThisEvent = { ES_INIT, 0 };

  MyPriority = Priority;
  PlateFollowsPot = false;
  Dispensing = false;
  CurrentDirection = DirStop;
  CurrentDuty = 0;
  DirectionSettling = false;

  // ENB (RB15) and ENA (RB11): start low, then hand them to OC1 / OC2
  LATBCLR = (1u << 15) | (1u << 11);
  ANSELBbits.ANSB15 = 0;
  TRISBbits.TRISB15 = 0;
  TRISBbits.TRISB11 = 0;
  RPB15R = OC_PPS_CODE;
  RPB11R = OC_PPS_CODE;

  // Timer2: PBCLK 1:1, 1 kHz period
  T2CON = 0;
  T2CONbits.TCKPS = 0b000;
  TMR2 = 0;
  PR2 = PWM_PERIOD_TICKS;

  // OC1 and OC2: PWM mode (fault pin off), 16-bit, from Timer2, starting at 0 %
  OC1CON = 0;
  OC1CONbits.OCTSEL = 0;
  OC1CONbits.OCM = 0b110;
  OC1R = 0;
  OC1RS = 0;
  OC2CON = 0;
  OC2CONbits.OCTSEL = 0;
  OC2CONbits.OCM = 0b110;
  OC2R = 0;
  OC2RS = 0;

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

  if (ES_NEW_KEY == ThisEvent.EventType)
  {
    switch (ThisEvent.EventParam)
    {
      case 'a': ThisEvent.EventType = ES_PLATE_START;    break;
      case 'x': ThisEvent.EventType = ES_PLATE_STOP;     break;
      case 'd': ThisEvent.EventType = ES_DISPENSE_TREAT; break;
      case 'b': ThisEvent.EventType = ES_PLAY_DING;      break;
      case 'j':
        PlateFollowsPot = false;
        drivePlate(DirForward, TEST_DUTY);
        return ReturnEvent;
      case 'k':
        PlateFollowsPot = false;
        drivePlate(DirReverse, TEST_DUTY);
        return ReturnEvent;
      case '?':
        DB_printf("Plate: %s, direction %d, duty %d%%, pot %d; dispenser %s\r\n",
                  PlateFollowsPot ? "follows pot" : "manual",
                  CurrentDirection, CurrentDuty, GetPotReading(),
                  Dispensing ? "running" : "off");
        return ReturnEvent;
      default:
        return ReturnEvent;
    }
  }

  switch (ThisEvent.EventType)
  {
    case ES_INIT:
      DB_printf("ActuatorService ready (service %d). Test keys: a j k x d b ?\r\n", MyPriority);
      break;

    case ES_PLATE_START:
      PlateFollowsPot = true;
      ES_Timer_InitTimer(PLATE_SAMPLE_TIMER, PLATE_UPDATE_MS);
      break;

    case ES_PLATE_STOP:
      stopPlate();
      break;

    case ES_DISPENSE_TREAT:
      if (!Dispensing)
      {
        Dispensing = true;
        setDispenserDuty(DISPENSE_DUTY);
        ES_Timer_InitTimer(DISPENSER_TIMER, DISPENSE_TIME_MS);
      }
      break;

    case ES_PLAY_DING:
      setBell(true);
      ES_Timer_InitTimer(DING_TIMER, DING_TIME_MS);
      break;

    case ES_TIMEOUT:
      if (PLATE_SAMPLE_TIMER == ThisEvent.EventParam)
      {
        if (DirectionSettling)
        {
          // the new direction has been shifted out: now apply the speed
          DirectionSettling = false;
          setPlateDuty(PendingDuty);
        }
        if (PlateFollowsPot)
        {
          int Offset = (int)GetPotReading() - POT_CENTER;
          int Magnitude = abs(Offset);
          if (Magnitude <= POT_DEADBAND)
          {
            drivePlate(DirStop, 0);
          }
          else
          {
            int Span = (POT_CENTER - POT_DEADBAND);
            int Duty = PLATE_MIN_DUTY +
                       ((Magnitude - POT_DEADBAND) * (PLATE_MAX_DUTY - PLATE_MIN_DUTY)) / Span;
            if (Duty > PLATE_MAX_DUTY)
            {
              Duty = PLATE_MAX_DUTY;
            }
            drivePlate((Offset > 0) ? DirForward : DirReverse, (uint8_t)Duty);
          }
          ES_Timer_InitTimer(PLATE_SAMPLE_TIMER, PLATE_UPDATE_MS);
        }
      }
      else if (DISPENSER_TIMER == ThisEvent.EventParam)
      {
        setDispenserDuty(0);
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

static void setPlateDuty(uint8_t Duty)
{
  CurrentDuty = Duty;
  OC1RS = DUTY_TO_OCRS(Duty);
}

static void setDispenserDuty(uint8_t Duty)
{
  OC2RS = DUTY_TO_OCRS(Duty);
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
