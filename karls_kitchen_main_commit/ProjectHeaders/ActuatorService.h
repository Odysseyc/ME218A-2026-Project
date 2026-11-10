/****************************************************************************

  Header file for ActuatorService
  based on the Gen 2 Events and Services Framework

 ****************************************************************************/

#ifndef ActuatorService_H
#define ActuatorService_H

#include <stdint.h>
#include <stdbool.h>

#include "ES_Events.h"
#include "ES_Port.h"

// Public Function Prototypes

bool InitActuatorService(uint8_t Priority);
bool PostActuatorService(ES_Event_t ThisEvent);
ES_Event_t RunActuatorService(ES_Event_t ThisEvent);

#endif /* ActuatorService_H */
