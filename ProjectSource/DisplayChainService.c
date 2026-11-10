/****************************************************************************
 Module
   DisplayChainService.c

 Description
   Drives the plate LED chain through the 74xxx595 shift registers: per-LED brightness (dimming), flashing the timer-button LEDs.

 Notes
   STUB created on main so every branch builds against the same service list.
   Owner branch: display-chain. Only edit this file on your own branch.
   Do not edit ES_Configure.h on a feature branch (see the README).

 History
 When           Who     What/Why
 -------------- ---     --------
 11/09/26       ---     stub
****************************************************************************/
/*----------------------------- Include Files -----------------------------*/
#include "../ProjectHeaders/DisplayChainService.h"

#include "ES_Configure.h"
#include "ES_Framework.h"
#include "ES_Port.h"
#include "terminal.h"
#include "dbprintf.h"

/*---------------------------- Module Variables ---------------------------*/
static uint8_t MyPriority;

/*------------------------------ Module Code ------------------------------*/
bool InitDisplayChainService(uint8_t Priority)
{
  ES_Event_t ThisEvent;

  MyPriority = Priority;

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
    default:
    {}
    break;
  }

  return ReturnEvent;
}
