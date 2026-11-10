# Karl's Kitchen: Team 3 (ME 218a, Fall 2026)

Our CHEF is a microwave game, a bit like whack-a-mole. The player slides the food in, picks a cook time,
then turns a knob to spin the plate toward whichever LED lights up. Reach it before it turns off and the food
cooks a little more. Fill the cooking meter before time runs out and the microwave dings and drops a TREAT.

Built on a PIC32MX170F256B and the ME218 Events & Services Framework.

Team: Christopher Luey, Jeremy Merritt, Alison Peckham, Adam Sabet

## How the game works

1. Slide the food into the starting spot. A hall sensor notices and the game wakes up.
2. Pick a cook time: 30, 45 or 60 seconds. The LEDs by the three buttons flash until one is pressed.
3. One of the 4 LEDs around the plate lights up at random.
4. Turn the potentiometer to spin the plate until the food reaches that LED, before the LED turns off.
5. When the food gets there, the LED goes off and the cooked-level display on the LED matrix fills up a bit.
   Then another random LED lights.
6. Fill the matrix before the game clock runs out and you win: the microwave dings and a TREAT is dispensed.
   Run out of time and you lose.
7. If nobody touches anything for 20 seconds, the game resets to the welcome screen.

What happens when the player misses an LED (it turns off before they reach it) isn't decided yet.

## Meeting the project spec

| Requirement | How we do it |
|---|---|
| 3 or more inputs | Hall sensors (food position), potentiometer, time-select buttons |
| Analog input | The potentiometer angle sets where the plate points |
| Non-contact sensing | Hall sensors |
| 3 or more kinds of feedback | LEDs and LED matrix, buzzer ding, plate servo, TREAT |
| Moving parts | Servo-driven plate and a servo TREAT dispenser |
| Time display | Game clock shown on the LED matrix |
| 20 second timeout | Inactivity timer sends us back to the welcome screen |

## Game states and events

States: Welcome, Select Time, Cooking, Won, Lost. The handwritten diagram in `docs/Checkpoint_1.pdf` is the
original. This table is the current version.

| Event | What it means | What happens |
|---|---|---|
| `ES_FOOD_REMOVED` | Food detected at the start spot (Welcome to Select Time) | Flash the LEDs by the timer buttons, start the 20 s inactivity timer |
| `ES_TIME_SELECTED` | A time button was pressed (Select Time to Cooking) | Stop the inactivity timer, start the game clock, clear the matrix, light the first random LED |
| `ES_FOOD_PASSED` | The food reached an LED (stay in Cooking) | If it's the lit LED: turn it off, fill the matrix a bit, light a new random LED |
| `ES_COOKED` | The matrix is full (Cooking to Won) | Stop the clocks and the plate, play the ding, dispense the TREAT |
| `ES_GAME_CLOCK_ELAPSED` | Time ran out (Cooking to Lost) | Stop the clocks and the plate |
| `ES_TIMEOUT` | A timer expired (inactivity, or the win/lose screen finished) | Back to Welcome |

The name `ES_FOOD_PASSED` is left over from the first design. It now means "food reached an LED". We can rename it later.

## Hardware

From the Checkpoint 1 BOM: the PIC32, two MAX7219 LED matrix modules (through a 74xxx244 level shifter), two 74xxx595
shift registers driving the LEDs, five hall sensors, two servos, a potentiometer, four switches, a buzzer (with a 2N3904),
and a CP2102 for serial debugging.

### Pins

Fill in the TBD rows before wiring so two people don't pick the same pin. Do it with a small pull request to `main`.

| Pin | Use |
|---|---|
| RA0 | SPI1 SS |
| RA1 | SPI1 SDO |
| RB14 | SPI1 SCK |
| RB6 / RB7 | Serial to the CP2102 (RX / TX) |
| RB0 / RB1 | Snap programming (PGED1 / PGEC1) |
| TBD | Potentiometer |
| TBD | Hall sensors |
| TBD | Timer buttons |
| TBD | Servos |
| TBD | Buzzer |
| TBD | 74xxx595 chain |

## Repo layout

```
FrameworkHeaders/   course framework headers (ES_Configure.h is shared, see below)
FrameworkSource/    course framework sources
ProjectHeaders/     service headers and EventCheckers.h
ProjectSource/      main.c, EventCheckers.c, and one .c file per service
docs/               spec, slides, checkpoint PDFs
hardware/           KiCad schematic and BOM
```

## Branches and who owns what

| Branch | Owner | What it covers | Files |
|---|---|---|---|
| `main` | everyone, through pull requests | Always builds. Shared files only | `ES_Configure.h`, `main.c`, this README |
| `game-logic` | Adam | Game state machine, random LED choice, hall sensor and button checkers | `GameService`, `EventCheckers` |
| `actuators` | Christopher | Potentiometer, plate servo, TREAT dispenser, buzzer, telling the game when the food reaches an LED | `ActuatorService` |
| `led-matrix-display` | TBD | LED matrix: cooked level and game clock | `LEDMatrixService` plus the SPI and matrix HAL files |
| `display-chain` | TBD | The 4 plate LEDs and the timer-button LEDs through the shift registers | `DisplayChainService` |

## How we use Git

- Work only on your own branch. Never push straight to `main`.
- Commit small and often, with a short message that says what changed.
- When something works, open a pull request into `main`. Someone else looks it over before it's merged.
- Before opening the pull request, run `git merge main` on your branch and make sure it still builds.
- Don't edit `ES_Configure.h` on your branch. See the next section.

### The shared file

`ES_Configure.h` already lists every service, event and timer we need, and each service has a stub that builds.
So nobody should have to touch it. If you need a new event, timer or service, make a small pull request to `main`
that adds just that line, merge it, and tell the group to run `git merge main`.

### Services

| Number | Service | Branch |
|---|---|---|
| 0 | `GameService` | `game-logic` |
| 1 | `LEDMatrixService` | `led-matrix-display` |
| 2 | `DisplayChainService` | `display-chain` |
| 3 | `ActuatorService` | `actuators` |

### Timers

| Timer | Name | Used for |
|---|---|---|
| 0 | `GAME_INACTIVITY_TIMER` | 20 s no-input timeout |
| 1 | `GAME_CLOCK_TIMER` | Game clock |
| 2 | `GAME_RESULT_TIMER` | How long the win or lose screen stays up |
| 3 | `MATRIX_TIMER` | LED matrix |
| 4 | `CHAIN_TIMER` | Flashing and brightness for the LED chain |
| 5 | `PLATE_SAMPLE_TIMER` | Reading the potentiometer |
| 6 | `SERVO_TIMER` | Dispenser and ding timing |
| 7 to 15 | free | The "LED turns off" timer will need one of these |

### Events between services

| Event | Sent by | Received by | Parameter |
|---|---|---|---|
| `ES_FOOD_REMOVED` | `Check4FoodRemoved` | `GameService` | none |
| `ES_TIME_SELECTED` | `Check4TimeButtons` | `GameService` | seconds (30, 45 or 60) |
| `ES_FOOD_PASSED` | `ActuatorService` | `GameService` | LED number (0 to 3) |
| `ES_COOKED` | `GameService` | `GameService` | none |
| `ES_GAME_CLOCK_ELAPSED` | `GameService` | `GameService` | none |
| `ES_MATRIX_CLEAR` | `GameService` | `LEDMatrixService` | none |
| `ES_MATRIX_SET_COOKED` | `GameService` | `LEDMatrixService` | cooked level, 0 is empty |
| `ES_MATRIX_SHOW_CLOCK` | `GameService` | `LEDMatrixService` | seconds elapsed |
| `ES_MATRIX_STEP_DONE` | `LEDMatrixService` | `LEDMatrixService` | none (internal) |
| `ES_CHAIN_SET_ALL` | `GameService` | `DisplayChainService` | brightness, 0 to 255 |
| `ES_CHAIN_SET_LED` | `GameService` | `DisplayChainService` | `(LED number << 8) \| brightness` (brightness 0 turns it off) |
| `ES_CHAIN_FLASH_TIME_LEDS` | `GameService` | `DisplayChainService` | 1 to start flashing, 0 to stop |
| `ES_PLATE_START` | `GameService` | `ActuatorService` | none |
| `ES_PLATE_STOP` | `GameService` | `ActuatorService` | none |
| `ES_PLAY_DING` | `GameService` | `ActuatorService` | none |
| `ES_DISPENSE_TREAT` | `GameService` | `ActuatorService` | none |

This is a first draft. If something doesn't fit what you're building, change it with a pull request.
The LED numbers 0 to 3 are the plate LEDs. The `display-chain` owner decides the numbers for the timer-button LEDs.

## Build setup (XC32, MPLAB extension for VS Code)

Everyone builds on their own machine. The generated project files aren't in the repo because they contain paths
specific to each computer.

1. Create a new MPLAB project (PIC32MX170F256B, Application, XC32) and delete its sample `main.c`.
2. Copy the four source folders from this repo into the project folder.
3. Add every `.c` file in `FrameworkSource` and `ProjectSource` to the project.
4. Add `FrameworkHeaders` and `ProjectHeaders` as include directories, in the project settings or with a
   `user.cmake` in `cmake/<project>/default/` and `cmake/<project>/default.production/`:
   ```cmake
   target_include_directories(<project>_default_default_XC32_compile PRIVATE
       "${CMAKE_CURRENT_SOURCE_DIR}/../../../FrameworkHeaders"
       "${CMAKE_CURRENT_SOURCE_DIR}/../../../ProjectHeaders")
   ```
5. The config bits are in the framework's `ES_Port.c`. Don't add a second set in your own files.
6. Build with Production.

## Schedule

| Date | What |
|---|---|
| 11/4 | Design review (done) |
| 11/8 | Checkpoint 1 submission. Team meeting 11/9 |
| 11/13 | Checkpoint 2: 3 inputs sensed, a state machine with 3 or more states run from the keyboard, one actuator working, updated schematic |
| 11/17 | Checkpoint 3: everything integrated, all actuators running |
| 11/18, 1 to 5 pm | Grading session (30 second pitch) |
| 11/18, 7 pm | Public presentation in the 550 Atrium |
| 11/30, 4 pm | Report draft (a hosted website) |
| 12/1, 11:30 am | Peer review of the partner team's report |
| 12/4, 5 pm | Final report |
