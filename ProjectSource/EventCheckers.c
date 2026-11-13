/****************************************************************************
 Module
   EventCheckers.c

 Revision
   1.0.1

 Description
   This is the sample for writing event checkers along with the event
   checkers used in the basic framework test harness.

 Notes
   Note the use of static variables in sample event checker to detect
   ONLY transitions.

 History
 When           Who     What/Why
 -------------- ---     --------
 08/06/13 13:36 jec     initial version
****************************************************************************/

// this will pull in the symbolic definitions for events, which we will want
// to post in response to detecting events
#include "ES_Configure.h"
// This gets us the prototype for ES_PostAll
#include "ES_Framework.h"
// this will get us the structure definition for events, which we will need
// in order to post events in response to detecting events
#include "ES_Events.h"
// if you want to use distribution lists then you need those function
// definitions too.
#include "ES_PostList.h"
// This include will pull in all of the headers from the service modules
// providing the prototypes for all of the post functions
#include "ES_ServiceHeaders.h"
// this test harness for the framework references the serial routines that
// are defined in ES_Port.c
#include "ES_Port.h"
// include our own prototypes to insure consistency between header &
// actual functionsdefinition
#include "EventCheckers.h"

// This is the event checking function sample. It is not intended to be
// included in the module. It is only here as a sample to guide you in writing
// your own event checkers
#if 0
/****************************************************************************
 Function
   Check4Lock
 Parameters
   None
 Returns
   bool: true if a new event was detected
 Description
   Sample event checker grabbed from the simple lock state machine example
 Notes
   will not compile, sample only
 Author
   J. Edward Carryer, 08/06/13, 13:48
****************************************************************************/
bool Check4Lock(void)
{
  static uint8_t  LastPinState = 0;
  uint8_t         CurrentPinState;
  bool            ReturnVal = false;

  CurrentPinState = LOCK_PIN;
  // check for pin high AND different from last time
  // do the check for difference first so that you don't bother with a test
  // of a port/variable that is not going to matter, since it hasn't changed
  if ((CurrentPinState != LastPinState) &&
      (CurrentPinState == LOCK_PIN_HI)) // event detected, so post detected event
  {
    ES_Event ThisEvent;
    ThisEvent.EventType   = ES_LOCK;
    ThisEvent.EventParam  = 1;
    // this could be any of the service post functions, ES_PostListx or
    // ES_PostAll functions
    ES_PostAll(ThisEvent);
    ReturnVal = true;
  }
  LastPinState = CurrentPinState; // update the state for next time

  return ReturnVal;
}

#endif

/****************************************************************************
 Function
   Check4Keystroke
 Parameters
   None
 Returns
   bool: true if a new key was detected & posted
 Description
   checks to see if a new key from the keyboard is detected and, if so,
   retrieves the key and posts an ES_NewKey event to TestHarnessService0
 Notes
   The functions that actually check the serial hardware for characters
   and retrieve them are assumed to be in ES_Port.c
   Since we always retrieve the keystroke when we detect it, thus clearing the
   hardware flag that indicates that a new key is ready this event checker
   will only generate events on the arrival of new characters, even though we
   do not internally keep track of the last keystroke that we retrieved.
 Author
   J. Edward Carryer, 08/06/13, 13:48
****************************************************************************/
bool Check4Keystroke(void)
{
  if (IsNewKeyReady())   // new key waiting?
  {
    ES_Event_t ThisEvent;
    ThisEvent.EventType   = ES_NEW_KEY;
    ThisEvent.EventParam  = GetNewKey();
    ES_PostAll(ThisEvent);
    return true;
  }
  return false;
}

/***************************************************************************
 Input helpers: the pins are not assigned yet. When the pin map in the README
 is filled in, set up the pins (TRIS / ANSEL) and read them in these two
 functions. Nothing else in this file has to change.
***************************************************************************/
#define DEBOUNCE_MS             20

// true when the hall sensor sees the food at the start position
static bool readFoodSensor(void)
{
  return false;   // TODO: read the hall sensor pin
}

// bit 0 = time button 1, bit 1 = button 2, bit 2 = button 3; 1 = pressed
static uint8_t readTimeButtons(void)
{
  return 0;       // TODO: read the three button pins
}

/****************************************************************************
 Function
   Check4FoodSensor
 Description
   Watches the hall sensor at the food's start position. Posts
   ES_FOOD_REMOVED to GameService when the food is lifted off, and
   ES_FOOD_PLACED when it is put back. Owner: game-logic.
****************************************************************************/
bool Check4FoodSensor(void)
{
  static bool Initialized = false;
  static bool LastFoodPresent = false;
  bool FoodPresent = readFoodSensor();
  ES_Event_t ThisEvent;

  if (!Initialized)
  {
    LastFoodPresent = FoodPresent;      // ignore whatever state we power up in
    Initialized = true;
    return false;
  }

  if (FoodPresent == LastFoodPresent)
  {
    return false;
  }

  LastFoodPresent = FoodPresent;
  ThisEvent.EventType = FoodPresent ? ES_FOOD_PLACED : ES_FOOD_REMOVED;
  ThisEvent.EventParam = 0;
  PostGameService(ThisEvent);
  return true;
}

/****************************************************************************
 Function
   Check4TimeButtons
 Description
   Debounces the three time buttons and posts ES_TIME_SELECTED to GameService
   with EventParam = the button number (1, 2 or 3). That number is also the
   difficulty. Owner: game-logic.
****************************************************************************/
bool Check4TimeButtons(void)
{
  static uint8_t LastReading = 0;
  static uint8_t StableState = 0;
  static uint16_t LastChangeTime = 0;
  uint8_t Reading = readTimeButtons();
  uint16_t Now = ES_Timer_GetTime();
  bool ReturnVal = false;

  if (Reading != LastReading)
  {
    LastReading = Reading;
    LastChangeTime = Now;
  }
  else if ((Reading != StableState) && ((uint16_t)(Now - LastChangeTime) >= DEBOUNCE_MS))
  {
    uint8_t NewlyPressed = Reading & (uint8_t)(~StableState);
    StableState = Reading;
    if (NewlyPressed)
    {
      ES_Event_t ThisEvent;
      ThisEvent.EventType = ES_TIME_SELECTED;
      ThisEvent.EventParam = (NewlyPressed & 0x01) ? 1 : ((NewlyPressed & 0x02) ? 2 : 3);
      PostGameService(ThisEvent);
      ReturnVal = true;
    }
  }
  return ReturnVal;
}
