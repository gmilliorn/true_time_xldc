//----------------------------------------------------------------------
// XL-DC Backplane Manager
// V1
// 2022_0925
// by Gary Milliorn
// gary@milliorn.org
//
// Used to reverse-engineer functions of the XL-DC TrueTime front-panel
// and bus, using a Mega2560 on a euro-card board.  No details on the
// system were found, so address bit order, names, etc. are wholly
// arbitrary.
//----------------------------------------------------------------------

#include <limits.h>


const char *name    = "Gary Milliorn";
const char *version = "XL-DC Manager";
const char *prompt  = "XM>";
char sbuf[100];
char last_cmd[100];


static int clku_show     = 1;
static int clku_tick     = 0;
static int clku_rate     = 1;

static int startup_tick  = 5;


//----------------------------------------------------------------------
//----------------------------------------------------------------------
int get_arg( char *s, char **ps, int base )
{
    int      v;

    for (; *s  &&  (isspace(*s)  ||  ispunct(*s)); s++)
        ;
    v = strtoul( s, ps, base );
    return( v );
}

int get_arg2c( char *s, char **ps, int base )
{
    int      v, n;
    for (; *s  &&  (isspace(*s)  ||  ispunct(*s)); s++)
        ;
    for (n = v= 0; n < 2; n++, s++)
        if (isdigit(*s))
            v = v * 10 + (*s - '0');
    *ps = s;
    return( v );
}


//======================================================================
// BACKPLANE INTERFACE
//======================================================================
// Backplane Interface Pins
// Mapping pins to the Eurocard interface.
//
static int BP_A[] = {  
    22,                             // A0  = 22 = PA0
    24,                             // A0  = 24 = PA2
    26,                             // A2  = 26 = PA4
    28,                             // A3  = 28 = PA6
    30,                             // A4  = 30 = PC7
    32,                             // A5  = 32 = PC5
    34,                             // A6  = 34 = PC3
    36,                             // A7  = 36 = PC1             
    38,                             // A8  = 38 = PD7
    40,                             // A9  = 40 = PG1
    42,                             // A10 = 42 = PL7
    44                              // A11 = 44 = PL5
};
#define BP_A_SIZE   (sizeof(BP_A)/sizeof(int))

static int BP_D[] = {
    37,                             // D0 = 37 = PC0
    35,                             // D1 = 35 = PC2
    33,                             // D2 = 33 = PC4
    31,                             // D3 = 31 = PC6
    29,                             // D4 = 29 = PA7
    27,                             // D5 = 27 = PA5
    25,                             // D6 = 25 = PA3
    23                              // D7 = 23 = PA1
};
#define BP_D_SIZE   (sizeof(BP_D)/sizeof(int))

static int BP_CS[] = {
    53,                             // CS0  = 53 = PB0
    51                              // CS1  = 51 = PB2
};
#define BP_CS_SIZE  (sizeof(BP_CS)/sizeof(int))

static int BP_CLK       =   39;     // CLK  = 39 = PG2
static int BP_IO_EN_B   =   46;     // EN_B = 46 = PL3

static int BP_RW        =   49;     // RW   = 49 = PL0
static int BP_RW_WR     =    0;     //      0 = DIR=0 = B->A = write
static int BP_RW_RD     =    1;     //      1 = DIR=1 = A->B = read

static int BP_PROBE_A   =   52;     // PA   = 52 = PB1

static int led          = 0;
static int clk_out      = 0;

static u32 clk_hz       = 0;
static u32 clk_tick     = 0;
static u32 clk_cnt      = 0;




void BP_SetAddrMode( void )
{
    int i;

    for (i = 0; i < BP_A_SIZE; i++) {
        if (BP_A[i] != 0) {
            pinMode( BP_A[i], OUTPUT );
            digitalWrite( BP_A[i], LOW );
        }
    }
}

void BP_SetAddr( int a )
{
    int i, b, aval;

    for (i = 0, aval = a; i < BP_A_SIZE; i++) {
        b = aval & 0x01;
        if (BP_A[i] != 0)
            digitalWrite( BP_A[i], b );
        aval >>= 1;
    }
}


//----------------------------------------------------------------------
// BP_SetDataMode -- set pin direction for R or W.
//                   <dir>  0 = input, 1 = output
//----------------------------------------------------------------------
void BP_SetDataMode( int dir )
{
    int i;

    for (i = 0; i < BP_D_SIZE; i++) {
        if (BP_D[i] != 0) {
            pinMode( BP_D[i], (dir) ? OUTPUT : INPUT );
            digitalWrite( BP_D[i], LOW );
        }
    }
}


//----------------------------------------------------------------------
// BP_SetData -- set data bus pin values.
//----------------------------------------------------------------------
void BP_SetData( int d )
{
    int i, b, dval;

    for (i = 0, dval = d; i < BP_D_SIZE; i++) {
        b = dval & 0x01;
        digitalWrite( BP_D[i], b == 0 ? LOW : HIGH );
        dval >>= 1;
    }
}


//----------------------------------------------------------------------
// BP_GetData -- get data bus pin values.
//----------------------------------------------------------------------
int BP_GetData( void )
{
    int i, b, dval;

    for (i = 0, dval = 0; i < BP_D_SIZE; i++) {
        b = digitalRead( BP_D[i] );
        dval = dval | (b << i);
    }
    return( dval );
}


//----------------------------------------------------------------------
// BP_SetCtlMode -- setup CS and RW pins.
//----------------------------------------------------------------------
void BP_SetCtlMode( void )
{
    int i;

    for (i = 0; i < BP_CS_SIZE; i++) {
        pinMode( BP_CS[i], OUTPUT );
        digitalWrite( BP_CS[i], HIGH );
    }

    pinMode( BP_CLK, OUTPUT );
    digitalWrite( BP_CLK, LOW );

    pinMode( BP_RW, OUTPUT );
    digitalWrite( BP_RW, LOW );

    pinMode( BP_IO_EN_B, OUTPUT );
    digitalWrite( BP_IO_EN_B, HIGH );
}


//----------------------------------------------------------------------
// BP_SetCS -- set CS + IO_EN pins for cycles to run.
//              CS and IO_EN pins are active-low.
//              Only one CS may be low.
//              If <-1> is passed, all are disabled.
//----------------------------------------------------------------------
void BP_SetCS( int cs, int lvl )
{
    int i, b, dval;

//    sprintf(sbuf, "set CS%1d=%d", cs, lvl);    Serial.println(sbuf);

    if (cs == -1) {
        digitalWrite( BP_CS[0],   HIGH );
        digitalWrite( BP_CS[1],   HIGH );
        digitalWrite( BP_IO_EN_B, HIGH );
    }
    if (cs == 0) {
        digitalWrite( BP_CS[0],     lvl == 0 ? LOW : HIGH );
        digitalWrite( BP_IO_EN_B,   lvl == 0 ? LOW : HIGH );
    }
    if (cs == 1) {
        digitalWrite( BP_CS[1],     lvl == 0 ? LOW : HIGH );
        digitalWrite( BP_IO_EN_B,   lvl == 0 ? LOW : HIGH );
    }
}


//----------------------------------------------------------------------
// BP_SetRW -- control bus RW pin.
//----------------------------------------------------------------------
void BP_SetRW( int rw )
{
    int b;

    b = (rw == 1) ? HIGH : LOW;
    digitalWrite( BP_RW, b );
}


//----------------------------------------------------------------------
// BP_WriteCycle -- do a write cycle to address with data.
//----------------------------------------------------------------------
void BP_WriteCycle( int cs, int addr, int data )
{
//    sprintf(sbuf, "Write: %02X", d); Serial.println(sbuf);

    BP_SetDataMode( 1 );

    BP_SetAddr( addr );
    BP_SetCS( cs, 0 );
    BP_SetRW( BP_RW_WR );

    delayMicroseconds(2);
    BP_SetData( data );

    BP_SetCS( cs, 1 );
    BP_SetDataMode( 0 );
}


//----------------------------------------------------------------------
// BP_ReadCycle -- do a read cycle from address.
//----------------------------------------------------------------------
int BP_ReadCycle( int cs, int addr )
{
    int d;

    BP_SetDataMode( 0 );

    BP_SetAddr( addr );
    BP_SetCS( cs, 0 );
    BP_SetRW( BP_RW_RD );

    delayMicroseconds(2);
    d = BP_GetData();

    BP_SetCS( cs, 1 );
    return( d );
}





//======================================================================
// Display Panel
//======================================================================
// The front panel of the TrueTime XL-DC is accessed with 8 IO addresses
// The GAL address decoder specifically looks for 0xFAx.

//----------------------------------------------------------------------
// Addresses.
//
#define DPADDR_LCD_CMD  0xFA0
#define DPADDR_LCD_DATA 0xFA1
#define DPADDR_KBD      0xFA2
#define DPADDR_CLK_DATA 0xFA3
#define DPADDR_CLK_SEND 0xFA4
#define DPADDR_CLK_STB  0xFA5
#define DPADDR_BL_ON    0xFA6
#define DPADDR_BL_OFF   0xFA7


//----------------------------------------------------------------------
// DP_ReadKBD -- read from keyboard.
//----------------------------------------------------------------------
int DP_ReadKBD( void )
{
    return( BP_ReadCycle( 0, DPADDR_KBD ) );
}


//----------------------------------------------------------------------
// DP_Backlight -- control backlight.
//----------------------------------------------------------------------
void DP_Backlight( int on_off )
{
    if (on_off == 1)
        BP_ReadCycle( 0, DPADDR_BL_ON );
    else
        BP_ReadCycle( 0, DPADDR_BL_OFF );
}


//======================================================================
// LCD Panel
//======================================================================

int LCD_row;
int LCD_col;


//----------------------------------------------------------------------
// LCD_Init -- initialize the 16x2 LCD panel.  There are a lot of 
//	       varieties of these, probably many similar.
//----------------------------------------------------------------------
void LCD_Init( int modes )
{
    int v;

//    v = (modes == -1) ? 0x07 : modes & 0x7;
    v = 7;
    LCD_row = 0;
    LCD_col = 0;

    delay( 2 );
    BP_WriteCycle( 0, 0xFA0, 0x01 );                // Clear display
    delay( 2 );
    BP_WriteCycle( 0, 0xFA0, 0x3C );                // 8 bit, 2 lines, 5x8 font
    BP_WriteCycle( 0, 0xFA0, 0x3C );                // "
    BP_WriteCycle( 0, 0xFA0, 0x3C );                // "
    delay( 2 );

// DISPLAY:
//        0 0 0 0 1 D C B
//                  | | +---- B=BLINK   ON/OFF
//                  | +------ C=CURSOR  ON/OFF
//                  +-------- D=DISPLAY ON/OFF
//
    BP_WriteCycle( 0, 0xFA0, 0x08 | v );
    delay( 2 );
    BP_WriteCycle( 0, 0xFA0, 0x06 );
    delay( 2 );
}


//----------------------------------------------------------------------
//----------------------------------------------------------------------
void LCD_Clear( void )
{
    delay( 2 );
    BP_WriteCycle( 0, 0xFA0, 0x01 );                // Clear display
    delay( 2 );
}


//----------------------------------------------------------------------
//----------------------------------------------------------------------
void LCD_RowCol( int row, int col )
{

    LCD_row = row;
    LCD_col = col;

    delay( 2 );

    if (row == 0)
        BP_WriteCycle( 0, 0xFA0, 0x80 + col );
    else
        BP_WriteCycle( 0, 0xFA0, 0xC0 + col );
    delay( 2 );
}


//----------------------------------------------------------------------
//----------------------------------------------------------------------
void LCD_Putc( char c )
{

    if (c == 0x08) {
        LCD_col = (LCD_col == 0) ? 0 : LCD_col - 1;
        LCD_RowCol( LCD_row, LCD_col );
        BP_WriteCycle( 0, 0xFA1, ' ' );
        LCD_RowCol( LCD_row, LCD_col );

    } else {        
        BP_WriteCycle( 0, 0xFA1, c );
        LCD_col += 1;
    }
}


//----------------------------------------------------------------------
//----------------------------------------------------------------------
void LCD_Puts( int row, int col, char *s )
{

    if (row != -1)
        LCD_RowCol( row, col );
    for (; *s; s++)
        LCD_Putc( *s );
}


//======================================================================
// Clock Panel
//======================================================================
// The TT XLDC clock LCD is a bit-mapped display basically controlled
// by a 96-bit shift register.
// Data is shifted MSB-first, which is why seconds are at the end.
// However the legends/colons/etc are everywhere.
//
//     <--A-->          <-----A----->
//     |     |          |\    |    /|
//     F     B          F H   I   J B
//     |     |          |   \ | /   | 
//     <--G-->          <-G1-> <-G2->
//     |     |          |   / | \   |
//     E     C          E K   L   M C
//     |     |          |/    |    \|
//     <--D-->          <-----D----->

// 0     0b0    ICON    "ALT"  
// 1     0b1    ICON    "FEET"  
// 2     0b2    ICON    "UNLOCK"  
// 3     0b3    ICON    "nS"  
// 4     0b4    ICON    "uS"  
// 5     0b5    ICON    "mS"  
// 6     0b6    ICON    "M"  
// 7     0b7    ICON    DMS ICONS + SH-SL PERIOD  
// 8     1b0    ALPHA   J  
// 9     1b1    ALPHA   G2  
// 10    1b2    ALPHA   M  
// 11    1b3    ALPHA   L  
// 12    1b4    ALPHA   K  
// 13    1b5    ALPHA   G1  
// 14    1b6    ICON    "LAT" 
// 15    1b7    ICON    "LONG"  
// 16    2b0    ALPHA   A  
// 17    2b1    ALPHA   B  
// 18    2b2    ALPHA   C  
// 19    2b3    ALPHA   D  
// 20    2b4    ALPHA   E  
// 21    2b5    ALPHA   F  
// 22    2b6    ALPHA   H  
// 23    2b7    ALPHA   I  
// 24    3b0    D1      A		day 100s
// 25    3b1    D1      B
// 26    3b2    D1      C
// 27    3b3    D1      D
// 28    3b4    D1      E
// 29    3b5    D1      F
// 30    3b6    D1      G
// 31    3b7    PERIOD  MH:ML
// 32    4b0    D2      A		day 10s
// 33    4b1    D2      B
// 34    4b2    D2      C
// 35    4b3    D2      D
// 36    4b4    D2      E
// 37    4b5    D2      F
// 38    4b6    D2      G
// 39    4b7    PERIOD  D1:D2
// 40    5b0    D3      A		day 1s
// 41    5b1    D3      B
// 42    5b2    D3      C
// 43    5b3    D3      D
// 44    5b4    D3      E
// 45    5b5    D3      F
// 46    5b6    D3      G
// 47    5b7    MINUS   LEADING
// 48    6b0    HH      A		hours high
// 49    6b1    HH      B
// 50    6b2    HH      C
// 51    6b3    HH      D
// 52    6b4    HH      E
// 53    6b5    HH      F
// 54    6b6    HH      G
// 55    6b7    COLON   LEADING
// 56    7b0    HL      A		hours low
// 57    7b1    HL      B
// 58    7b2    HL      C
// 59    7b3    HL      D
// 60    7b4    HL      E
// 61    7b5    HL      F
// 62    7b6    HL      G
// 63    7b7                       	blank?  nothing visible
// 64    8b0    MH      A		minutes high
// 65    8b1    MH      B
// 66    8b2    MH      C
// 67    8b3    MH      D
// 68    8b4    MH      E
// 69    8b5    MH      F
// 70    8b6    MH      G
// 71    9b7    PERIOD  D2:D3
// 72    9b0    ML      A		minutes low
// 73    9b1    ML      B
// 74    9b2    ML      C
// 75    9b3    ML      D
// 76    9b4    ML      E
// 77    9b5    ML      F
// 78    9b6    ML      G
// 79    9b7    DASH    H-M
// 80   10b0    SM      A		seconds high
// 81   10b1    SM      B
// 82   10b2    SM      C
// 83   10b3    SM      D
// 84   10b4    SM      E
// 85   10b5    SM      F
// 86   10b6    SM      G   
// 87   10b7    COLON   H-M
// 88   11b0    SL      A		seconds low
// 89   11b1    SL      B
// 90   11b2    SL      C
// 91   11b3    SL      D
// 92   11b4    SL      E
// 93   11b5    SL      F
// 94   11b6    SL      G
// 95   11b7    COLONS  DAY-H + M-S
 

// Buffer to build the data to shift out
// 
int  CLK_Image[12];


//--------------------------------------------------------------------------------
// CLK_DigitMap -- convert digit to 7-segment value.
//--------------------------------------------------------------------------------
int CLK_DigitMap( int d )
{
    switch (d) {    //     GFEDCBA
    case 0:     return  0b00111111;
    case 1:     return  0b00000110;
    case 2:     return  0b01011011;
    case 3:     return  0b01001111;
    case 4:     return  0b01100110;
    case 5:     return  0b01101101;
    case 6:     return  0b01111101;
    case 7:     return  0b00000111;
    case 8:     return  0b01111111;
    case 9:     return  0b01101111;
    default:    return  0b11111111;
    }
}


void CLK_Preset( int val )
{
    int i;

    for (i = 0; i < 12; i++)
        CLK_Image[i] = val;
}


//--------------------------------------------------------------------------------
// CLK_Send1 -- send one byte to the clock display.
//		optionally trigger an update to show the result, normally do that
//		once at the end.
//--------------------------------------------------------------------------------
void CLK_Send1( int v, int update )
{
    delayMicroseconds( 1 );
    BP_WriteCycle( 0, 0xFA3, v );
    delayMicroseconds( 1 );
    BP_WriteCycle( 0, 0xFA4, 0 );

    if (update) {
        delayMicroseconds( 1 );
        BP_WriteCycle( 0, 0xFA5, 0 );
    }
}


//--------------------------------------------------------------------------------
// CLK_Send -- send the generated image to the clock LCD and update it.
//--------------------------------------------------------------------------------
void CLK_Send( void )
{
    int i;

    CLK_Send1( 0x00, 0 );

    for (i = 0; i < 12; i++)
        CLK_Send1( CLK_Image[i], 0 );

    delayMicroseconds( 1 );
    BP_WriteCycle( 0, 0xFA5, 0 );
}


void CLK_Clear( void )
{
    CLK_Preset( 0 );
    CLK_Send();
}


//--------------------------------------------------------------------------------
// CLK_Set -- update LCD with the semblance of a clock.
//	      alas, not enough digits to do YY MM DD so <days>
//	      is Julian.
//--------------------------------------------------------------------------------
void CLK_Set( int flags, int days, int h, int m, int s )
{
    int d1, d2, d3;

    CLK_Preset( 0 );

// Suppress zeros for day.  If day == 0, completely off.
//
    d1 = days / 100;
    days = days % 100;
    d2 = days / 10;
    d3 = days % 10;
    if (d1 != 0)
        CLK_Image[ 3] = CLK_DigitMap( d1 );
    if (d1 + d2 != 0)
        CLK_Image[ 4] = CLK_DigitMap( d2 );
    if (d1 + d2 +d3 != 0)
        CLK_Image[ 5] = CLK_DigitMap( d3 );

// No zero suppress for clock.
//
    CLK_Image[ 6] = CLK_DigitMap( h / 10 );
    CLK_Image[ 7] = CLK_DigitMap( h % 10 );

    CLK_Image[ 8] = CLK_DigitMap( m / 10 );
    CLK_Image[ 9] = CLK_DigitMap( m % 10 );

    CLK_Image[10] = CLK_DigitMap( s / 10 );
    CLK_Image[11] = CLK_DigitMap( s % 10 );

    if (flags & 0x01) {
        CLK_Image[10] |= 0x80;      // Colons
        CLK_Image[11] |= 0x80;      // Colons
    }

    CLK_Send();
}


//======================================================================
// RTC -- fake SW-managed RTC.
//======================================================================
// Note: this is junk as I don't have an RTC module wired up, as the plan
// is to get that from a GPDSO.  Could be used for timezone or fallback,
// perhaps.

int RTC_D,  RTC_H,  RTC_M,  RTC_S;

void RTC_Set( int d, int h, int m, int s )
{
    if (d >= 0)
        RTC_D   =   d;
    if (h >= 0)
        RTC_H   =   h;
    if (m >= 0)
        RTC_M   =   m;
    if (s >= 0)
        RTC_S   =   s;
}


void RTC_Init( void )
{
    RTC_Set( 520, 19, 51, 00 );        // 139==May 19
}


//----------------------------------------------------------------------
// RTC_Increment -- Update RTC values.  Called outside interrupt handler
//                  or main loop.
//----------------------------------------------------------------------
void RTC_Increment( void )
{

    if (++RTC_S >= 60) {
        RTC_S = 0;
        if (++RTC_M >= 60) {
            RTC_M = 0;
            if (++RTC_H >= 24) {
                RTC_H = 0;
                RTC_D++;
            }
        }
    }                   
}


void RTC_Show( void )
{

    CLK_Set( 1, RTC_D, RTC_H, RTC_M, RTC_S );
}


//======================================================================
// KEYBOARD
//======================================================================
// The keyboard is a 74C923 4x4 keyboard scanner.
// Debouncing is not very good, needs to be replaced.

int key, prev_key;
long key_millis;
long key_debounce    = 100L;


#define KBD_UP      0x21
#define KBD_DOWN    0x22
#define KBD_LEFT    0x23
#define KBD_RIGHT   0x24
#define KBD_CLR     0x31
#define KBD_ENT     0x33
#define KBD_TIME    0x41
#define KBD_STAT    0x42
#define KBD_POS     0x43

typedef struct kb_info_st {
    u8           code;
    const char  *name;
} KB_INFO;

const KB_INFO kbd[] = {
    {   KBD_UP,     "UP"    },          // 0x00
    {   1,          "1"     },          // 0x01  
    {   2,          "2"     },          // 0x02  
    {   3,          "3"     },          // 0x03  
    {   KBD_DOWN,   "DN"    },          // 0x04
    {   4,          "4"     },          // 0x05  
    {   5,          "5"     },          // 0x06  
    {   6,          "6"     },          // 0x07  
    {   KBD_RIGHT,  "RT"    },          // 0x08
    {   7,          "7"     },          // 0x09  
    {   8,          "8"     },          // 0x0A  
    {   9,          "9"     },          // 0x0B  
    {   KBD_LEFT,   "LF"    },          // 0x0C
    {   KBD_CLR,    "CLR"   },          // 0x0D
    {   0,          "0"     },          // 0x0E  
    {   KBD_ENT,    "ENT"   },          // 0x0F
    {   0xFF,       "??"    },          // 0x10
    {   KBD_TIME,   "TIME"  },          // 0x11
    {   KBD_STAT,   "STAT"  },          // 0x12
    {   KBD_POS,    "POS"   }           // 0x13
};

static u32  db_cnt      = 0;
static u32  db_clk_hz   = 0;
static int  last_key    = -1;

#define KBD_BUF_SIZ     40
char    kbd_buf[KBD_BUF_SIZ];
int     kp;
int     kcontext;


//----------------------------------------------------------------------
// Command function codes.
//
#define KFN_BACKLIGHT   1
#define KFN_TIME        2
#define KFN_DAY         3
#define KFN_CLKRATE     4


//----------------------------------------------------------------------
//----------------------------------------------------------------------
void KBD_Init( void )
{
    kbd_buf[0] = 0;
    kp = 0;    
    kcontext    = 0;
}

    
//----------------------------------------------------------------------
// KBD_Map -- key was received (and debounced), process it.
//	      returns 0..9 for digits, else KBD_* for others.
//----------------------------------------------------------------------
int KBD_Map( int key )
{
    KB_INFO *k;
    int      c, cx;

    if (key & 0x80) {
        c = key & 0x1F; 
        k = &kbd[c];

        if (k->code > 9)
            return( k->code );
        if (kp >= KBD_BUF_SIZ-1)
            return( 0 );

        cx = k->code + '0';
        kbd_buf[ kp++ ] = cx;
        kbd_buf[ kp   ] = 0;
        LCD_Putc( cx );
    }
    return( 0 );
}


//----------------------------------------------------------------------
// KBD_GetFN_Name -- map function number to prompt string.
//----------------------------------------------------------------------
char *KBD_GetFN_Name( int fno )
{

    switch (fno) {
    case KFN_BACKLIGHT:     return( "BACKLIGHT=" );
    case KFN_TIME:          return( "TIME=" );
    case KFN_DAY:           return( "DAY=" );
    case KFN_CLKRATE:       return( "CLKRATE=" );
    default:                return( NULL );
    }
}


//----------------------------------------------------------------------
// KBD_DoFN -- execute completed front-panel command. 
//----------------------------------------------------------------------
int KBD_DoFN( int fno, char *args )
{
    char *s;
    int   v, x;

    switch (fno) {
        case KFN_BACKLIGHT:
            v = get_arg( args, &s, 10 );
            DP_Backlight( v );
            break;

        case KFN_TIME:
            v = get_arg2c( args, &s, 10 );
            x = get_arg2c( s, &s, 10 );
            RTC_Set( -1, v, x, 0 );
            RTC_Show();
            break;

        case KFN_DAY:
            v = get_arg( args, &s, 10 );
            RTC_Set( v, -1, -1, -1 );
            RTC_Show();
            break;

        case KFN_CLKRATE:
            clku_rate = get_arg( args, &s, 10 );
            break;
            
        default:
            return( -1 );
    }

    return( 0 );
}


//----------------------------------------------------------------------
//----------------------------------------------------------------------
void KBD_Do( int cmd )
{
    char *s;
    int   clr, upd, brst;

    brst = clr = upd = 0;

// TIME     -- toggle time display.
//
    if (cmd == KBD_TIME) {
        clku_show = 1 - clku_show;
        if (!clku_show)
            CLK_Clear();
        clr = brst = 1;
    }

    else if (cmd == KBD_CLR) {
        clr = brst = upd = 1;
        kcontext = 0;
    }

    else if (cmd == KBD_LEFT) {
        if (kp > 0)
            kbd_buf[ --kp ] = 0;
        LCD_Putc( 0x08 );
    }

    else if (cmd == KBD_ENT) {
        sprintf(sbuf, "  ENTER:"); Serial.println(sbuf);

        if (*kbd_buf) {
            // if <kcontext> was set, apply data to function selected.
            //
            if (kcontext) {
                sprintf(sbuf, "  ENTER: do fn#%d(%s)", kcontext, kbd_buf); Serial.println(sbuf);
                KBD_DoFN( kcontext, kbd_buf );
                clr = brst = upd = 1;
                kcontext = 0;
            }
            // else start context of selected command.
            else {
                kcontext = strtoul( kbd_buf, &s, 10 );
                sprintf(sbuf, "  ENTER: start fn %d", kcontext); Serial.println(sbuf);

                s = KBD_GetFN_Name( kcontext );
                if (s != NULL) {
                    LCD_Puts( 0, 0, s );
                    brst = 1;
                }
                else
                    upd = clr = brst = 1;
            }
        }

        // No data but enter, prompt for FN number.
        //
        else {
            LCD_Puts( 0, 0, "FN>" );
        }
    }

    else if (cmd == KBD_STAT) {
        DP_Backlight( 1 );
        LCD_Puts( 0, 0, version );
        LCD_Puts( 1, 0, name );
        brst = 1;        
    }
    
    else {
        sprintf(sbuf, "  kbdbuf  '%s'", kbd_buf); Serial.println(sbuf);
        sprintf(sbuf, "  code    %d", cmd); Serial.println(sbuf);
    }

    if (brst) {
        kp = 0;
        kbd_buf[ kp ] = 0;
    }
    if (clr)
       LCD_Clear();
    if (upd)
        LCD_Puts( 0, 0, kbd_buf );
}


//======================================================================
// SERIAL
//======================================================================

#define MAX_CMDLEN  40

char cmd_buf[MAX_CMDLEN];
int  cmd_len;


//----------------------------------------------------------------------
// command_prompt -- show prompt on serial port when ready to accept
//                   commands.  Also (re)initializes input.
//----------------------------------------------------------------------
int command_prompt()
{
  
    Serial.print(prompt);
    cmd_len = 0;
}


//----------------------------------------------------------------------
// command_run -- interpret the entered command.
//----------------------------------------------------------------------
int command_run()
{
    char         *s, *r;
    int           addr, c, d, e, last;
    KB_INFO      *k;
    unsigned long x;

// .        -- repeat last command.
//
    if (cmd_buf[0] == '.') {
        strcpy( cmd_buf, last_cmd );
    }


// led -- toggle arduino LED (just a validator)
//
    if (strcasecmp( cmd_buf, "led" ) == 0) {
        led = 1 - led;
        digitalWrite( 13, led ? HIGH : LOW );
    }

// info     -- show various settings.
//
    else if (strcasecmp( cmd_buf, "in" ) == 0) {
        sprintf(sbuf, "  k %02x  lastk %02x", key, last_key); Serial.println(sbuf);
        sprintf(sbuf, "  kbdbuf  '%s'", kbd_buf); Serial.println(sbuf);
        sprintf(sbuf, "  kcontext %d", kcontext); Serial.println(sbuf);
        sprintf(sbuf, "  clk     %lu Hz", clk_hz); Serial.println(sbuf);
        sprintf(sbuf, "  ticks   %lu",    clk_tick); Serial.println(sbuf);
    }


// cs 	-- set CS0/1 to value.
//
    else if (strncasecmp( cmd_buf, "cs", 2 ) == 0) {
        for (s = cmd_buf+2; *s  &&  isspace(*s); s++)
            ;
        c = strtoul( s, &r, 16 );
        for (s = r; *s  &&  isspace(*s); s++)
            ;
        d = strtoul( s, &r, 16 );
        sprintf(sbuf, "  CS%1d <= %1d", c, d); Serial.println(sbuf);
        BP_SetCS( c, d );
    }

// al -- cycle over address space values (no CS strobing) until key pressed.
//	 used to probe GAL decoding.
//
    else if (strcasecmp( cmd_buf, "al" ) == 0) {
        Serial.println("looping addr");
        addr = 0;
        BP_SetCS( 0, 0 );
        while (1) {
            if (Serial.available() >= 0) {
                c = Serial.read();
                if (c == ' ')
                    break;                    
            }
            BP_SetAddr( addr );
            delayMicroseconds(200);
            addr = (addr+1) & 0xFFF;
            sprintf(sbuf, "addr: %03X", addr); Serial.println(sbuf);
        }
        BP_SetCS( 0, 1 );
        Serial.println("done");
    }

// rl <a>   -- read address 1/ until key pressed.
//
    else if (strncmp( cmd_buf, "rl", 2 ) == 0) {
        for (s = cmd_buf+2; *s  &&  isspace(*s); s++)
            ;
        addr = strtoul( s, &r, 16 );
        while (1) {
            if (Serial.available() >= 0) {
                c = Serial.read();
                if (c == ' ')
                    break;                    
            }
            d = BP_ReadCycle( 0, addr );
            sprintf(sbuf, "  0x%03X = 0x%02X", addr, d); Serial.println(sbuf);
            delay(1000);
        }
    }

// wl <a>   -- write address with 0..FF until key pressed.
//
    else if (strncmp( cmd_buf, "wl", 2 ) == 0) {
        for (s = cmd_buf+2; *s  &&  isspace(*s); s++)
            ;
        addr = strtoul( s, &r, 16 );
        d = 0;
        while (1) {
            if (Serial.available() >= 0) {
                c = Serial.read();
                if (c == ' ')
                    break;                    
            }
            BP_WriteCycle( 0, addr, d );
            sprintf(sbuf, "  0x%03X <= 0x%02X", addr, d); Serial.println(sbuf);
            delay(100);
            d = (d + 1) & 0xFF;
        }
    }

// amap  	-- Seach for PR=0 across all address values.
//	    	   PR is a probe input, by wiring it to a GAL output the address can 
//	    	   be determined.
//
    else if (strcasecmp( cmd_buf, "amap" ) == 0) {
        Serial.println("  Responding Addresses:");
        addr = 0;
        BP_SetCS( 0, 0 );
        for (addr = 0; addr <= 0xFFF; addr++) {
            BP_SetAddr( addr );
            delayMicroseconds(200);
            c = digitalRead( BP_PROBE_A );
            if (c == 0) {
                sprintf(sbuf, "  0x%03X", addr, c); Serial.println(sbuf);
            }
        }
        BP_SetCS( 0, 1 );
    }

// pr		-- show value of probe pin.
//
    else if (strcasecmp( cmd_buf, "pr" ) == 0) {
        c = digitalRead( BP_PROBE_A );

        sprintf(sbuf, "PR_A=%d", c); Serial.println(sbuf);
    }

// r <a>        -- read from address
//
    else if (strncmp( cmd_buf, "r ", 2 ) == 0) {    
        for (s = cmd_buf+2; *s  &&  isspace(*s); s++)
            ;
        addr = strtoul( s, &r, 16 );
        d = BP_ReadCycle( 0, addr );
        sprintf(sbuf, "  0x%03X = 0x%02X", addr, d); Serial.println(sbuf);
    }

// w <a> <v>    -- write to address
//
    else if (strncmp( cmd_buf, "w ", 2 ) == 0) {    
        for (s = cmd_buf+2; *s  &&  isspace(*s); s++)
            ;
        addr = strtoul( s, &r, 16 );
        for (s = r; *s  &&  isspace(*s); s++)
            ;
        d = strtoul( s, &r, 16 );
        BP_WriteCycle( 0, addr, d );
        sprintf(sbuf, "  0x%04X <= 0x%02X", addr, d); Serial.println(sbuf);
    }

// ct           -- clock transmit value.
//
    else if (strncmp( cmd_buf, "ct", 2 ) == 0) {
        for (s = cmd_buf+2; *s  &&  isspace(*s); s++)
            ;
        d = strtoul( s, &r, 16 );
        CLK_Send1( d, 1 );
    } 

// cx n         -- send 12x bytes of zero with bit <n> set to one.
//
    else if (strncmp( cmd_buf, "cx", 2 ) == 0) {    
        for (s = cmd_buf+2; *s  &&  isspace(*s); s++)
            ;
        addr = strtoul( s, &r, 10 );
        if (addr >= 12*8) {
            Serial.println("error!");   
        } else {
            c = addr / 8;
            d = addr % 8;
            sprintf(sbuf, "  set bit %d in byte %d", d, c); Serial.println(sbuf);

            CLK_Preset(0);
            CLK_Image[c] = 1 << d;
            CLK_Send();
        }        
    }

// cf n         -- send 12x bytes of val.
//
    else if (strncmp( cmd_buf, "cf", 2 ) == 0) {    
        for (s = cmd_buf+2; *s  &&  isspace(*s); s++)
            ;
        d = strtoul( s, &r, 16 );
        CLK_Preset(d);
        CLK_Send();
    }

// cy n         -- 
//
    else if (strncmp( cmd_buf, "cy", 2 ) == 0) {   
        for (s = cmd_buf+2; *s  &&  isspace(*s); s++)
            ;
        d = strtoul( s, &r, 10 );
 
        CLK_Set( 0, 0, 0, 0, d );
    }  

// cz n         -- 
//
    else if (strncmp( cmd_buf, "cz", 2 ) == 0) {   
        RTC_Show();
    } 

// ti h m s     -- set time to values.
//
    else if (strncmp( cmd_buf, "ti", 2 ) == 0) {
        c = get_arg( cmd_buf + 2, &r, 10 );
        d = get_arg( r, &r, 10 );
        e = get_arg( r, &r, 10 );
 
        RTC_Set( -1, c, d, e );
        RTC_Show();
    }

// td d         -- set day to value.
//
    else if (strncmp( cmd_buf, "td", 2 ) == 0) {   
        c = get_arg( cmd_buf + 2, &r, 10 );
        RTC_Set( c, -1, -1, -1 );
        RTC_Show();
    }
                
// tr           -- set time update rate (seconds). 
//
    else if (strncmp( cmd_buf, "tr", 2 ) == 0) {   
        clku_rate = get_arg( cmd_buf + 2, &r, 10 );
    }

// ts           -- time show toggle 
//
    else if (strncmp( cmd_buf, "ts", 2 ) == 0) {   
        clku_show = 1 - clku_show;
    }

// li           -- LCD init / clear screen.
//
    else if (strncmp( cmd_buf, "li", 2 ) == 0) {
        c = get_arg( cmd_buf + 2, &r, 16 );
        LCD_Init( c );       
    }

// lt           -- LCD text.
//
    else if (strncmp( cmd_buf, "lt", 2 ) == 0) {     
        LCD_Init( -1 );  
        LCD_Puts( 0, 0, (char *) version );
        LCD_Puts( 1, 0, (char *) name );
    }

// kb           -- read keyboard until space pressed.
//
    else if (strncmp( cmd_buf, "kb", 2 ) == 0) {
        last = -1;
        while (1) {
            if (Serial.available() >= 0) {
                c = Serial.read();
                if (c == ' ')
                    break;                    
            }
            d = BP_ReadCycle( 0, 0xfa2 );
            if (d & 0x80) {
                if (d != last) {
                    if (d & 0x80) {
                        c = d & 0x1F;   
                        k = &kbd[c];        
                        sprintf(sbuf, "%02X '%s'", k->code, k->name); Serial.println(sbuf);
                    }
                }
            }
            last = d;            
       }
    }    

// bl <v>       -- set backlight on (1) or off (0).
//
    else if (strncmp( cmd_buf, "bl", 2 ) == 0) {
        for (s = cmd_buf+2; *s  &&  isspace(*s); s++)
            ;
        d = strtoul( s, &r, 16 );
        DP_Backlight( d & 1 );
    }     

    else {   
        sprintf(sbuf, "error: unknown '%s'", cmd_buf); Serial.println(sbuf);
    }

// Reissue prompt and start a new buffer.
//
    strcpy( last_cmd, cmd_buf );

    command_prompt();
    cmd_len = 0;
}


//----------------------------------------------------------------------
// command_get -- non-blocking, get a character from the serial port and
//                save/edit in a buffer, until CR/LF is seen.
//----------------------------------------------------------------------
int command_get()
{
   int c;
   
    while (Serial.available() > 0) {
        c = Serial.read();
        if (c == '\n'  ||  c == '\r') {
            Serial.write('\n');       
            Serial.write('\r');       
            command_run();
        }
    
        else if (c == 0x08  ||  c == 0x7F) {
            if (cmd_len > 0) {
                cmd_buf[ --cmd_len ] = 0;
                Serial.write(0x08);       
                Serial.write(' ');       
                Serial.write(0x08);       
            }
        }

        else if (cmd_len > MAX_CMDLEN)
            ;

        else {
            Serial.write(c);
            cmd_buf[ cmd_len++ ] = c;
        }
    }
    cmd_buf[ cmd_len ] = 0;
}


//======================================================================
//                   ARDUINO ENVIRONMENT
//======================================================================


ISR(TIMER2_COMPA_vect)
{ 

    clk_out   = 1 - clk_out;
    digitalWrite( BP_CLK, clk_out ? HIGH : LOW );

    if (++clk_cnt >= clk_hz-1L) {
        clk_tick++;
        clku_tick++;
        clk_cnt = 0;
    }
}


//----------------------------------------------------------------------
// setup -- setup everything.
//----------------------------------------------------------------------
void setup( void )
{

// Set timer2 interrupt to generate the free-running bus clock.
// This is relatively slow which affects display rate/update timing.
// Seems to be about the limit for this approach.

    cli();

    TCCR2A = 0;                     // set entire TCCR2A register to 0
    TCCR2B = 0;                     // same for TCCR2B
    TCNT2  = 0;                     //initialize counter value to 0

// Set compare match register for selected rates.
//
#define CLK_50KHZ
#ifdef CLK_8KHZ
    OCR2A   = 250-1;                // = (16*10^6) / (8000*8) - 1 (must be <256)
    clk_hz  = 8000L;
#endif
#ifdef CLK_50KHZ
    OCR2A   = 40-1;                 // 50 kHz
    clk_hz  = 50000L;
#endif
#ifdef CLK_75KHZ
    OCR2A   = 30-1;                 // 75 kHz
    clk_hz  = 75000L;
#endif
#ifdef CLK_100KHZ
    OCR2A   = 20-1;                 // 100 kHz
    clk_hz  = 100000L;
#endif

    TCCR2A |= (1 << WGM21);         // turn on CTC mode
    TCCR2B |= (1 << CS21);          // Set CS21 bit for 8 prescaler
    TIMSK2 |= (1 << OCIE2A);        // enable timer compare interrupt

    sei();

// GPIO
//
    BP_SetAddrMode();
    BP_SetDataMode( 0 );
    BP_SetCtlMode();
    BP_SetRW( 1 );

    pinMode( BP_PROBE_A, INPUT );
    pinMode( 13, OUTPUT );

// Other setup.
//
    RTC_Init();
    KBD_Init();
    BP_ReadCycle( 0, 0x0FA6 );  

// Open the serial port just for debugging.
//
    Serial.begin(9600);
    Serial.println("\n\n\n\r=============================================================");  
    Serial.println(version);  

    last_cmd[0] = 0;
    command_prompt();

    LCD_Init(-1);
    startup_tick = 3;
}



//----------------------------------------------------------------------
// loop -- 
//----------------------------------------------------------------------
void loop( void )
{
    int cmd;

    if (Serial.available() > 0)
        command_get();

// Check and debounce keyboard.
//
    key = DP_ReadKBD();
    if (key & 0x80) {                                       // key is pressed
        if (key != last_key) {                              // and is different
            if ((millis() - key_millis) > key_debounce) {   // debounce elapsed
                key_millis = millis();                      // note the time it changed
                if ((cmd = KBD_Map( key )) != 0)
                    KBD_Do( cmd );
                last_key = key;                             // remember the button state 
            }
        }
    }
    else
        last_key = -1;

// Software RTC clock tick.  <clk_tick> could be more than one, depending
// on how long commands take to complete.
//
    if (clk_tick) {
        clk_tick--;
        RTC_Increment();
        if (startup_tick) {
            startup_tick--;
            if (startup_tick == 2) {
                LCD_Puts( 0, 0, version );
                LCD_Puts( 1, 0, name );
            }
            if (startup_tick == 0)
                LCD_Init( -1 );
        } 
    }

// Clock update tick.
//
    if (clku_tick > clku_rate) {
        clku_tick = 0;
        if (clku_show)
            RTC_Show();
    }
}
