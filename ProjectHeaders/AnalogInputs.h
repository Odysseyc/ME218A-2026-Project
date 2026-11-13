/****************************************************************************

  Header file for AnalogInputs
  Everything read with the ADC (schematic: INPUTS):
    Hall North  RB2 / AN4      Hall East        RB3 / AN5
    Hall South  RA0 / AN0      Hall West        RA1 / AN1
    Hall Food Start RB13 / AN11   Pot Wiper     RB12 / AN12

 ****************************************************************************/

#ifndef AnalogInputs_H
#define AnalogInputs_H

#include <stdint.h>
#include <stdbool.h>

// Plate LED / Hall sensor index used everywhere (ES_FOOD_PASSED, ES_CHAIN_SET_LED):
// 0 = North, 1 = East, 2 = South, 3 = West
#define HALL_NORTH 0
#define HALL_EAST  1
#define HALL_SOUTH 2
#define HALL_WEST  3

// Call once from main() before ES_Initialize. Sets up the pins and the ADC
// and starts the Hall baseline calibration.
void InitAnalogInputs(void);

// Event checker (in EVENT_CHECK_LIST). Reads the next ADC channel and posts
// ES_FOOD_PASSED to GameService (EventParam = sensor index) when the magnet
// under the food is at a plate Hall sensor.
bool Check4PlateHalls(void);

// true when the Hall sensor at the food's start spot sees the magnet.
// Reports true until the baselines have been measured (the game assumes the
// food starts on its spot).
bool IsFoodAtStart(void);

// latest potentiometer reading, 0 (one end) to 1023 (other end)
uint16_t GetPotReading(void);

#endif /* AnalogInputs_H */
