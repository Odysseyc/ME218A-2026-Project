/****************************************************************************
 Module
   ActuatorService.c

 Description
   Planned service responsibilities from the original stub:
   Samples the potentiometer, drives the plate servo, detects when the food reaches a plate LED (posts ES_FOOD_PASSED with the LED number), runs the TREAT dispenser servo and the buzzer ding.

   Current implementation: manual DC plate-motor testing over UART.
   Potentiometer/encoder control, dispensing and the buzzer remain pending.

 Notes
   STUB created on main so every branch builds against the same service list.
   Owner branch: actuators. Only edit this file on your own branch.
   Do not edit ES_Configure.h on a feature branch (see the README).

   Keys: a starts, j selects forward, k selects reverse, x/Escape stops.
   Direction changes take effect while running; ? shows the current state.
   PIC pins: 9/RA2 -> IN3, 10/RA3 -> IN4, 26/RB15 -> ENB, 22/RB11 -> ENA.
   (IN3/IN4 moved from RA0/RA1 at integration: those are the South and West
   Hall sensor inputs. RA2/RA3 are free because the PIC runs on its internal
   oscillator.)
   Channel A stays off. There is no automatic motor cutoff in manual mode.

 History
 When           Who     What/Why
 -------------- ---     --------
 11/09/26       ---     stub
 11/12/26       ---     continuous start/stop control and direction selection
 11/13/26       ---     integration: IN3/IN4 moved to RA2/RA3
****************************************************************************/
/*----------------------------- Include Files -----------------------------*/
#include "../ProjectHeaders/ActuatorService.h"

#include <xc.h>

#include "ES_Configure.h"
#include "ES_Framework.h"
#include "ES_Port.h"
#include "terminal.h"
#include "dbprintf.h"

/*----------------------------- Module Defines ----------------------------*/
#define PLATE_ENABLE      (1u << 15)
#define BOTH_ENABLES      (PLATE_ENABLE | (1u << 11))

/*------------------------------ Module Types -----------------------------*/
typedef enum { Forward, Reverse } MotorDirection_t;

/*---------------------------- Module Functions ---------------------------*/
static void ApplyMotorOutput(void);

/*---------------------------- Module Variables ---------------------------*/
static uint8_t MyPriority;
static bool MotorRunning = false;
static MotorDirection_t SelectedDirection = Forward;

/*------------------------------ Module Code ------------------------------*/
bool InitActuatorService(uint8_t Priority)
{
  ES_Event_t ThisEvent = { ES_INIT, 0 };

  MyPriority = Priority;
  MotorRunning = false;
  SelectedDirection = Forward;

  // Set enable latches low before making the motor-control pins outputs.
  ApplyMotorOutput();
  ANSELBbits.ANSB15 = 0;
  TRISBbits.TRISB15 = 0;
  TRISBbits.TRISB11 = 0;
  TRISAbits.TRISA2 = 0;               // RA2 / RA3 have no analog function
  TRISAbits.TRISA3 = 0;

  return ES_PostToService(MyPriority, ThisEvent);
}

bool PostActuatorService(ES_Event_t ThisEvent)
{
  return ES_PostToService(MyPriority, ThisEvent);
}

ES_Event_t RunActuatorService(ES_Event_t ThisEvent)
{
  ES_Event_t ReturnEvent = { ES_NO_EVENT, 0 };
  bool ReportStatus = false;

  switch (ThisEvent.EventType)
  {
    case ES_INIT:
      MotorRunning = false;
      ApplyMotorOutput();
      DB_printf("Actuators stopped: a start, j forward, k reverse, x stop, ? status\r\n");
      break;

    case ES_NEW_KEY:
      ReportStatus = true;
      switch (ThisEvent.EventParam)
      {
        case 'a':
          if (!MotorRunning)
          {
            MotorRunning = true;
            ApplyMotorOutput();
          }
          break;
        case 'j':
          if (SelectedDirection != Forward)
          {
            SelectedDirection = Forward;
            ApplyMotorOutput();
          }
          break;
        case 'k':
          if (SelectedDirection != Reverse)
          {
            SelectedDirection = Reverse;
            ApplyMotorOutput();
          }
          break;
        case 'x':
        case 27:
          MotorRunning = false;
          ApplyMotorOutput();
          break;
        case '?':
          break;
        default:
          ReportStatus = false;
          break;
      }
      break;

    case ES_PLATE_STOP:
      MotorRunning = false;
      ApplyMotorOutput();
      break;

    /* TODO: add pot/encoder control, dispenser sequencing and buzzer here. */
    case ES_PLATE_START:
    case ES_DISPENSE_TREAT:
    case ES_PLAY_DING:
    default:
      break;
  }

  if (ReportStatus)
    DB_printf("Plate: %s; direction: %s; dispenser off\r\n",
              MotorRunning ? "RUNNING" : "STOPPED",
              SelectedDirection == Forward ? "forward" : "reverse");
  return ReturnEvent;
}

/***************************************************************************
 private functions
 ***************************************************************************/
// Disable the bridge before changing direction; keep the dispenser disabled.
static void ApplyMotorOutput(void)
{
  LATBCLR = BOTH_ENABLES;
  LATAbits.LATA2 = SelectedDirection == Forward;
  LATAbits.LATA3 = SelectedDirection == Reverse;
  if (MotorRunning)
    LATBSET = PLATE_ENABLE;
}
