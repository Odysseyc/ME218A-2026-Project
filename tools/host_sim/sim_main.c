/* Host-side simulator for GameService.
   Runs on a normal computer (no PIC32, no framework): the framework calls are
   replaced by the small stand-ins below. Type keys to drive the game.

   Game keys (same as on the board):
     f  food lifted off      p  food put back
     1 2 3  time buttons     w e r t  food reached LED 0 1 2 3
   Simulator-only keys (stand in for timers expiring):
     c  one second of game clock     l  the lit LED's timer ran out
     i  20 s inactivity timer        x  the result screen timer
     q  quit                         ?  show this help
*/
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include "ES_Configure.h"
#include "ES_Framework.h"
#include "ES_Timers.h"
#include "ES_Events.h"
#include "GameService.h"
#include "LEDMatrixService.h"
#include "DisplayChainService.h"
#include "ActuatorService.h"

static const char *names[] = {"NO_EVENT", "ERROR", "INIT", "TIMEOUT", "SHORT_TIMEOUT", "NEW_KEY",
  "FOOD_REMOVED", "FOOD_PLACED", "TIME_SELECTED", "FOOD_PASSED", "COOKED", "GAME_CLOCK_ELAPSED",
  "MATRIX_CLEAR", "MATRIX_SET_COOKED", "MATRIX_SHOW_CLOCK", "MATRIX_SHOW_MSG", "MATRIX_STEP_DONE",
  "CHAIN_SET_ALL", "CHAIN_SET_LED", "CHAIN_FLASH_TIME_LEDS", "CHAIN_SET_START_LEDS", "CHAIN_SET_BELL",
  "PLATE_START", "PLATE_STOP", "PLAY_DING", "DISPENSE_TREAT"};

static ES_Event_t queue[64];
static int head = 0, tail = 0;

bool ES_PostToService(uint8_t p, ES_Event_t e) { queue[tail++ % 64] = e; return true; }
static void show(const char *who, ES_Event_t e) { printf("      -> %-7s %s (param %u)\n", who, names[e.EventType], e.EventParam); }
bool PostLEDMatrixService(ES_Event_t e)    { show("matrix", e);  return true; }
bool PostDisplayChainService(ES_Event_t e) { show("chain", e);   return true; }
bool PostActuatorService(ES_Event_t e)     { show("actuator", e); return true; }
ES_TimerReturn_t ES_Timer_InitTimer(uint8_t n, uint16_t t) { printf("      timer %d started: %u ms\n", n, t); return 0; }
ES_TimerReturn_t ES_Timer_StopTimer(uint8_t n) { return 0; }
uint16_t ES_Timer_GetTime(void) { return 1234; }
int DB_printf(const char *f, ...) { va_list a; va_start(a, f); printf("   * "); vprintf(f, a); va_end(a); return 0; }

static void run(ES_EventType_t type, uint16_t param)
{
  ES_Event_t e; e.EventType = type; e.EventParam = param;
  RunGameService(e);
  while (head < tail) { RunGameService(queue[head++ % 64]); }   // events the service posted to itself
}

int main(void)
{
  int c;
  printf("GameService simulator. Type keys, then Enter. ? for help, q to quit.\n");
  InitGameService(0);
  while (head < tail) { RunGameService(queue[head++ % 64]); }
  while ((c = getchar()) != EOF)
  {
    if (c == '\n' || c == '\r' || c == ' ') continue;
    printf("\n[key %c]\n", c);
    switch (c)
    {
      case 'c': run(ES_TIMEOUT, GAME_CLOCK_TIMER); break;
      case 'l': run(ES_TIMEOUT, LED_ON_TIMER); break;
      case 'i': run(ES_TIMEOUT, GAME_INACTIVITY_TIMER); break;
      case 'x': run(ES_TIMEOUT, GAME_RESULT_TIMER); break;
      case 'q': return 0;
      case '?': printf("%s", "f food off | p food back | 1 2 3 buttons | w e r t LED 0-3 reached\nc clock tick | l LED timer | i inactivity | x result timer | q quit\n"); break;
      default: run(ES_NEW_KEY, (uint16_t)c); break;
    }
  }
  return 0;
}
