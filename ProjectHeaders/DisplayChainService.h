/****************************************************************************

  Header file for DisplayChainService
  based on the Gen 2 Events and Services Framework

 ****************************************************************************/

#ifndef DisplayChainService_H
#define DisplayChainService_H

#include <stdint.h>
#include <stdbool.h>

#include "ES_Events.h"
#include "ES_Port.h"

// Public Function Prototypes

bool InitDisplayChainService(uint8_t Priority);
bool PostDisplayChainService(ES_Event_t ThisEvent);
ES_Event_t RunDisplayChainService(ES_Event_t ThisEvent);

#endif /* DisplayChainService_H */
