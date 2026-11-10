/****************************************************************************
 Module
   ActuatorService.c

 Description
   Samples the potentiometer, drives the plate servo, detects ES_FOOD_PASSED and ES_TOO_FAST, runs the TREAT dispenser servo and the buzzer ding.

 Notes
   STUB created on main so every branch builds against the same service list.
   Owner branch: actuators. Only edit this file on your own branch.
   Do not edit ES_Configure.h on a feature branch (see the README).

 History
 When           Who     What/Why
 -------------- ---     --------
 11/09/26       ---     stub
****************************************************************************/
/*----------------------------- Include Files -----------------------------*/
#include "../ProjectHeaders/ActuatorService.h"

#include "ES_Configure.h"
#include "ES_Framework.h"
#include "ES_Port.h"
#include "terminal.h"
#include "dbprintf.h"

/*---------------------------- Module Variables ---------------------------*/
static uint8_t MyPriority;

/*------------------------------ Module Code ------------------------------*/
bool InitActuatorService(uint8_t Priority)
{
  ES_Event_t ThisEvent;

  MyPriority = Priority;

  ThisEvent.EventType = ES_INIT;
  return ES_PostToService(MyPriority, ThisEvent);
}

bool PostActuatorService(ES_Event_t ThisEvent)
{
  return ES_PostToService(MyPriority, ThisEvent);
}

ES_Event_t RunActuatorService(ES_Event_t ThisEvent)
{
  ES_Event_t ReturnEvent;
  ReturnEvent.EventType = ES_NO_EVENT;

  switch (ThisEvent.EventType)
  {
    case ES_INIT:
    {
      DB_printf("%s ready (service %d)\r\n", "ActuatorService", MyPriority);
    }
    break;
    default:
    {}
    break;
  }

  return ReturnEvent;
}
