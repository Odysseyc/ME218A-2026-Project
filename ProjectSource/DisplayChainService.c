/****************************************************************************
 Module
   DisplayChainService.c

 Description
   Drives the two chained 74HC595 shift registers (schematic: LEDS). They hold
   the plate LEDs, the time-button LEDs, the 5 food start-spot LEDs, the
   buzzer ("Bell") and the plate motor's direction inputs. GameService and
   ActuatorService say what should be on; this service turns that into 16
   bits and shifts them out.

 Notes
   Owner: Jeremy (595 code first written in GameService.c on the game-logic
   branch). Moved here during integration.

   Pins (schematic):  RB8  LED Data  -> first 595 SER (pin 14)
                      RB9  LED SRCLK -> both 595 SRCLK (pin 11)
                      RB10 LED RCLK  -> both 595 RCLK (pin 12)
   The first 595's QH' feeds the second 595's SER (LED Data In 2).

   First 595:  QA Bell (buzzer)   QB LED North   QC LED East   QD LED South
               QE LED West        QF 30s button  QG 45s button QH 60s button
   Second 595: QA to QE  LED Food Start 1 to 5
               QF  plate motor H-bridge IN3   QG  plate motor H-bridge IN4
               (QF and QG were unused on the schematic. The PIC has no free
               pins, so the H-bridge direction inputs go here. QH is free.)

   Plate LED n (0 = North ... 3 = West) is first-595 bit n + 1.
   Brightness is on or off for now (no dimming yet).

 History
 When           Who     What/Why
 -------------- ---     --------
 11/09/26       ---     stub
 11/11/26       Jeremy  595 shifting and plate LED bits
 11/13/26       ---     moved out of GameService into this service
 11/13/26       ---     both 595s per the schematic: bell, time-button
                        and start-spot LEDs
****************************************************************************/
/*----------------------------- Include Files -----------------------------*/
#include "../ProjectHeaders/DisplayChainService.h"

#include <xc.h>

#include "ES_Configure.h"
#include "ES_Framework.h"
#include "ES_Port.h"
#include "terminal.h"
#include "dbprintf.h"
#include "ES_Timers.h"
#include "ActuatorService.h"

/*----------------------------- Module Defines ----------------------------*/
#define SR_DATA   (1u << 8)     // RB8  -> 595 pin 14
#define SR_CLOCK  (1u << 9)     // RB9  -> 595 pin 11
#define SR_LATCH  (1u << 10)    // RB10 -> 595 pin 12

#define NUM_PLATE_LEDS   4
#define BELL_BIT         0x01u                    // first 595 QA
#define PLATE_LED_BIT(n) ((uint8_t)(1u << ((n) + 1u)))   // QB to QE
#define ALL_PLATE_LEDS   (PLATE_LED_BIT(0) | PLATE_LED_BIT(1) | PLATE_LED_BIT(2) | PLATE_LED_BIT(3))
#define TIME_LED_BITS    0xE0u                    // first 595 QF, QG, QH
#define START_LED_BITS   0x1Fu                    // second 595 QA to QE
#define MOTOR_IN3_BIT    0x20u                    // second 595 QF
#define MOTOR_IN4_BIT    0x40u                    // second 595 QG
#define MOTOR_DIR_STOP   0
#define MOTOR_DIR_FWD    1
#define MOTOR_DIR_REV    2

#define FLASH_MS         250                      // time-button LED flash rate

/*---------------------------- Module Functions ---------------------------*/
static void sendChain(void);
static void shiftByte(uint8_t Bits);
static void pinDelay(void);

/*---------------------------- Module Variables ---------------------------*/
static uint8_t MyPriority;
static uint8_t FirstBits = 0;           // first 595 (bell, plate, time LEDs)
static uint8_t SecondBits = 0;          // second 595 (start-spot LEDs)
static bool Flashing = false;

/*------------------------------ Module Code ------------------------------*/
bool InitDisplayChainService(uint8_t Priority)
{
  ES_Event_t ThisEvent;

  MyPriority = Priority;

  // the three 595 lines are digital outputs, starting low
  LATBCLR  = SR_DATA | SR_CLOCK | SR_LATCH;
  TRISBCLR = SR_DATA | SR_CLOCK | SR_LATCH;
  FirstBits = 0;
  SecondBits = 0;
  sendChain();                          // start with everything off

  ThisEvent.EventType = ES_INIT;
  return ES_PostToService(MyPriority, ThisEvent);
}

bool PostDisplayChainService(ES_Event_t ThisEvent)
{
  return ES_PostToService(MyPriority, ThisEvent);
}

ES_Event_t RunDisplayChainService(ES_Event_t ThisEvent)
{
  ES_Event_t ReturnEvent;
  ReturnEvent.EventType = ES_NO_EVENT;

  switch (ThisEvent.EventType)
  {
    case ES_INIT:
    {
      DB_printf("%s ready (service %d)\r\n", "DisplayChainService", MyPriority);
    }
    break;

   case ES_CHAIN_SET_LED:
{
    uint8_t index = (uint8_t)(ThisEvent.EventParam >> 8);
    uint8_t brightness = (uint8_t)ThisEvent.EventParam;

    if (index < NUM_PLATE_LEDS) {
        if (brightness > 0u) {
            FirstBits |= PLATE_LED_BIT(index);
        } else {
            FirstBits &= (uint8_t)~PLATE_LED_BIT(index);
        }
        sendChain();
    }
}
break;

    case ES_CHAIN_SET_ALL:
    {
      if (ThisEvent.EventParam > 0)
      {
        FirstBits |= ALL_PLATE_LEDS;
      }
      else
      {
        FirstBits &= (uint8_t)~ALL_PLATE_LEDS;
      }
      sendChain();
    }
    break;

    case ES_CHAIN_SET_START_LEDS:
    {
      if (ThisEvent.EventParam != 0)
      {
        SecondBits |= START_LED_BITS;
      }
      else
      {
        SecondBits &= (uint8_t)~START_LED_BITS;
      }
      sendChain();
    }
    break;

    case ES_CHAIN_FLASH_TIME_LEDS:
    {
      Flashing = (ThisEvent.EventParam != 0);
      FirstBits &= (uint8_t)~TIME_LED_BITS;
      if (Flashing)
      {
        FirstBits |= TIME_LED_BITS;     // start with them on
        ES_Timer_InitTimer(CHAIN_TIMER, FLASH_MS);
      }
      else
      {
        ES_Timer_StopTimer(CHAIN_TIMER);
      }
      sendChain();
    }
    break;

    case ES_TIMEOUT:
    {
      if ((CHAIN_TIMER == ThisEvent.EventParam) && Flashing)
      {
        FirstBits ^= TIME_LED_BITS;
        sendChain();
        ES_Timer_InitTimer(CHAIN_TIMER, FLASH_MS);
      }
    }
    break;

    case ES_CHAIN_SET_MOTOR_DIR:
{
    uint8_t dir = (uint8_t)ThisEvent.EventParam;

    SecondBits &= (uint8_t)~(MOTOR_IN3_BIT | MOTOR_IN4_BIT);
    if (dir == MOTOR_DIR_FWD) {
        SecondBits |= MOTOR_IN3_BIT;
    } else if (dir == MOTOR_DIR_REV) {
        SecondBits |= MOTOR_IN4_BIT;
    }

    sendChain();

    if ((ThisEvent.EventParam >> 8) != 0u) {
        (void)PostActuatorService(ThisEvent);
    }
}
break;

    case ES_CHAIN_SET_BELL:
    {
      // Param = 1 buzzer on, 0 off. ActuatorService times the ding.
      if (ThisEvent.EventParam != 0)
      {
        FirstBits |= BELL_BIT;
      }
      else
      {
        FirstBits &= (uint8_t)~BELL_BIT;
      }
      sendChain();
    }
    break;

    default:
    {}
    break;
  }

  return ReturnEvent;
}

/***************************************************************************
 private functions
 ***************************************************************************/
// Sends all 16 bits and latches them. The second 595's byte goes first,
// because it has to pass through the first 595 to get there. Each byte is
// sent most significant bit first, so bit 7 ends up on QH and bit 0 on QA.
// Takes a few microseconds, so it is done all at once.
static void sendChain(void)
{
  LATBCLR = SR_LATCH;
  shiftByte(SecondBits);
  shiftByte(FirstBits);
  LATBCLR = SR_CLOCK;
  LATBSET = SR_LATCH;                   // show the new bits on the outputs
  pinDelay();
  LATBCLR = SR_LATCH;
}

static void shiftByte(uint8_t Bits)
{
  for (uint8_t Mask = 0x80; Mask != 0; Mask >>= 1)
  {
    LATBCLR = SR_CLOCK;
    if (Bits & Mask)
    {
      LATBSET = SR_DATA;
    }
    else
    {
      LATBCLR = SR_DATA;
    }
    pinDelay();
    LATBSET = SR_CLOCK;                 // the 595 reads the data on this edge
    pinDelay();
  }
}

// a short pause so each 595 pulse is long enough
static void pinDelay(void)
{
  for (volatile uint8_t i = 0; i < 10; i++)
  {
  }
}
/*------------------------------ End of file ------------------------------*/
