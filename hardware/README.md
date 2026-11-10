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
FrameworkHeaders/   course framework headers (ES_Configure.h lives here)
FrameworkSource/    course framework sources
ProjectHeaders/     our service and HAL headers
ProjectSource/      main.c, event checkers, our services and HALs
docs/               spec, design review slides, checkpoint PDFs, state diagram
hardware/           KiCad project (schematic) and BOM
```

## Branches

| Branch | Owns | Modules and services (proposed) |
|---|---|---|
| `main` | Always builds. Merge-only. Game logic and the shared framework files | Game state machine service, `main.c`, event checkers, `ES_Configure.h` |
| `actuators` | Everything that moves or makes sound | Plate servo (rotation driven by the potentiometer, rate limiting for "too fast"), TREAT dispenser servo, buzzer "ding" |
| `led-matrix-display` | The LED matrix modules | SPI HAL, `DM_Display`, `FontStuff`, matrix service (game clock, cooked level) |
| `display-chain` | The plate LED chain | 74xxx595 driver, per-LED brightness (dimming) service |

Rename a branch if your split is different. These names are a proposal.

### Workflow

- Work on your own branch. Do not push directly to `main`.
- Small commits with clear messages, for example `actuators: servo PWM on OC1`.
- Merge into `main` through a pull request. Someone else reviews it, and the build must pass first.
- Before merging, update your branch from `main` (`git merge main`) and rebuild.
- Do not commit generated build files (see `.gitignore`).

### Shared files (the usual conflict spots)

`ES_Configure.h` gets edited by every branch: services, events, timers. To avoid conflicts, agree on the
allocations below and each edit only your own lines.

| Service | Owner | Purpose |
|---|---|---|
| 0 | `main` | Game state machine |
| 1 | `led-matrix-display` | LED matrix service |
| 2 | `display-chain` | Plate LED chain service |
| 3 | `actuators` | Plate servo, dispenser, buzzer |

Events from the table above should be declared once, in `main`, and then used by every branch.

## Build notes (PIC32, XC32, MPLAB extension for VS Code)

Each person builds locally. The generated project files are machine-specific and are not in the repo.

1. Create a new MPLAB project (PIC32MX170F256B, Application, XC32) and delete its sample `main.c`.
2. Copy the four source folders from this repo into the project folder.
3. Do not build `ES_ShortTimer.c` or the framework template files.
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
