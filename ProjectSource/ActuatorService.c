/****************************************************************************
 Module
   ActuatorService.c

 Description
   Planned service responsibilities from the original stub:
   Samples the potentiometer, drives the plate servo, detects when the food reaches a plate LED (posts ES_FOOD_PASSED with the LED number), runs the TREAT dispenser servo and the buzzer ding.

   Current implementation: pot throttle control of the DC plate motor and a
   timed DC dispenser cycle. The buzzer remains pending.

 Notes
   STUB created on main so every branch builds against the same service list.
   Owner branch: actuators. Only edit this file on your own branch.
   Do not edit ES_Configure.h on a feature branch (see the README).

   Keys: a arms, d dispenses once, x/Escape stops both, ? shows the state.
   Dispenser: 3 seconds forward, 3 seconds reverse, then off.
   Repeated dispense requests are ignored until the current cycle finishes.
   Pot on pin 6/RB2/AN4: low reverses, center stops, high runs forward.
   Turning farther from center increases PWM duty with a square-root curve:
   25% travel beyond the deadband gives about 50% duty, 50% gives about 71%.
   The pot uses 3.3 V and GND; it is sampled every 20 ms.
   Plate pins: 2/RA0 -> IN3, 3/RA1 -> IN4, 26/RB15 -> ENB.
   Dispenser pins: 7/RB3 -> IN1, 11/RB4 -> IN2, 22/RB11 -> ENA.
   RA0/RA1 overlap the planned SPI display; reassign before integration.
   RB4 overlaps the display SS pin on main; reassign before merging branches.
   Arming lasts until x/Escape, ES_PLATE_STOP or reset.
   This bench mode requires a before dispensing; game start/stop remains TODO.
   ENB uses OC1/Timer2 at 1 kHz. The encoder is not used.
   This sets motor drive, not a measured speed; speed depends on the load.

 History
 When           Who     What/Why
 -------------- ---     --------
 11/09/26       ---     stub
 11/12/26       ---     continuous start/stop control and direction selection
 11/13/26       ---     pot direction with center stop and UART arming
 11/14/26       ---     nonblocking forward/reverse dispenser cycle
 11/14/26       ---     bidirectional pot throttle with hardware PWM
 11/14/26       ---     quick-rise square-root response in both directions
****************************************************************************/
/*----------------------------- Include Files -----------------------------*/
#include "../ProjectHeaders/ActuatorService.h"

#include <xc.h>

#include "ES_Configure.h"
#include "ES_Framework.h"
#include "ES_Port.h"
#include "ES_Timers.h"
#include "dbprintf.h"

/*----------------------------- Module Defines ----------------------------*/
#define PLATE_ENABLE      (1u << 15)
#define DISPENSER_ENABLE  (1u << 11)
#define BOTH_ENABLES      (PLATE_ENABLE | DISPENSER_ENABLE)
#define POT_SAMPLE_MS     20
#define DISPENSER_RUN_MS  3000
#define PWM_PERIOD        20000u  // 20 MHz peripheral clock / 1 kHz.

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
static uint8_t PlateDuty = 0;     // Percent, 0-100.
static MotorDirection_t DispenserDirection = Stopped;
static uint16_t DispenserStarted;

/*------------------------------ Module Code ------------------------------*/
bool InitActuatorService(uint8_t Priority)
{
  ES_Event_t ThisEvent = { ES_INIT, 0 };

  MyPriority = Priority;
  MotorArmed = false;
  PotValue = 512;
  SelectedDirection = Stopped;
  PlateDuty = 0;
  DispenserDirection = Stopped;

  // Set enable latches low before making the motor-control pins outputs.
  PMD3bits.OC1MD = 0;
  PMD4bits.T2MD = 0;
  ApplyMotorOutput();
  ANSELBbits.ANSB15 = 0;
  TRISBbits.TRISB15 = 0;
  TRISBbits.TRISB11 = 0;
  ANSELBbits.ANSB3 = 0;
  TRISBbits.TRISB3 = 0;
  TRISBbits.TRISB4 = 0;         // Secondary oscillator is disabled in ES_Port.c.
  ANSELAbits.ANSA0 = 0;
  ANSELAbits.ANSA1 = 0;
  TRISAbits.TRISA0 = 0;
  TRISAbits.TRISA1 = 0;

  // OC1 drives ENB on pin 26. Timer2 runs PWM without an interrupt.
  T2CON = 0;
  TMR2 = 0;
  PR2 = PWM_PERIOD - 1;
  OC1CON = 0;
  OC1R = 0;
  OC1RS = 0;
  OC1CONbits.OCTSEL = 0;       // Timer2, 1:1 prescaler.
  OC1CONbits.OCM = 6;          // PWM mode, fault input disabled.
  IEC0bits.OC1IE = 0;
  IEC0bits.T2IE = 0;
  IFS0bits.OC1IF = 0;
  IFS0bits.T2IF = 0;
  RPB15R = 5;                 // OC1 -> RB15 (physical pin 26).
  T2CONbits.ON = 1;

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
  uint16_t Elapsed;
  MotorDirection_t NewDirection = Stopped;
  uint8_t NewDuty = 0;
  uint16_t DutySquared = 0;

  // Keyboard controls use the same stop and dispense handlers as service events.
  if (ThisEvent.EventType == ES_NEW_KEY)
  {
    if (ThisEvent.EventParam == 'd')
      ThisEvent.EventType = ES_DISPENSE_TREAT;
    else if (ThisEvent.EventParam == 'x' || ThisEvent.EventParam == 27)
    {
      ThisEvent.EventType = ES_PLATE_STOP;
      ReportStatus = true;
    }
  }

  switch (ThisEvent.EventType)
  {
    case ES_INIT:
      MotorArmed = false;
      SelectedDirection = Stopped;
      PlateDuty = 0;
      DispenserDirection = Stopped;
      ES_Timer_StopTimer(SERVO_TIMER);
      ApplyMotorOutput();
      ES_Timer_InitTimer(PLATE_SAMPLE_TIMER, POT_SAMPLE_MS);
      DB_printf("Throttle DISARMED: a arm, d dispense, x stop both, ? status; center stops, farther turns faster\r\n");
      break;

    case ES_TIMEOUT:
      if (ThisEvent.EventParam == PLATE_SAMPLE_TIMER)
      {
        ES_Timer_InitTimer(PLATE_SAMPLE_TIMER, POT_SAMPLE_MS);
        PotValue = ADC1BUF0;
        int Offset = (int)PotValue - POT_CENTER;
        unsigned Distance = Offset < 0 ? -Offset : Offset;

        // Duty = 100 * sqrt(distance / range), rounded up to a whole percent.
        // Compare squares below to keep the calculation integer-only.
        if (Distance > POT_DEADBAND)
        {
          NewDirection = Offset < 0 ? Reverse : Forward;
          unsigned Range = Offset < 0 ? POT_CENTER - POT_DEADBAND :
                                        1023 - POT_CENTER - POT_DEADBAND;
          DutySquared = ((Distance - POT_DEADBAND) * 10000u + Range - 1u) / Range;
          while (NewDuty * NewDuty < DutySquared)
            NewDuty++;
        }

        if (NewDirection != SelectedDirection)
        {
          SelectedDirection = NewDirection;
          PlateDuty = NewDuty;
          ApplyMotorOutput();
          ReportStatus = true;
        }
        else if (NewDuty != PlateDuty)
        {
          PlateDuty = NewDuty;
          // Same direction: change duty at the next PWM period without a restart.
          OC1RS = MotorArmed ? PWM_PERIOD * PlateDuty / 100u : 0;
          ReportStatus = true;
        }
      }
      else if (ThisEvent.EventParam == SERVO_TIMER &&
               MotorArmed && DispenserDirection != Stopped)
      {
        // Ignore an old queued timeout after a new cycle starts.
        Elapsed = (uint16_t)(ES_Timer_GetTime() - DispenserStarted);
        if (Elapsed < DISPENSER_RUN_MS)
          ES_Timer_InitTimer(SERVO_TIMER, DISPENSER_RUN_MS - Elapsed);
        else
        {
          DispenserDirection = DispenserDirection == Forward ? Reverse : Stopped;
          ApplyMotorOutput();
          if (DispenserDirection == Reverse)
          {
            DispenserStarted = ES_Timer_GetTime();
            ES_Timer_InitTimer(SERVO_TIMER, DISPENSER_RUN_MS);
          }
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
        case '?':
          break;
        default:
          ReportStatus = false;
          break;
      }
      break;

    case ES_PLATE_STOP:
      MotorArmed = false;
      DispenserDirection = Stopped;
      ES_Timer_StopTimer(SERVO_TIMER);
      ApplyMotorOutput();
      break;

    case ES_DISPENSE_TREAT:
      if (MotorArmed && DispenserDirection == Stopped)
      {
        DispenserDirection = Forward;
        ApplyMotorOutput();
        DispenserStarted = ES_Timer_GetTime();
        ES_Timer_InitTimer(SERVO_TIMER, DISPENSER_RUN_MS);
      }
      ReportStatus = true;
      break;

    /* TODO: add game start/stop behavior and buzzer here. */
    case ES_PLATE_START:
    case ES_PLAY_DING:
    default:
      break;
  }

  if (ReportStatus)
    DB_printf("Plate: %s; pot=%u; direction=%s; duty=%u%%; output=%s; dispenser=%s\r\n",
              MotorArmed ? "ARMED" : "DISARMED", (unsigned)PotValue,
              SelectedDirection == Stopped ? "center" :
              SelectedDirection == Forward ? "forward" : "reverse",
              (unsigned)(MotorArmed ? PlateDuty : 0),
              MotorArmed && SelectedDirection != Stopped ? "ON" : "OFF",
              DispenserDirection == Stopped ? "OFF" :
              DispenserDirection == Forward ? "forward" : "reverse");
  return ReturnEvent;
}

/***************************************************************************
 private functions
 ***************************************************************************/
// OC1 owns ENB while running: LATBCLR alone cannot stop it.
// Disable PWM before changing direction, then restore each active motor.
static void ApplyMotorOutput(void)
{
  OC1CONbits.ON = 0;
  LATBCLR = BOTH_ENABLES;
  LATAbits.LATA0 = SelectedDirection == Forward;
  LATAbits.LATA1 = SelectedDirection == Reverse;
  LATBbits.LATB3 = DispenserDirection == Forward;
  LATBbits.LATB4 = DispenserDirection == Reverse;
  OC1R = MotorArmed ? PWM_PERIOD * PlateDuty / 100u : 0;
  OC1RS = OC1R;
  if (MotorArmed && SelectedDirection != Stopped && PlateDuty != 0)
    OC1CONbits.ON = 1;
  if (MotorArmed && DispenserDirection != Stopped)
    LATBSET = DISPENSER_ENABLE;
}
