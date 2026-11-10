# Karl's Kitchen: Team 3 CHEF (ME 218a, Fall 2026)

A microwave-themed cooking game built on a PIC32MX170F256B and the ME218 Events & Services Framework.
The player slides a plate of "food" into the microwave, picks a cook time, then spins the plate with a
potentiometer. LEDs around the plate dim as the food passes them, and the player wins by dimming every
LED before the game clock runs out. Winning dings the microwave and dispenses a TREAT.

**Team 3:** Christopher Luey, Jeremy Merritt, Alison Peckham, Adam Sabet

## Gameplay

1. **Start:** slide the plate into the microwave. A hall sensor detects it, and all the plate LEDs come on.
2. **Pick a time:** 30, 45 or 60 seconds. The LEDs next to the three timer buttons flash to invite a press.
3. **Cook:** rotate the plate with the potentiometer. Each LED on the outer edge dims, following an
   exponentially decreasing curve, when the food passes close to it.
4. **Too fast:** if the player spins too quickly, the plate LEDs brighten again and the cooked level drops.
5. **Win:** every LED dimmed and the cooked-level LED matrix full before time runs out. The microwave dings
   and the TREAT is dispensed.
6. **Lose or idle:** if the game clock runs out, or there is no input for 20 s, the CHEF resets to the welcome mode.

## How it meets the project spec

| Requirement | How |
|---|---|
| 3+ distinct inputs | Hall sensors (plate position, non-contact), potentiometer (analog), push buttons (cook time) |
| Analog input used in an analog way | Potentiometer angle sets plate rotation and how far each LED dims (no simple threshold) |
| Non-contact sensing | Hall sensors |
| 3+ modes of feedback | LEDs and LED matrix (visual), buzzer ding (audio), plate rotation (electromechanical), TREAT |
| Electromechanical actuation | Servo-driven rotating plate, plus servo TREAT dispenser |
| Elapsed-time display | Game clock on the LED display |
| 20 s inactivity timeout | Inactivity timer returns the CHEF to the welcome mode |

## State machine (from Checkpoint 1, draft)

States: **Welcome**, **Select Time**, **Cooking**, **Won**, **Lost**. `docs/Checkpoint_1.pdf` is the source of truth.

| Event | Meaning | Response |
|---|---|---|
| `ES_FOOD_REMOVED` | Plate/food detected at the start position (Welcome to Select Time) | Flash the LEDs next to the timer buttons, start the 20 s inactivity timer |
| `ES_TIME_SELECTED` | A cook-time button was pressed (Select Time to Cooking) | Stop the inactivity timer, start the game clock (display and software), plate LEDs to full brightness, cooked-level matrix to lowest, dim the LEDs by the unchosen timer buttons and by the food's start position |
| `ES_FOOD_PASSED` | Food passed an LED (stay in Cooking) | Dim that LED, update the cooked level |
| `ES_TOO_FAST` | Plate spun too fast (stay in Cooking) | Brighten all LEDs |
| `ES_COOKED` | All LEDs dimmed (Cooking to Won) | Stop the game clock, inactivity timer and plate; play the "ding"; dispense the TREAT |
| `ES_GAME_CLOCK_ELAPSED` | Time ran out (Cooking to Lost) | Stop the game clock, inactivity timer and plate |
| `ES_TIMEOUT` | Inactivity or result-display timeout | Return to Welcome |

## Hardware (see the Checkpoint 1 BOM)

PIC32MX170F256B, 2 MAX7219 LED matrix modules (via a 74xxx244 level shifter), 2 x 74xxx595 shift registers driving
12 plate LEDs, 5 hall sensors, 2 servos, a potentiometer, 4 switches, a buzzer (2N3904 driver), and a CP2102 USB-serial
bridge for debug output.

### Pin map

Known allocations. Fill in the TBD rows **before** wiring so branches don't collide.

| Pin | Use |
|---|---|
| RA0 | SPI1 SS (SS1) |
| RA1 | SPI1 SDO (SDO1) |
| RB14 | SPI1 SCK (SCK1) |
| RB6 / RB7 | Framework serial (UART RX / TX to the CP2102) |
| RB0 / RB1 | PGED1 / PGEC1 (Snap programming) |
| MCLR | Reset, with a pull-up and the reset button |
| TBD | Potentiometer (analog input) |
| TBD | Hall sensors (5) |
| TBD | Timer buttons |
| TBD | Servos (2) |
| TBD | Buzzer |
| TBD | 74xxx595 chain (latch, enable) |

## Repository layout

```
FrameworkHeaders/   course framework headers (ES_Configure.h is shared, see below)
FrameworkSource/    course framework sources (ES_ShortTimer.c removed, it is not built)
ProjectHeaders/     service headers, EventCheckers.h
ProjectSource/      main.c, EventCheckers.c, one .c file per service
docs/               spec, design review slides, checkpoint PDFs, state diagram
hardware/           KiCad project (schematic) and BOM
```

## Branches

| Branch | Owner | Owns | Files |
|---|---|---|---|
| `main` | everyone (pull requests only) | Always builds. Shared files only | `ES_Configure.h`, `main.c`, this README |
| `game-logic` | TBD | Game state machine, hall sensor and button event checkers | `GameService.c/.h`, `EventCheckers.c/.h` |
| `led-matrix-display` | TBD | LED matrix: SPI HAL, `DM_Display`, `FontStuff`, cooked level and game-clock display | `LEDMatrixService.c/.h` plus HAL files |
| `display-chain` | TBD | Plate LED chain: 74xxx595 driver, per-LED dimming, flashing timer LEDs | `DisplayChainService.c/.h` |
| `actuators` | TBD | Potentiometer sampling, plate servo, TREAT dispenser, buzzer ding, `ES_FOOD_PASSED` / `ES_TOO_FAST` detection | `ActuatorService.c/.h` |

## Working rules

- Work on your own branch. Do not push directly to `main`.
- Small commits with clear messages, for example `actuators: servo PWM on OC1`.
- Merge into `main` through a pull request. Someone else reviews it and the build must pass first.
- Update your branch from `main` (`git merge main`) before opening a pull request.
- Do not commit generated build files (see `.gitignore`).

### Shared files: do not edit on a feature branch

`ES_Configure.h` is already set up on `main` with every service, event and timer the project needs, and each
service already exists as a stub that builds. Nobody should need to touch `ES_Configure.h` on their own branch.
If you need a new event, timer or service, open a small pull request to `main` that adds only that line, merge it,
and tell the others to pull it in. The same goes for the pin map below.

### Services

Higher service number means higher priority in the framework.

| Service | Name | Branch |
|---|---|---|
| 0 | `GameService` | `game-logic` |
| 1 | `LEDMatrixService` | `led-matrix-display` |
| 2 | `DisplayChainService` | `display-chain` |
| 3 | `ActuatorService` | `actuators` |

### Timers

| Timer | Name | Used by |
|---|---|---|
| 0 | `GAME_INACTIVITY_TIMER` | `GameService`, 20 s no-input timeout |
| 1 | `GAME_CLOCK_TIMER` | `GameService`, game clock |
| 2 | `GAME_RESULT_TIMER` | `GameService`, win/lose display time |
| 3 | `MATRIX_TIMER` | `LEDMatrixService` |
| 4 | `CHAIN_TIMER` | `DisplayChainService` |
| 5 | `PLATE_SAMPLE_TIMER` | `ActuatorService`, potentiometer sampling |
| 6 | `SERVO_TIMER` | `ActuatorService`, dispenser and ding durations |
| 7 to 15 | free | claim one with a pull request |

### Event contract

Every cross-service event, who posts it, who handles it, and what `EventParam` means.

| Event | Posted by | Handled by | `EventParam` |
|---|---|---|---|
| `ES_FOOD_REMOVED` | `Check4FoodRemoved` | `GameService` | unused |
| `ES_TIME_SELECTED` | `Check4TimeButtons` | `GameService` | seconds (30, 45 or 60) |
| `ES_FOOD_PASSED` | `ActuatorService` | `GameService` | LED index |
| `ES_TOO_FAST` | `ActuatorService` | `GameService` | unused |
| `ES_COOKED` | `GameService` (to itself) | `GameService` | unused |
| `ES_GAME_CLOCK_ELAPSED` | `GameService` (to itself) | `GameService` | unused |
| `ES_MATRIX_CLEAR` | `GameService` | `LEDMatrixService` | unused |
| `ES_MATRIX_SET_COOKED` | `GameService` | `LEDMatrixService` | cooked level, 0 (lowest) up |
| `ES_MATRIX_SHOW_CLOCK` | `GameService` | `LEDMatrixService` | seconds elapsed |
| `ES_MATRIX_STEP_DONE` | `LEDMatrixService` (to itself) | `LEDMatrixService` | unused |
| `ES_CHAIN_SET_ALL` | `GameService` | `DisplayChainService` | brightness 0 to 255 |
| `ES_CHAIN_SET_LED` | `GameService` | `DisplayChainService` | `(LED index << 8) \| brightness` |
| `ES_CHAIN_FLASH_TIME_LEDS` | `GameService` | `DisplayChainService` | 1 start, 0 stop |
| `ES_PLATE_START` | `GameService` | `ActuatorService` | unused |
| `ES_PLATE_STOP` | `GameService` | `ActuatorService` | unused |
| `ES_PLAY_DING` | `GameService` | `ActuatorService` | unused |
| `ES_DISPENSE_TREAT` | `GameService` | `ActuatorService` | unused |

Fill in the LED index numbering (which indices are plate LEDs and which are the timer-button LEDs) once the
`display-chain` owner has decided it. This event list is the team's first draft, so change it by pull request.

## Build notes (PIC32, XC32, MPLAB extension for VS Code)

Each person builds locally. The generated project files are machine-specific and are not in the repo.

1. Create a new MPLAB project (PIC32MX170F256B, Application, XC32) and delete its sample `main.c`.
2. Copy the four source folders from this repo into the project folder.
3. `ES_ShortTimer.c` and the framework template files have already been left out of this repo. Add every `.c` file in `FrameworkSource` and `ProjectSource` to the project.
4. Add the include directories `FrameworkHeaders` and `ProjectHeaders`, either in the project settings or with a
   `user.cmake` in `cmake/<project>/default/` and `cmake/<project>/default.production/`:
   ```cmake
   target_include_directories(<project>_default_default_XC32_compile PRIVATE
       "${CMAKE_CURRENT_SOURCE_DIR}/../../../FrameworkHeaders"
       "${CMAKE_CURRENT_SOURCE_DIR}/../../../ProjectHeaders")
   ```
5. The config bits live in the framework's `ES_Port.c`. Keep any HAL test-harness `main()` or config bits turned off.
6. Build with **Production**.

## Milestones

| Date | What |
|---|---|
| 11/4 | Design review (done) |
| 11/8 | Checkpoint 1 submission: KiCad schematic, state diagram, BOM. Team meeting 11/9 |
| 11/13 | Checkpoint 2: 3 inputs sensed, state machine (3+ states) driven by keyboard, one actuator working, up-to-date schematic |
| 11/17 | Checkpoint 3: everything integrated and all actuators running |
| 11/18, 1 to 5 pm | Grading session (30 s pitch) |
| 11/18, 7 pm | Public presentation in the 550 Atrium |
| 11/30, 4 pm | Report draft (hosted website) |
| 12/1, 11:30 am | Peer review of the partner team's report |
| 12/4, 5 pm | Final report |
