/****************************************************************************

  Header file for LEDMatrixService
  based on the Gen 2 Events and Services Framework

 ****************************************************************************/

#ifndef LEDMatrixService_H
#define LEDMatrixService_H

#include <stdint.h>
#include <stdbool.h>

#include "ES_Events.h"
#include "ES_Port.h"

// Public Function Prototypes

bool InitLEDMatrixService(uint8_t Priority);
bool PostLEDMatrixService(ES_Event_t ThisEvent);
ES_Event_t RunLEDMatrixService(ES_Event_t ThisEvent);

#endif /* LEDMatrixService_H */
