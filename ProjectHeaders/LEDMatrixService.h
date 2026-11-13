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

typedef enum
{
  MSG_BLANK = 0,
  MSG_WELCOME,
  MSG_SET_TIME,
  MSG_DONE,
  MSG_TIME_UP,
  MSG_TIMEOUT,
  NUM_DISPLAY_MSGS      // this is the count, not a message
} DisplayMsg_t;

#define COOK_LEVEL_MAX MATRIX_MAX_COOKED_LEVEL

#endif /* LEDMatrixService_H */
