/****************************************************************************
 Module
   LEDMatrixService.c

 Description
   Drives the MAX7219 LED matrix modules: cooked level and the game-clock (elapsed time) display.

 Notes
   STUB created on main so every branch builds against the same service list.
   Owner branch: led-matrix-display. Only edit this file on your own branch.
   Do not edit ES_Configure.h on a feature branch (see the README).

 History
 When           Who     What/Why
 -------------- ---     --------
 11/09/26       ---     stub
****************************************************************************/
/*----------------------------- Include Files -----------------------------*/
#include "../ProjectHeaders/LEDMatrixService.h"

#include "ES_Configure.h"
#include "ES_Framework.h"
#include "ES_Port.h"
#include "terminal.h"
#include "dbprintf.h"

/*---------------------------- Module Variables ---------------------------*/
static uint8_t MyPriority;

/*------------------------------ Module Code ------------------------------*/
bool InitLEDMatrixService(uint8_t Priority)
{
  ES_Event_t ThisEvent;

  MyPriority = Priority;

  ThisEvent.EventType = ES_INIT;
  return ES_PostToService(MyPriority, ThisEvent);
}

bool PostLEDMatrixService(ES_Event_t ThisEvent)
{
  return ES_PostToService(MyPriority, ThisEvent);
}

ES_Event_t RunLEDMatrixService(ES_Event_t ThisEvent)
{
  ES_Event_t ReturnEvent;
  ReturnEvent.EventType = ES_NO_EVENT;

  switch (ThisEvent.EventType)
  {
    case ES_INIT:
    {
      DB_printf("%s ready (service %d)\r\n", "LEDMatrixService", MyPriority);
    }
    break;
    default:
    {}
    break;
  }

  return ReturnEvent;
}
