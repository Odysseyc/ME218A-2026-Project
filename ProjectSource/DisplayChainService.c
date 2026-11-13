/****************************************************************************
 Module
   DisplayChainService.c

 Description
   Drives the plate LEDs through the 74HC595 shift register. GameService
   tells it which LEDs to light; this service turns that into the 8 bits the
   595 holds and shifts them out.

 Notes
   Owner: Jeremy (595 code first written in GameService.c on the game-logic
   branch). Moved here during integration.

   Pins:  RB8 -> 595 pin 14 (SER, data)
          RB9 -> 595 pin 11 (SRCLK, shift clock)
          RB10 -> 595 pin 12 (RCLK, latch)

   595 bit (sent most significant first) for each plate LED:
          LED 0 North = bit 1, LED 1 East = bit 2,
          LED 2 South = bit 3, LED 3 West = bit 4
   Bits 0, 5, 6 and 7 are free for the start-position LEDs, the time-button
   LEDs or a second 595 later.

   Not done yet: dimming (brightness is only on or off for now), flashing the
   time-button LEDs (ES_CHAIN_FLASH_TIME_LEDS) and the 4 start-position LEDs
   (ES_CHAIN_SET_START_LEDS). Their requests are remembered so the hardware
   can be added without changing GameService.

 History
 When           Who     What/Why
 -------------- ---     --------
 11/09/26       ---     stub
 11/11/26       Jeremy  595 shifting and plate LED bits
 11/13/26       ---     moved out of GameService into this service
****************************************************************************/
/*----------------------------- Include Files -----------------------------*/
#include "../ProjectHeaders/DisplayChainService.h"

#include <xc.h>

#include "ES_Configure.h"
#include "ES_Framework.h"
#include "ES_Port.h"
#include "terminal.h"
#include "dbprintf.h"

/*----------------------------- Module Defines ----------------------------*/
#define SR_DATA   (1u << 8)     // RB8  -> 595 pin 14
#define SR_CLOCK  (1u << 9)     // RB9  -> 595 pin 11
#define SR_LATCH  (1u << 10)    // RB10 -> 595 pin 12

#define NUM_PLATE_LEDS   4
#define PLATE_LED_BIT(n) ((uint8_t)(1u << ((n) + 1u)))
#define ALL_PLATE_LEDS   (PLATE_LED_BIT(0) | PLATE_LED_BIT(1) | PLATE_LED_BIT(2) | PLATE_LED_BIT(3))

/*---------------------------- Module Functions ---------------------------*/
static void send595(uint8_t Bits);
static void pinDelay(void);

/*---------------------------- Module Variables ---------------------------*/
static uint8_t MyPriority;
static uint8_t ChainBits = 0;           // what the 595 is showing
static bool StartLEDsOn = false;        // requested, no hardware yet
static bool FlashTimeLEDs = false;      // requested, no hardware yet

/*------------------------------ Module Code ------------------------------*/
bool InitDisplayChainService(uint8_t Priority)
{
  ES_Event_t ThisEvent;

  MyPriority = Priority;

  // the three 595 lines are digital outputs, starting low
  LATBCLR  = SR_DATA | SR_CLOCK | SR_LATCH;
  TRISBCLR = SR_DATA | SR_CLOCK | SR_LATCH;
  ChainBits = 0;
  send595(ChainBits);                   // start with the plate dark

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
      // Param = (LED index << 8) | brightness. Any brightness above 0 is on.
      uint8_t Index = (uint8_t)(ThisEvent.EventParam >> 8);
      uint8_t Brightness = (uint8_t)(ThisEvent.EventParam & 0xFF);
      if (Index < NUM_PLATE_LEDS)
      {
        if (Brightness > 0)
        {
          ChainBits |= PLATE_LED_BIT(Index);
        }
        else
        {
          ChainBits &= (uint8_t)~PLATE_LED_BIT(Index);
        }
        send595(ChainBits);
      }
    }
    break;

    case ES_CHAIN_SET_ALL:
    {
      if (ThisEvent.EventParam > 0)
      {
        ChainBits |= ALL_PLATE_LEDS;
      }
      else
      {
        ChainBits &= (uint8_t)~ALL_PLATE_LEDS;
      }
      send595(ChainBits);
    }
    break;

    case ES_CHAIN_SET_START_LEDS:
    {
      StartLEDsOn = (ThisEvent.EventParam != 0);   // TODO: wire to a 595 bit
    }
    break;

    case ES_CHAIN_FLASH_TIME_LEDS:
    {
      FlashTimeLEDs = (ThisEvent.EventParam != 0); // TODO: flash with CHAIN_TIMER
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
// Shifts 8 bits into the 595 (most significant first) and latches them.
// Takes a few microseconds, so it is done all at once.
static void send595(uint8_t Bits)
{
  LATBCLR = SR_LATCH;
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
  LATBCLR = SR_CLOCK;
  LATBSET = SR_LATCH;                   // show the new bits on the outputs
  pinDelay();
  LATBCLR = SR_LATCH;
}

// a short pause so each 595 pulse is long enough
static void pinDelay(void)
{
  for (volatile uint8_t i = 0; i < 10; i++)
  {
  }
}
/*------------------------------ End of file ------------------------------*/
