# Karl's Kitchen: Team 3 (ME 218a, Fall 2026)

Our CHEF is a microwave game, a bit like whack-a-mole. The player picks up the food, chooses a cook time, then
turns a knob to spin the plate toward whichever LED lights up. Reach it before it turns off and the food cooks a
little more. Fill the cooking meter before time runs out and the microwave dings and drops a TREAT.

Built on a PIC32MX170F256B and the ME218 Events & Services Framework.

Team: Christopher Luey, Jeremy Merritt, Alison Peckham, Adam Sabet

## How to play

1. The food sits on its start spot. Lifting it off wakes the game up.
2. The LEDs by the three time buttons flash. Press one: 30, 45 or 60 seconds. That button also sets the difficulty (1, 2 or 3).
3. One of the 4 LEDs around the plate lights at random.
4. Turn the potentiometer to spin the plate until the food reaches that LED, before the LED turns off.
5. When the food gets there, the LED goes off and the cooked-level display on the LED matrix fills up a bit. Then another random LED lights.
6. Fill the matrix before the game clock runs out and you win: the microwave dings and a TREAT is dispensed. Run out of time and you lose.
7. When the game ends, the 4 LEDs around the food's start spot light up. Put the food back to reset. The buttons do nothing until it's back.
8. If nobody touches anything for 20 seconds, the game resets.

What happens when the player misses an LED (it turns off before they reach it) isn't decided yet. Right now a new LED just lights.

## Meeting the project spec

| Requirement | How we do it |
|---|---|
| 3 or more inputs | Hall sensors (food position), potentiometer, time buttons |
| Analog input | The potentiometer angle sets where the plate points |
| Non-contact sensing | Hall sensors |
| 3 or more kinds of feedback | LEDs and LED matrix, buzzer ding, plate servo, TREAT |
| Moving parts | Motor-driven plate and a servo TREAT dispenser |
| Time display | Game clock shown on the LED matrix |
| 20 second timeout | Inactivity timer resets the game |

## How the system is organized

Four services, each owned by one teammate. `GameService` makes all the decisions and tells the others what to show or move.

![System overview: GameService sends commands to the LED matrix, plate LED chain and actuator services](docs/system_overview.png)

### Game states

| State | What's happening | Moves on when |
|---|---|---|
| Welcome | Idle, waiting for someone to start | Food lifted off (`ES_FOOD_REMOVED`) |
| SelectTime | Time-button LEDs flash | A button is pressed (`ES_TIME_SELECTED`), or 20 s with no input |
| Cooking | Random LEDs, plate spinning, clock running | Matrix full (`ES_COOKED`) or clock out (`ES_GAME_CLOCK_ELAPSED`) or 20 s with no input |
| Won | Ding, TREAT, result shown for 10 s | The result timer ends |
| Lost | Result shown for 10 s | The result timer ends |
| ReturnFood | The 4 start LEDs are lit. Everything else is ignored | Food put back (`ES_FOOD_PLACED`) |

After Won, Lost or a timeout, the game goes to ReturnFood if the food isn't on its start spot, or straight to Welcome if it is.
The original hand-drawn diagram is in `docs/Checkpoint_1.pdf`.

## What each teammate needs to know

Each owner works in their own service. Don't edit `ES_Configure.h` on your branch (see "Working with Git").

The 4 plate LEDs and the 4 Hall sensors next to them use the same numbers everywhere:
0 = North, 1 = East, 2 = South, 3 = West.

### LED matrix (`LEDMatrixService`)

You receive:

| Event | Parameter | What to do |
|---|---|---|
| `ES_MATRIX_CLEAR` | none | Blank the matrix |
| `ES_MATRIX_SET_COOKED` | cooked level, 0 to `MATRIX_MAX_COOKED_LEVEL` | Show how full the cooking meter is |
| `ES_MATRIX_SHOW_CLOCK` | seconds elapsed | Show the passing of game time (shown as time left) |
| `ES_MATRIX_SHOW_MSG` | a `DisplayMsg_t` from `LEDMatrixService.h` | Show a message on the timer display |

`GameService` shows `MSG_WELCOME` in Welcome, `MSG_SET_TIME` in SelectTime, `MSG_DONE` on a win, `MSG_TIME_UP` on a loss and
`MSG_TIMEOUT` after 20 s with no input. The message stays up during ReturnFood.

You can ask `GameService` for these (include `GameService.h`):
- `GetDifficulty()` returns 1, 2 or 3 (which time button was pressed), or 0 if no game has started.
- `GetGameSeconds()` returns the game length: 30, 45 or 60 (0 if no game).
- `MATRIX_MAX_COOKED_LEVEL` (in `ES_Configure.h`, currently 8) is how many hits fill the meter. It can be raised.

Your timer is `MATRIX_TIMER` (3). `ES_MATRIX_STEP_DONE` is yours to use inside your own service.

### Plate LEDs (`DisplayChainService`)

You receive:

| Event | Parameter | What to do |
|---|---|---|
| `ES_CHAIN_SET_ALL` | brightness 0 to 255 | Set every plate LED |
| `ES_CHAIN_SET_LED` | `(LED number << 8) \| brightness` | Set one plate LED. Numbers 0 to 3 are the plate LEDs. Brightness 0 is off |
| `ES_CHAIN_FLASH_TIME_LEDS` | 1 start, 0 stop | Flash the 3 LEDs next to the time buttons |
| `ES_CHAIN_SET_START_LEDS` | 1 on, 0 off | Light the 4 LEDs around the food's start spot ("put the food back") |

The plate LEDs are on one 74HC595 (bits 1 to 4 for LEDs 0 to 3). Bits 0, 5, 6 and 7 are free for the start-position and
time-button LEDs. Dimming, flashing and the start LEDs aren't wired up yet.
`GetDifficulty()` is available to you too. Your timer is `CHAIN_TIMER` (4).

### Actuators (`ActuatorService`)

You receive:

| Event | What to do |
|---|---|
| `ES_PLATE_START` | Start letting the potentiometer spin the plate |
| `ES_PLATE_STOP` | Stop the plate |
| `ES_PLAY_DING` | Sound the buzzer |
| `ES_DISPENSE_TREAT` | Run the TREAT dispenser |

Right now the plate is a DC motor on an H-bridge, run from the keyboard (`a` start, `j` forward, `k` reverse, `x` stop,
`?` status). `ES_PLATE_STOP` stops it. The potentiometer, dispenser and buzzer are still to do.

Your timers are `PLATE_SAMPLE_TIMER` (5) and `SERVO_TIMER` (6).

### Hall sensors (`HallSensors.c`)

`Check4PlateHalls` reads the 4 Hall sensors one at a time with the ADC and posts `ES_FOOD_PASSED` to `GameService` with the
sensor number (0 to 3) when the magnet under the food arrives. While the magnet stays there it repeats every 200 ms, so a
newly lit LED under the food still counts. `GameService` only counts it if that LED is the lit one. At power-up it measures
each sensor's resting value. If the magnet is sitting next to one sensor, that sensor's value is estimated from the other three.
`InitHallSensors()` is called from `main()`.

### Game logic (`GameService`, hall sensor, buttons)

`EventCheckers.c` reads the hall sensor and the three buttons. `Check4FoodSensor` posts `ES_FOOD_REMOVED` and `ES_FOOD_PLACED`.
`Check4TimeButtons` debounces and posts `ES_TIME_SELECTED` with the button number (1 to 3). The code is written. It needs
the pin numbers in two small functions once the pin map below is filled in.

## Events

| Event | Sent by | Received by | Parameter |
|---|---|---|---|
| `ES_FOOD_REMOVED` | `Check4FoodSensor` | `GameService` | none |
| `ES_FOOD_PLACED` | `Check4FoodSensor` | `GameService` | none |
| `ES_TIME_SELECTED` | `Check4TimeButtons` | `GameService` | button number 1 to 3 (also the difficulty) |
| `ES_FOOD_PASSED` | `Check4PlateHalls` | `GameService` | LED number 0 to 3 |
| `ES_COOKED`, `ES_GAME_CLOCK_ELAPSED` | `GameService` | `GameService` | none |
| `ES_MATRIX_CLEAR`, `ES_MATRIX_SET_COOKED`, `ES_MATRIX_SHOW_CLOCK`, `ES_MATRIX_SHOW_MSG` | `GameService` | `LEDMatrixService` | see above |
| `ES_CHAIN_SET_ALL`, `ES_CHAIN_SET_LED`, `ES_CHAIN_FLASH_TIME_LEDS`, `ES_CHAIN_SET_START_LEDS` | `GameService` | `DisplayChainService` | see above |
| `ES_PLATE_START`, `ES_PLATE_STOP`, `ES_PLAY_DING`, `ES_DISPENSE_TREAT` | `GameService` | `ActuatorService` | none |

Timers: 0 inactivity (20 s), 1 game clock, 2 result screen, 3 matrix, 4 plate LED chain, 5 plate sampling, 6 servo timing,
7 how long a plate LED stays lit. Timers 8 to 15 are free.

## Testing the game logic without hardware

Keyboard testing on the board (terminal at 115200 baud) and in the simulator use the same keys:

| Key | Does |
|---|---|
| `f` | food lifted off |
| `p` | food put back |
| `1` `2` `3` | time buttons |
| `w` `e` `r` `t` | food reached plate LED 0, 1, 2, 3 |

To run the simulator on a normal Linux computer (needs `gcc`):

```bash
./tools/host_sim/run_sim.sh
```

It also has keys for timers: `c` is one second of clock, `l` is the LED timer, `i` is the inactivity timer, `x` is the result timer.

## Pins

Check this table before wiring so two people don't pick the same pin. Claim a pin with a small pull request to `main`.

| Pin | Use | Owner |
|---|---|---|
| RA0 / AN0 | Hall sensor South | Jeremy |
| RA1 / AN1 | Hall sensor West | Jeremy |
| RA2 | Plate motor IN3 | Christopher |
| RA3 | Plate motor IN4 | Christopher |
| RA4 | free | |
| RB0 / RB1 | Snap programming (PGED1 / PGEC1) | |
| RB2 / AN4 | Hall sensor North | Jeremy |
| RB3 / AN5 | Hall sensor East | Jeremy |
| RB4 | SPI1 SS (LED matrix) | Alison |
| RB5 | SPI1 SDO (LED matrix) | Alison |
| RB6 / RB7 | Serial to the CP2102 (RX / TX) | |
| RB8 | 74HC595 SER (data) | Jeremy |
| RB9 | 74HC595 SRCLK (shift clock) | Jeremy |
| RB10 | 74HC595 RCLK (latch) | Jeremy |
| RB11 | Motor ENA (held low, channel A unused) | Christopher |
| RB12 / AN12 | free | |
| RB13 / AN11 | free | |
| RB14 | SPI1 SCK (LED matrix) | Alison |
| RB15 | Plate motor ENB | Christopher |

Still without pins: the potentiometer (needs an analog pin, so RB12 or RB13), the 3 time buttons, the food start-position
sensor, the buzzer and the dispenser servo. Only RA4, RB12 and RB13 are free (RB11 too, if ENA is tied low on the board),
so some of these have to share. Options: the 3 buttons on one analog pin through a resistor ladder, and the buzzer and the
LEDs on spare 595 outputs or a second 595.

## Working with Git

| Branch | Owner | Covers |
|---|---|---|
| `main` | everyone, by pull request | Always builds. Shared files |
| `game-logic` | Adam | `GameService`, `EventCheckers` |
| `actuators` | Christopher | `ActuatorService` |
| `led-matrix-display` | Alison | `LEDMatrixService`, SPI and matrix HAL files |
| `display-chain` | Jeremy | `DisplayChainService`, `HallSensors` |

- Work only on your own branch. Never push straight to `main`.
- Commit small and often, with a short message that says what changed.
- When something works, open a pull request into `main`. Someone else looks it over first.
- Before the pull request, run `git merge main` on your branch and make sure it still builds.
- `ES_Configure.h` already has every service, event and timer we need. If you need a new one, make a small pull request to `main` that adds just that line.

Repo layout: `FrameworkHeaders/` and `FrameworkSource/` (course framework), `ProjectHeaders/` and `ProjectSource/` (our code),
`tools/host_sim/` (simulator), `docs/` (spec, slides, checkpoint PDFs), `hardware/` (KiCad schematic and BOM).

## Build setup (XC32, MPLAB extension for VS Code)

Everyone builds on their own machine. The generated project files aren't in the repo because they contain paths specific to each computer.

1. Create a new MPLAB project (PIC32MX170F256B, Application, XC32) and delete its sample `main.c`.
2. Copy the four source folders from this repo into the project folder.
3. Add every `.c` file in `FrameworkSource` and `ProjectSource` to the project.
4. Add `FrameworkHeaders` and `ProjectHeaders` as include directories, in the project settings or with a `user.cmake` in
   `cmake/<project>/default/` and `cmake/<project>/default.production/`:
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
