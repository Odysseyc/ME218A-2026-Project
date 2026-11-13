/****************************************************************************

  Header file for GameService
  based on the Gen 2 Events and Services Framework

 ****************************************************************************/

#ifndef GameService_H
#define GameService_H

#include <stdint.h>
#include <stdbool.h>

#include "ES_Events.h"
#include "ES_Port.h"

// Public Function Prototypes

bool InitGameService(uint8_t Priority);
bool PostGameService(ES_Event_t ThisEvent);
ES_Event_t RunGameService(ES_Event_t ThisEvent);

/****************************************************************************
 Function
   GetDifficulty

 Returns
   uint8_t: 1, 2 or 3 depending on which time button was pressed (button 1,
   2 or 3). 0 means no button has been pressed yet in this game.

 Description
   Other services call this to find out which game mode was chosen.
****************************************************************************/
uint8_t GetDifficulty(void);

/****************************************************************************
 Function
   GetGameSeconds

 Returns
   uint8_t: length of the current game in seconds (30, 45 or 60), or 0 if
   no game has been started.
****************************************************************************/
uint8_t GetGameSeconds(void);

#endif /* GameService_H */
