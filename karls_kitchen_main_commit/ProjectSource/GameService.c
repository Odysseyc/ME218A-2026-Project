/****************************************************************************
 Module
   GameService.c

 Description
   Game state machine (Welcome, SelectTime, Cooking, Won, Lost) from the Checkpoint 1 diagram. Owns the inactivity, game-clock and result timers and decides when to send commands to the other services.

 Notes
   STUB created on main so every branch builds against the same service list.
   Owner branch: game-logic. Only edit this file on your own branch.
   Do not edit ES_Configure.h on a feature branch (see the README).

 History
 When           Who     What/Why
 -------------- ---     --------
 11/09/26       ---     stub
****************************************************************************/
/*----------------------------- Include Files -----------------------------*/
#include "../ProjectHeaders/GameService.h"

#include "ES_Configure.h"
#include "ES_Framework.h"
#include "ES_Port.h"
#include "terminal.h"
#include "dbprintf.h"

/*---------------------------- Module Variables ---------------------------*/
static uint8_t MyPriority;

typedef enum { Welcome = 0, SelectTime, Cooking, Won, Lost } GameState_t;
static GameState_t CurrentState;

/*------------------------------ Module Code ------------------------------*/
bool InitGameService(uint8_t Priority)
{
  ES_Event_t ThisEvent;

  MyPriority = Priority;
  CurrentState = Welcome;

  ThisEvent.EventType = ES_INIT;
  return ES_PostToService(MyPriority, ThisEvent);
}

bool PostGameService(ES_Event_t ThisEvent)
{
  return ES_PostToService(MyPriority, ThisEvent);
}

ES_Event_t RunGameService(ES_Event_t ThisEvent)
{
  ES_Event_t ReturnEvent;
  ReturnEvent.EventType = ES_NO_EVENT;

  switch (CurrentState)
  {
    case Welcome:
    {
      if (ES_INIT == ThisEvent.EventType)
      {
        DB_printf("GameService ready (service %d)\r\n", MyPriority);
      }
      // ES_FOOD_REMOVED -> SelectTime
    }
    break;
    case SelectTime:
    {
      // ES_TIME_SELECTED -> Cooking, ES_TIMEOUT (inactivity) -> Welcome
    }
    break;
    case Cooking:
    {
      // ES_FOOD_PASSED, ES_TOO_FAST (stay), ES_COOKED -> Won, ES_GAME_CLOCK_ELAPSED -> Lost
    }
    break;
    case Won:
    case Lost:
    {
      // ES_TIMEOUT (result timer) -> Welcome
    }
    break;
    default:
    {}
    break;
  }

  return ReturnEvent;
}
