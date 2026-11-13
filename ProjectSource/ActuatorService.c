/****************************************************************************
 Module
   ActuatorService.c

 Description
   Planned service responsibilities from the original stub:
   Samples the potentiometer, drives the plate servo, detects when the food reaches a plate LED (posts ES_FOOD_PASSED with the LED number), runs the TREAT dispenser servo and the buzzer ding.

   Current implementation: potentiometer direction control of the DC plate motor.
   Encoder feedback, dispensing and the buzzer remain pending.

 Notes
   STUB created on main so every branch builds against the same service list.
   Owner branch: actuators. Only edit this file on your own branch.
   Do not edit ES_Configure.h on a feature branch (see the README).

   Keys: a arms, x/Escape stops and disarms, ? shows the current state.
   Pot on pin 6/RB2/AN4: low reverses, center stops, high runs forward.
   The pot uses 3.3 V and GND; it is sampled every 20 ms.
   PIC pins: 2/RA0 -> IN3, 3/RA1 -> IN4, 26/RB15 -> ENB, 22/RB11 -> ENA.
   RA0/RA1 overlap the planned SPI display; reassign before integration.
   Channel A stays off. Arming lasts until x/Escape, ES_PLATE_STOP or reset.
   No encoder feedback or speed control is used.

 History
 When           Who     What/Why
 -------------- ---     --------
 11/09/26       ---     stub
 11/12/26       ---     continuous start/stop control and direction selection
 11/13/26       ---     pot direction with center stop and UART arming
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

/*----------------------------- Module Defines ----------------------------*/
#define PLATE_ENABLE      (1u << 15)
#define BOTH_ENABLES      (PLATE_ENABLE | (1u << 11))
#define POT_SAMPLE_MS     20

// Stop near the center of the pot's 0-1023 range.
#define POT_CENTER        512
#define POT_DEADBAND      64

/*------------------------------ Module Types -----------------------------*/
typedef enum { Stopped, Forward, Reverse } MotorDirection_t;

/*---------------------------- Module Functions ---------------------------*/
static void ApplyMotorOutput(void);

/*---------------------------- Module Variables ---------------------------*/
static uint8_t MyPriority;
static bool MotorArmed = false;
static uint16_t PotValue = 512;
static MotorDirection_t SelectedDirection = Stopped;

/*------------------------------ Module Code ------------------------------*/
bool InitActuatorService(uint8_t Priority)
{
  ES_Event_t ThisEvent = { ES_INIT, 0 };

  MyPriority = Priority;
  MotorArmed = false;
  PotValue = 512;
  SelectedDirection = Stopped;

  // Set enable latches low before making the motor-control pins outputs.
  ApplyMotorOutput();
  ANSELBbits.ANSB15 = 0;
  TRISBbits.TRISB15 = 0;
  TRISBbits.TRISB11 = 0;
  ANSELAbits.ANSA0 = 0;
  ANSELAbits.ANSA1 = 0;
  TRISAbits.TRISA0 = 0;
  TRISAbits.TRISA1 = 0;

  // Read AN4 continuously into ADC1BUF0. The service timer reads the latest value.
  PMD1bits.AD1MD = 0;
  AD1CON1 = 0;
  AD1CON2 = 0;                  // AVDD/AVSS references, one channel, buffer 0.
  AD1CON3 = 0;
  AD1CHS = 0;
  AD1CSSL = 0;
  ANSELBbits.ANSB2 = 1;
  TRISBbits.TRISB2 = 1;
  IEC0bits.AD1IE = 0;           // No ADC interrupt is needed.
  IFS0bits.AD1IF = 0;
  AD1CON3bits.ADCS = 19;        // 2 us ADC clock at the 20 MHz peripheral clock.
  AD1CON3bits.SAMC = 10;        // 20 us acquisition time.
  AD1CHSbits.CH0SA = 4;         // Physical pin 6 = RB2/AN4.
  AD1CON1bits.SSRC = 7;         // Auto-convert after the acquisition time.
  AD1CON1bits.ASAM = 1;         // Automatically sample again after conversion.
  AD1CON1bits.ON = 1;

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
  MotorDirection_t NewDirection = Stopped;

  switch (ThisEvent.EventType)
  {
    case ES_INIT:
      MotorArmed = false;
      SelectedDirection = Stopped;
      ApplyMotorOutput();
      ES_Timer_InitTimer(PLATE_SAMPLE_TIMER, POT_SAMPLE_MS);
      DB_printf("Pot control DISARMED: a arm, x stop, ? status; center stops\r\n");
      break;

    case ES_TIMEOUT:
      if (ThisEvent.EventParam == PLATE_SAMPLE_TIMER)
      {
        ES_Timer_InitTimer(PLATE_SAMPLE_TIMER, POT_SAMPLE_MS);
        PotValue = ADC1BUF0;
        if (PotValue < POT_CENTER - POT_DEADBAND)
          NewDirection = Reverse;
        else if (PotValue > POT_CENTER + POT_DEADBAND)
          NewDirection = Forward;

        if (NewDirection != SelectedDirection)
        {
          SelectedDirection = NewDirection;
          ApplyMotorOutput();
          ReportStatus = true;
        }
      }
      break;

    case ES_NEW_KEY:
      ReportStatus = true;
      switch (ThisEvent.EventParam)
      {
        case 'a':
          if (!MotorArmed)
          {
            MotorArmed = true;
            ApplyMotorOutput();
          }
          break;
        case 'x':
        case 27:
          MotorArmed = false;
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
      MotorArmed = false;
      ApplyMotorOutput();
      break;

    /* TODO: add encoder feedback, dispenser sequencing and buzzer here. */
    case ES_PLATE_START:
    case ES_DISPENSE_TREAT:
    case ES_PLAY_DING:
    default:
      break;
  }

  if (ReportStatus)
    DB_printf("Plate: %s; pot=%u; direction=%s; output=%s; dispenser off\r\n",
              MotorArmed ? "ARMED" : "DISARMED", (unsigned)PotValue,
              SelectedDirection == Stopped ? "center" :
              SelectedDirection == Forward ? "forward" : "reverse",
              MotorArmed && SelectedDirection != Stopped ? "ON" : "OFF");
  return ReturnEvent;
}

/***************************************************************************
 private functions
 ***************************************************************************/
// Disable the bridge before changing direction; keep the dispenser disabled.
static void ApplyMotorOutput(void)
{
  LATBCLR = BOTH_ENABLES;
  LATAbits.LATA0 = SelectedDirection == Forward;
  LATAbits.LATA1 = SelectedDirection == Reverse;
  if (MotorArmed && SelectedDirection != Stopped)
    LATBSET = PLATE_ENABLE;
}
