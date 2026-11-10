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

#endif /* GameService_H */
