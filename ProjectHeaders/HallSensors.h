/****************************************************************************

  Header file for HallSensors
  Reads the 4 Hall sensors at the plate LEDs and reports when the food
  (magnet) arrives at one of them.

 ****************************************************************************/

#ifndef HallSensors_H
#define HallSensors_H

#include <stdint.h>
#include <stdbool.h>

// LED / sensor index used everywhere (ES_FOOD_PASSED, ES_CHAIN_SET_LED):
// 0 = North, 1 = East, 2 = South, 3 = West
#define HALL_NORTH 0
#define HALL_EAST  1
#define HALL_SOUTH 2
#define HALL_WEST  3

// Call once from main() before ES_Initialize. Sets up the pins and the ADC
// and starts the baseline calibration.
void InitHallSensors(void);

// Event checker (in EVENT_CHECK_LIST). Posts ES_FOOD_PASSED to GameService
// with EventParam = sensor index when the magnet arrives at a sensor.
bool Check4PlateHalls(void);

#endif /* HallSensors_H */
