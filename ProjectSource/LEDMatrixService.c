/****************************************************************************
 Module
   LEDMatrixService.c

 Description
   Drives the MAX7219 LED matrix modules: cooked level and the game-clock (elapsed time) display.

 Notes
   STUB created on main so every branch builds against the same service list.
   Owner branch: led-matrix-display. Only edit this file on your own branch.
   Do not edit ES_Configure.h on a feature branch (see the README).

 History
 When           Who     What/Why
 -------------- ---     --------
 11/09/26       ---     stub
****************************************************************************/
/*----------------------------- Include Files -----------------------------*/
#include "../ProjectHeaders/LEDMatrixService.h"

#include "ES_Configure.h"
#include "ES_Framework.h"
#include "ES_Port.h"
#include "terminal.h"
#include "dbprintf.h"
#include "PIC32_SPI_HAL.h"
#include "DM_Display.h"
#include "GameService.h"

/*----------------------------- Module Defines ----------------------------*/
#define TIMER_DISPLAY       1   // the board wired to the PIC
#define COOKED_DISPLAY      2   // the board plugged into its DOUT

#define CLOCK_TEXT_LENGTH   4   // "M:SS"
#define ROW_ALL_ON          0xFFFFFFFFu
#define ROW_ALL_OFF         0x00000000u

#define COOK_BAR_FROM_BIT0 1
#define NARROW_ADVANCE 3
#define DISPLAY_COLS   32    // 4 modules x 8 columns
#define DISPLAY_ROWS   8
#define CHAR_WIDTH     4     // 4x6 font: 3 lit columns + 1 blank

/*------------------------------ Module Types -----------------------------*/
typedef enum { Idle, UpdatingDisplay } MatrixState_t;
typedef enum { ShowingTime, ShowingMessage } TimerContent_t;

/*---------------------------- Module Functions ---------------------------*/
static bool SaveRequest(ES_Event_t ThisEvent);
static bool AnythingChanged(void);
static void StartRefresh(void);
static void PostNextStep(void);
static void DrawTime(uint16_t Seconds);
static void DrawText(uint8_t WhichDisplay, const char *pText);
static void DrawCookBar(uint16_t Level);

/*---------------------------- Module Variables ---------------------------*/
static uint8_t MyPriority;
static MatrixState_t CurrentState;

// The newest request for each display. Requests only update these
// variables; the next refresh draws them into the frame buffer.
static TimerContent_t TimerDisplayContent = ShowingMessage;
static uint16_t              SecondsToShow = 0;
static DisplayMsg_t          MessageToShow = MSG_BLANK;
static bool                  TimerDisplayChanged = false;
 
static uint16_t              CookLevelToShow = 0;
static bool                  CookDisplayChanged = false;

static const char * const MessageText[NUM_DISPLAY_MSGS] =
{
  [MSG_BLANK]    = "",
  [MSG_WELCOME]  = "HUNGRY?",
  [MSG_SET_TIME] = "SET TIME",
  [MSG_DONE]     = "DING!",
  [MSG_TIME_UP]  = "TIME UP",
  [MSG_TIMEOUT]  = "TOO SLOW",
};

/*------------------------------ Module Code ------------------------------*/
bool InitLEDMatrixService(uint8_t Priority)
{
  DB_printf("InitLEDMatrixService\n");
  ES_Event_t ThisEvent;
  MyPriority = Priority;

  SPISetup_BasicConfig(SPI_SPI1);
  SPISetup_SetLeader(SPI_SPI1, SPI_SMP_MID);
  SPISetup_SetBitTime(SPI_SPI1, 10000);              // 10,000 ns -> BRG = 99 -> 100kHz
  SPISetup_MapSSOutput(SPI_SPI1, SPI_RPB4);
  SPISetup_MapSDOutput(SPI_SPI1, SPI_RPB5);
  SPISetup_SetClockIdleState(SPI_SPI1, SPI_CLK_HI);
  SPISetup_SetActiveEdge(SPI_SPI1, SPI_SECOND_EDGE);
  SPISetup_SetXferWidth(SPI_SPI1, SPI_16BIT);
  SPISetEnhancedBuffer(SPI_SPI1, true);
  SPISetup_EnableSPI(SPI_SPI1);
  SPIOperate_HasSS1_Risen();

  while (false == DM_TakeInitDisplayStep()) {}
  CurrentState = Idle;
  
  ThisEvent.EventType = ES_INIT;
  ThisEvent.EventParam = 0;
  return ES_PostToService(MyPriority, ThisEvent);
}

bool PostLEDMatrixService(ES_Event_t ThisEvent)
{
  return ES_PostToService(MyPriority, ThisEvent);
}

ES_Event_t RunLEDMatrixService(ES_Event_t ThisEvent)
{
  ES_Event_t ReturnEvent;
  ReturnEvent.EventType = ES_NO_EVENT;

  switch (CurrentState)
  {
    case Idle:
    {
      if (ES_INIT == ThisEvent.EventType)
      {
        DB_printf("LEDMatrixService ready (service %d)\r\n", MyPriority);
      }
      else if (true == SaveRequest(ThisEvent))
      {
        StartRefresh();
        CurrentState = UpdatingDisplay;
      }
    }
    break;

    case UpdatingDisplay:
    {
      if (ES_MATRIX_STEP_DONE == ThisEvent.EventType)
      {
        if (false == DM_TakeDisplayUpdateStep())   // sent a row, more to go
        {
          PostNextStep();
        }
        else if (true == AnythingChanged())              // all 8 rows sent, but the picture changed meanwhile
        {
          StartRefresh();
        }
        else                                       // all 8 rows sent and up to date
        {
          CurrentState = Idle;
        }
      }
      else
      {
        SaveRequest(ThisEvent);
      }
    }
    break;

    default:
    {}
    break;
  }

  return ReturnEvent;
}

/***************************************************************************
 private functions
 ***************************************************************************/

/****************************************************************************
 Function
   SaveRequest
 
 Parameters
   ES_Event_t : the event to check
 
 Returns
   bool : true if the event was a display request (and is now saved)
 
 Description
   Remembers the newest content for each display. Does not touch the frame
   buffer, so it is safe to call in the middle of a refresh.
****************************************************************************/
static bool SaveRequest(ES_Event_t ThisEvent)
{
  bool IsRequest = true;
 
  switch (ThisEvent.EventType)
  {
    case ES_MATRIX_CLEAR:
    {
      TimerDisplayContent = ShowingMessage;
      MessageToShow       = MSG_BLANK;
      TimerDisplayChanged = true;
      CookLevelToShow     = 0;
      CookDisplayChanged  = true;
    }
    break;
 
    case ES_MATRIX_SHOW_CLOCK:
    {
      uint8_t GameSeconds = GetGameSeconds();
      TimerDisplayContent = ShowingTime;

      if (ThisEvent.EventParam >= GameSeconds)
      {
        SecondsToShow = 0;
      }
      else
      {
        SecondsToShow = GameSeconds - ThisEvent.EventParam;
      }
      TimerDisplayChanged = true;
    }
    break;
 
    case ES_MATRIX_SHOW_MSG:
    {
      if (ThisEvent.EventParam < NUM_DISPLAY_MSGS)
      {
        TimerDisplayContent = ShowingMessage;
        MessageToShow = (DisplayMsg_t)ThisEvent.EventParam;
        TimerDisplayChanged = true;
      }
      else
      {
        IsRequest = false;
      }
    }
    break;

    case ES_MATRIX_SET_COOKED:
    {
      CookLevelToShow = ThisEvent.EventParam;
      CookDisplayChanged = true;
    }
    break;
 
    default:
    {
      IsRequest = false;
    }
    break;
  }
 
  return IsRequest;
}
 
/****************************************************************************
 Function
   AnythingChanged
 
 Returns
   bool : true if either display has a request that hasn't been drawn yet
****************************************************************************/
static bool AnythingChanged(void)
{
  return (TimerDisplayChanged || CookDisplayChanged);
}
 
/****************************************************************************
 Function
   StartRefresh
 
 Description
   Draws whatever changed into the frame buffer, then starts sending it to
   the displays. Only called when no refresh is in progress, so the buffer
   never changes halfway through being sent.
****************************************************************************/
static void StartRefresh(void)
{
  if (true == TimerDisplayChanged)
  {
    TimerDisplayChanged = false;
    if (ShowingTime == TimerDisplayContent)
    {
      DrawTime(SecondsToShow);
    }
    else
    {
      DrawText(TIMER_DISPLAY, MessageText[MessageToShow]);
    }
  }
 
  if (true == CookDisplayChanged)
  {
    CookDisplayChanged = false;
    DrawCookBar(CookLevelToShow);
  }
 
  PostNextStep();
}
 
/****************************************************************************
 Function
   PostNextStep
 
 Description
   Posts ES_MATRIX_STEP_DONE to ourselves to send the next row of the refresh.
****************************************************************************/
static void PostNextStep(void)
{
  ES_Event_t StepEvent;
  StepEvent.EventType  = ES_MATRIX_STEP_DONE;
  StepEvent.EventParam = 0;
 
  if (false == PostLEDMatrixService(StepEvent))
  {
    // If this post is lost, the refresh stalls. Make the queue bigger.
    DB_printf("DisplayService: queue full, refresh stalled\r\n");
  }
}
 
/****************************************************************************
 Function
   DrawTime
 
 Parameters
   uint16_t : seconds left
 
 Description
   Draws the time as M:SS on the timer display (1:00, 0:59, ... 0:00).
****************************************************************************/
static void DrawTime(uint16_t Seconds)
{
  char TimeText[5];    // "M:SS" plus the terminator
  uint8_t Minutes;
  uint8_t Secs;
 
  Minutes = (uint8_t)(Seconds / 60);
  Secs    = (uint8_t)(Seconds % 60);
 
  TimeText[0] = (char)('0' + Minutes);
  TimeText[1] = ':';
  TimeText[2] = (char)('0' + (Secs / 10));
  TimeText[3] = (char)('0' + (Secs % 10));
  TimeText[4] = '\0';
 
  // A local array is fine here: it's drawn before this function returns.
  // (The problem in the old code was a pointer outliving its array.)
  DrawText(TIMER_DISPLAY, TimeText);
}
 
/****************************************************************************
 Function
   DrawText
 
 Parameters
   uint8_t      : which display to draw on
   const char * : the text (up to 8 characters fit)
 
 Description
   Clears that display's buffer and writes the text the same way as in the labs:
   scroll left to make room, add the next character on the right. Then
   centers it. Characters advance 4 columns; ':' and the character after it
   advance 3, so a time reads "1:00" instead of "1 : 0 0".
****************************************************************************/
static void DrawText(uint8_t WhichDisplay, const char *pText)
{
  uint8_t Width = 0;    // columns used so far
  uint8_t Advance;
  uint8_t i;
 
  DM_ClearDisplayBuffer(WhichDisplay);
 
  for (i = 0; pText[i] != '\0'; i++)
  {
    Advance = CHAR_WIDTH;
    if ((i > 0) && ((':' == pText[i]) || (':' == pText[i - 1])))
    {
      Advance = NARROW_ADVANCE;
    }
 
    if ((Width + Advance) > DISPLAY_COLS)
    {
      break;  // out of room: drop the rest instead of pushing text off the left
    }
 
    if (i > 0)
    {
      DM_ScrollDisplayBuffer(Advance, WhichDisplay);  // make room on the right
    }
    DM_AddChar2DisplayBuffer((unsigned char)pText[i], WhichDisplay);
    Width += Advance;
  }
 
  // center whatever got drawn
  if (Width < DISPLAY_COLS)
  {
    DM_ScrollDisplayBuffer((uint8_t)((DISPLAY_COLS - Width) / 2), WhichDisplay);
  }
}
 
/****************************************************************************
 Function
   DrawCookBar
 
 Parameters
   uint16_t : cook level, 0 (raw) to COOK_LEVEL_MAX (done)
 
 Description
   Lights the same run of columns in all 8 rows, so the display (mounted on
   its side) shows a solid bar. The level is scaled to the 32 columns,
   rounding down, so the bar is only completely full at COOK_LEVEL_MAX.
****************************************************************************/
static void DrawCookBar(uint16_t Level)
{
  uint32_t ColsLit;
  uint32_t BarBits;
  uint8_t  Row;
 
  if (Level > COOK_LEVEL_MAX)
  {
    Level = COOK_LEVEL_MAX;
  }
 
  ColsLit = ((uint32_t)Level * DISPLAY_COLS) / COOK_LEVEL_MAX;
 
  if (0 == ColsLit)
  {
    BarBits = 0;
  }
  else if (ColsLit >= DISPLAY_COLS)
  {
    BarBits = 0xFFFFFFFF;  // its own case: shifting 32 bits by 32 is undefined in C
  }
  else
  {
#if COOK_BAR_FROM_BIT0
    BarBits = ((uint32_t)1 << ColsLit) - 1;          // lowest ColsLit bits
#else
    BarBits = ~((uint32_t)0xFFFFFFFF >> ColsLit);    // highest ColsLit bits
#endif
  }
 
  for (Row = 0; Row < DISPLAY_ROWS; Row++)
  {
    DM_PutDataIntoBufferRow(BarBits, Row, COOKED_DISPLAY);
  }
}
/*------------------------------ End of file ------------------------------*/
