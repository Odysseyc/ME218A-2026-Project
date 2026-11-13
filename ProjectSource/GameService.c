/****************************************************************************
 Module
   GameService.c

 Description
   Game state machine for the whack-a-mole microwave. One of the 4 plate LEDs
   lights at random. The player spins the plate to it before it turns off.
   Each hit fills the cooked-level display a bit. Fill it before the game
   clock runs out and the player wins (ding and TREAT).

   States: Welcome, SelectTime, Cooking, Won, Lost, ReturnFood

   After a game ends (or is abandoned) the 5 LEDs around the food's start
   position light up if the food is not back there yet (ReturnFood). Nothing
   starts until the food is put back (ES_FOOD_PLACED) and then lifted off
   again (ES_FOOD_REMOVED).

 Notes
   Owner: Adam. Plate LED timing per difficulty from Jeremy's plate code.
   The Hall sensors (AnalogInputs.c) post ES_FOOD_PASSED and the 595 code is
   in DisplayChainService.c.
   With KEYBOARD_SIMULATION set to 1 the state machine can be driven from the
   terminal (needed for Checkpoint 2):
     f          food lifted off the start position (Welcome -> SelectTime)
     p          food put back on the start position
     1 2 3      press time button 1, 2 or 3 (SelectTime -> Cooking)
     w e r t    the food reached plate LED 0, 1, 2 or 3

 History
 When           Who     What/Why
 -------------- ---     --------
 11/11/26       ---     first full version
 11/13/26       ---     integration: LED time per difficulty, matrix messages
****************************************************************************/
/*----------------------------- Include Files -----------------------------*/
#include "../ProjectHeaders/GameService.h"

#include <stdlib.h>

#include "ES_Configure.h"
#include "ES_Framework.h"
#include "ES_Port.h"
#include "terminal.h"
#include "dbprintf.h"

#include "LEDMatrixService.h"
#include "DisplayChainService.h"
#include "ActuatorService.h"

/*----------------------------- Module Defines ----------------------------*/
#define KEYBOARD_SIMULATION 1

#define NUM_PLATE_LEDS      4
#define NO_LED              0xFF
#define LED_FULL            255
#define LED_OFF             0

#define HITS_NEEDED         MATRIX_MAX_COOKED_LEVEL

#define ONE_SEC             1000
#define INACTIVITY_TIME_MS  20000     // spec: 20 s with no input resets the game
#define RESULT_TIME_MS      10000     // spec: result may show for at most 30 s

/*------------------------------ Module Types -----------------------------*/
typedef enum { Welcome = 0, SelectTime, Cooking, Won, Lost, ReturnFood } GameState_t;

/*---------------------------- Module Functions ---------------------------*/
static void postTo(bool (*PostFn)(ES_Event_t), ES_EventType_t Type, uint16_t Param);
static void enterWelcome(void);
static void enterReturnFood(void);
static void returnToIdle(void);
static void startGame(uint8_t Choice);
static void finishGame(bool PlayerWon);
static void lightNewLED(void);
static void turnOffLitLED(void);
static void stopGameTimers(void);
static void showMessage(DisplayMsg_t Message);
static ES_Event_t keyToEvent(ES_Event_t KeyEvent);

/*---------------------------- Module Variables ---------------------------*/
static uint8_t MyPriority;
static GameState_t CurrentState;

static uint8_t Difficulty = 0;          // 0 = none yet, otherwise 1, 2 or 3
static uint8_t GameSeconds = 0;
static uint8_t SecondsElapsed = 0;
static uint8_t CookedLevel = 0;
static uint8_t LitLED = NO_LED;
static bool FoodAtStart = true;         // assumes the food is in place at power-up

static const uint8_t TimeChoiceSeconds[3] = {30, 45, 60};   // button 1, 2, 3
// how long a plate LED stays lit, by difficulty (from Jeremy's easy/medium/hard)
static const uint16_t LEDOnTimeMs[3] = {2000, 1500, 1000};   // button 1, 2, 3

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

uint8_t GetDifficulty(void)
{
  return Difficulty;
}

uint8_t GetGameSeconds(void)
{
  return GameSeconds;
}

ES_Event_t RunGameService(ES_Event_t ThisEvent)
{
  ES_Event_t ReturnEvent;
  ReturnEvent.EventType = ES_NO_EVENT;

#if KEYBOARD_SIMULATION
  if (ES_NEW_KEY == ThisEvent.EventType)
  {
    ThisEvent = keyToEvent(ThisEvent);
    if (ES_NO_EVENT == ThisEvent.EventType)
    {
      return ReturnEvent;
    }
  }
#endif

  if (ES_INIT == ThisEvent.EventType)
  {
    DB_printf("GameService ready (service %d)\r\n", MyPriority);
    enterWelcome();
    return ReturnEvent;
  }

  if (ES_FOOD_REMOVED == ThisEvent.EventType)
  {
    FoodAtStart = false;
  }
  else if (ES_FOOD_PLACED == ThisEvent.EventType)
  {
    FoodAtStart = true;
  }

  switch (CurrentState)
  {
    case Welcome:
    {
      if (ES_FOOD_REMOVED == ThisEvent.EventType)
      {
        srand(ES_Timer_GetTime());                          // different LED order each game
        postTo(PostDisplayChainService, ES_CHAIN_FLASH_TIME_LEDS, 1);
        ES_Timer_InitTimer(GAME_INACTIVITY_TIMER, INACTIVITY_TIME_MS);
        showMessage(MSG_SET_TIME);
        CurrentState = SelectTime;
        DB_printf("Welcome -> SelectTime\r\n");
      }
    }
    break;

    case SelectTime:
    {
      if ((ES_TIME_SELECTED == ThisEvent.EventType) &&
          (ThisEvent.EventParam >= 1) && (ThisEvent.EventParam <= 3))
      {
        startGame((uint8_t)ThisEvent.EventParam);
      }
      else if (ES_USER_ACTIVITY == ThisEvent.EventType)
      {
        ES_Timer_InitTimer(GAME_INACTIVITY_TIMER, INACTIVITY_TIME_MS);
      }
      else if ((ES_TIMEOUT == ThisEvent.EventType) &&
               (GAME_INACTIVITY_TIMER == ThisEvent.EventParam))
      {
        DB_printf("No input for 20 s: leaving SelectTime\r\n");
        showMessage(MSG_TIMEOUT);
        returnToIdle();
      }
    }
    break;

    case Cooking:
    {
      switch (ThisEvent.EventType)
      {
        case ES_FOOD_PASSED:
        {
          // the food reached an LED: only the lit one counts
          if ((ThisEvent.EventParam == LitLED) && (CookedLevel < HITS_NEEDED))
          {
            ES_Timer_StopTimer(LED_ON_TIMER);
            turnOffLitLED();
            CookedLevel++;
            postTo(PostLEDMatrixService, ES_MATRIX_SET_COOKED, CookedLevel);
            ES_Timer_InitTimer(GAME_INACTIVITY_TIMER, INACTIVITY_TIME_MS);
            DB_printf("Hit! cooked level %d of %d\r\n", CookedLevel, HITS_NEEDED);
            if (CookedLevel >= HITS_NEEDED)
            {
              postTo(PostGameService, ES_COOKED, 0);
            }
            else
            {
              lightNewLED();
            }
          }
        }
        break;

        case ES_TIMEOUT:
        {
          if (LED_ON_TIMER == ThisEvent.EventParam)
          {
            // missed this one: it turns off and a new one lights
            // (a penalty for a miss is not decided yet)
            turnOffLitLED();
            lightNewLED();
          }
          else if (GAME_CLOCK_TIMER == ThisEvent.EventParam)
          {
            SecondsElapsed++;
            postTo(PostLEDMatrixService, ES_MATRIX_SHOW_CLOCK, SecondsElapsed);
            if (SecondsElapsed >= GameSeconds)
            {
              postTo(PostGameService, ES_GAME_CLOCK_ELAPSED, 0);
            }
            else
            {
              ES_Timer_InitTimer(GAME_CLOCK_TIMER, ONE_SEC);
            }
          }
          else if (GAME_INACTIVITY_TIMER == ThisEvent.EventParam)
          {
            DB_printf("No input for 20 s: leaving Cooking\r\n");
            showMessage(MSG_TIMEOUT);
            returnToIdle();
          }
        }
        break;

        case ES_USER_ACTIVITY:
        {
          // turning the knob is input even if the player hasn't hit an LED
          ES_Timer_InitTimer(GAME_INACTIVITY_TIMER, INACTIVITY_TIME_MS);
        }
        break;

        case ES_COOKED:
        {
          finishGame(true);
        }
        break;

        case ES_GAME_CLOCK_ELAPSED:
        {
          finishGame(false);
        }
        break;

        default:
        {}
        break;
      }
    }
    break;

    case Won:
    case Lost:
    {
      if ((ES_TIMEOUT == ThisEvent.EventType) &&
          (GAME_RESULT_TIMER == ThisEvent.EventParam))
      {
        DB_printf("Result done\r\n");
        returnToIdle();
      }
    }
    break;

    case ReturnFood:
    {
      // buttons and everything else are ignored until the food is back
      if (ES_FOOD_PLACED == ThisEvent.EventType)
      {
        enterWelcome();
      }
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
static void postTo(bool (*PostFn)(ES_Event_t), ES_EventType_t Type, uint16_t Param)
{
  ES_Event_t NewEvent;
  NewEvent.EventType = Type;
  NewEvent.EventParam = Param;
  PostFn(NewEvent);
}

static void stopGameTimers(void)
{
  ES_Timer_StopTimer(GAME_INACTIVITY_TIMER);
  ES_Timer_StopTimer(GAME_CLOCK_TIMER);
  ES_Timer_StopTimer(GAME_RESULT_TIMER);
  ES_Timer_StopTimer(LED_ON_TIMER);
}

// everything off and waiting for someone to start a game
static void enterWelcome(void)
{
  stopGameTimers();
  Difficulty = 0;
  GameSeconds = 0;
  SecondsElapsed = 0;
  CookedLevel = 0;
  LitLED = NO_LED;

  postTo(PostActuatorService, ES_PLATE_STOP, 0);
  postTo(PostDisplayChainService, ES_CHAIN_FLASH_TIME_LEDS, 0);
  postTo(PostDisplayChainService, ES_CHAIN_SET_ALL, LED_OFF);
  postTo(PostDisplayChainService, ES_CHAIN_SET_START_LEDS, 0);
  postTo(PostLEDMatrixService, ES_MATRIX_CLEAR, 0);
  showMessage(MSG_WELCOME);

  CurrentState = Welcome;
  DB_printf("State: Welcome\r\n");
}

// the game is over but the food is not back at its start position yet:
// light the 5 LEDs around that spot and wait for the food to be put back
static void enterReturnFood(void)
{
  stopGameTimers();
  Difficulty = 0;
  GameSeconds = 0;
  SecondsElapsed = 0;
  CookedLevel = 0;
  LitLED = NO_LED;

  postTo(PostActuatorService, ES_PLATE_STOP, 0);
  postTo(PostDisplayChainService, ES_CHAIN_FLASH_TIME_LEDS, 0);
  postTo(PostDisplayChainService, ES_CHAIN_SET_ALL, LED_OFF);
  postTo(PostDisplayChainService, ES_CHAIN_SET_START_LEDS, 1);
  // the matrix keeps showing the last message (DING!, TIME UP or TOO SLOW)

  CurrentState = ReturnFood;
  DB_printf("State: ReturnFood (put the food back)\r\n");
}

// every way a game can end (win, lose, timeout) comes through here
static void returnToIdle(void)
{
  if (FoodAtStart)
  {
    enterWelcome();
  }
  else
  {
    enterReturnFood();
  }
}

// choice is the button number: 1, 2 or 3. It is also the difficulty.
static void startGame(uint8_t Choice)
{
  Difficulty = Choice;
  GameSeconds = TimeChoiceSeconds[Choice - 1];
  SecondsElapsed = 0;
  CookedLevel = 0;
  LitLED = NO_LED;

  postTo(PostDisplayChainService, ES_CHAIN_FLASH_TIME_LEDS, 0);
  postTo(PostLEDMatrixService, ES_MATRIX_CLEAR, 0);
  postTo(PostLEDMatrixService, ES_MATRIX_SET_COOKED, 0);
  postTo(PostLEDMatrixService, ES_MATRIX_SHOW_CLOCK, 0);
  postTo(PostActuatorService, ES_PLATE_START, 0);

  lightNewLED();
  ES_Timer_InitTimer(GAME_CLOCK_TIMER, ONE_SEC);
  ES_Timer_InitTimer(GAME_INACTIVITY_TIMER, INACTIVITY_TIME_MS);

  CurrentState = Cooking;
  DB_printf("SelectTime -> Cooking: difficulty %d, %d seconds\r\n", Difficulty, GameSeconds);
}

static void finishGame(bool PlayerWon)
{
  stopGameTimers();
  postTo(PostActuatorService, ES_PLATE_STOP, 0);
  turnOffLitLED();

  if (PlayerWon)
  {
    postTo(PostActuatorService, ES_PLAY_DING, 0);
    postTo(PostActuatorService, ES_DISPENSE_TREAT, 0);
    showMessage(MSG_DONE);
    CurrentState = Won;
    DB_printf("Cooking -> Won\r\n");
  }
  else
  {
    showMessage(MSG_TIME_UP);
    CurrentState = Lost;
    DB_printf("Cooking -> Lost (cooked level %d of %d)\r\n", CookedLevel, HITS_NEEDED);
  }
  ES_Timer_InitTimer(GAME_RESULT_TIMER, RESULT_TIME_MS);
}

// pick a random plate LED that is not the one that was just lit, and light it
static void lightNewLED(void)
{
  uint8_t NewLED;
  do
  {
    NewLED = (uint8_t)(rand() % NUM_PLATE_LEDS);
  } while (NewLED == LitLED);

  LitLED = NewLED;
  postTo(PostDisplayChainService, ES_CHAIN_SET_LED, (uint16_t)((LitLED << 8) | LED_FULL));
  ES_Timer_InitTimer(LED_ON_TIMER, LEDOnTimeMs[Difficulty - 1]);
  DB_printf("LED %d lit\r\n", LitLED);
}

// what the timer display shows when it is not showing the clock
static void showMessage(DisplayMsg_t Message)
{
  postTo(PostLEDMatrixService, ES_MATRIX_SHOW_MSG, (uint16_t)Message);
}

static void turnOffLitLED(void)
{
  if (NO_LED != LitLED)
  {
    postTo(PostDisplayChainService, ES_CHAIN_SET_LED, (uint16_t)((LitLED << 8) | LED_OFF));
  }
}

// keyboard stand-ins for the real inputs
static ES_Event_t keyToEvent(ES_Event_t KeyEvent)
{
  ES_Event_t NewEvent;
  NewEvent.EventType = ES_NO_EVENT;
  NewEvent.EventParam = 0;

  switch (KeyEvent.EventParam)
  {
    case 'f': NewEvent.EventType = ES_FOOD_REMOVED; break;
    case 'p': NewEvent.EventType = ES_FOOD_PLACED;  break;
    case '1': NewEvent.EventType = ES_TIME_SELECTED; NewEvent.EventParam = 1; break;
    case '2': NewEvent.EventType = ES_TIME_SELECTED; NewEvent.EventParam = 2; break;
    case '3': NewEvent.EventType = ES_TIME_SELECTED; NewEvent.EventParam = 3; break;
    case 'w': NewEvent.EventType = ES_FOOD_PASSED;   NewEvent.EventParam = 0; break;
    case 'e': NewEvent.EventType = ES_FOOD_PASSED;   NewEvent.EventParam = 1; break;
    case 'r': NewEvent.EventType = ES_FOOD_PASSED;   NewEvent.EventParam = 2; break;
    case 't': NewEvent.EventType = ES_FOOD_PASSED;   NewEvent.EventParam = 3; break;
    default: break;
  }
  return NewEvent;
}
