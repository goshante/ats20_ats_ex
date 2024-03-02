// ----------------------------------------------------------------------
// ATS_EX (Extended) Firmware for ATS-20 and ATS-20+ receivers.
// Based on PU2CLR sources.
// Inspired by closed-source swling.ru firmware.
// For more information check README file in my github repository:
// http://github.com/goshante/ats20_ats_ex
// ----------------------------------------------------------------------
// By Goshante
// 02.2024
// http://github.com/goshante
// ----------------------------------------------------------------------


#include <SI4735.h>
#include <EEPROM.h>
#include <Tiny4kOLED.h>
#include <PixelOperatorBold.h> 

#include "font14x24sevenSeg.h"
#include "Rotary.h"
#include "SimpleButton.h"
#include "patch_ssb_compressed.h"


const uint16_t size_content = sizeof ssb_patch_content;
const uint16_t cmd_0x15_size = sizeof cmd_0x15;

//Removed, but can be easy implemented. Required Si4735 chip, RDS is not supported in 32-34 chips
#define USE_RDS       0 
//On 1: Atm328p is running on full clock speed. 
//On 0: Atm328p is running on half clock speed. 
#define FASTER_CPU    1 

#define FM_BAND_TYPE  0
#define MW_BAND_TYPE  1
#define SW_BAND_TYPE  2
#define LW_BAND_TYPE  3

// OLED Const values
#define DEFAULT_FONT FONT8X16POB
#define RST_PIN -1
#define RESET_PIN 12

// Encoder
#define ENCODER_PIN_A 2
#define ENCODER_PIN_B 3

// Buttons
#define MODE_SWITCH       4 
#define BANDWIDTH_BUTTON  5
#define VOLUME_BUTTON     6
#define AVC_BUTTON        7
#define BAND_BUTTON       8 
#define SOFTMUTE_BUTTON   9
#define AGC_BUTTON       11
#define STEP_BUTTON      10

#define ENCODER_BUTTON   14

// Default values
#define MIN_ELAPSED_TIME 100
#define MIN_ELAPSED_RSSI_TIME 150
#define DEFAULT_VOLUME 25

// Modulations
#define FM  0
#define LSB 1
#define USB 2
#define AM  3
#define CW  4

//EEPROM Settings
#define STORE_TIME 10000 // Inactive time to save our settings
const uint8_t app_id = 43;
const int eeprom_address = 0;
long storeTime = millis();

bool ssbLoaded = false;
bool fmStereo = true;

bool cmdVolume = false;
bool cmdStep = false;
bool cmdBw = false;
bool cmdBand = false;
bool settingsActive = false;

bool displayOn = true;

uint8_t muteVolume = 0;

long countRSSI = 0;
int currentBFO = 0;

// Encoder buttons
SimpleButton  btn_Bandwidth(BANDWIDTH_BUTTON);
SimpleButton  btn_BandUp(BAND_BUTTON);
SimpleButton  btn_BandDn(SOFTMUTE_BUTTON);
SimpleButton  btn_VolumeUp(VOLUME_BUTTON);
SimpleButton  btn_VolumeDn(AVC_BUTTON);
SimpleButton  btn_Encoder(ENCODER_BUTTON);
SimpleButton  btn_AGC(AGC_BUTTON);
SimpleButton  btn_Step(STEP_BUTTON);
SimpleButton  btn_Mode(MODE_SWITCH);

volatile int encoderCount = 0;
long elapsedRSSI = millis();

//Frequency tracking
uint16_t currentFrequency;
uint16_t previousFrequency;

//For managing BW
struct Bandwidth
{
    uint8_t idx;      //Internal SI473X index
    const char* desc;
};

enum SettingType
{
    ZeroAuto,
    Num,
    Switch
};

struct SettingsItem
{
    char name[5];
    int8_t param;
    uint8_t type;
};

SettingsItem Settings[] =
{
    { "ATT", 0,  SettingType::ZeroAuto  },  //Attenuation
    { "SM ", 0,  SettingType::Num       },  //Soft Mute
    { "SVC", 1,  SettingType::Switch    },  //SSB AVC Switch
    { "Syn", 0,  SettingType::Switch    },  //SSB Sync
    { "DeE", 1,  SettingType::Switch    },  //FM DeEmphasis (0 - 50, 1 - 75)
    { "AVC", 46, SettingType::Num       },  //Automatic Volume Control
};

enum SettingsIndex
{
    ATT,
    SoftMute,
    SVC,
    Sync,
    DeEmp,
    AutoVolControl,
    SETTINGS_MAX
};
uint8_t SettingSelected = 0;
bool SettingEditing = false;

int8_t bwIdxSSB = 4;
Bandwidth bandwidthSSB[] =
{
    {4, "0.5k"},
    {5, "1.0k"},
    {0, "1.2k"},
    {1, "2.2k"},
    {2, "3.0k"},
    {3, "4.0k"}
};

int8_t bwIdxAM = 4;
const int maxFilterAM = 6;
Bandwidth bandwidthAM[] =
{
    {4, "1.0k"}, // 0
    {5, "1.8k"}, // 1
    {3, "2.0k"}, // 2
    {6, "2.5k"}, // 3
    {2, "3.0k"}, // 4 - Default
    {1, "4.0k"}, // 5
    {0, "6.0k"}  // 6
};

int8_t bwIdxFM = 0;
Bandwidth bandwidthFM[] =
{
    {0, "AUTO"},
    {1, "110k"},
    {2, " 84k"},
    {3, " 60k"},
    {4, " 40k"}
};

int tabStep[] =
{
    // AM in KHz
    1,
    5,
    9,
    10,
    50,
    100,
    1000,
    // SSB in Hz
    10,
    25,
    50,
    100
};
int amTotalSteps = 7;
int amTotalStepsSSB = 4; //Prevent large AM steps appear in SSB mode
int ssbTotalSteps = 3;

int tabStepFM[] =
{
    5,  // 50 KHz
    10, // 100 KHz
    100 // 1 MHz
};
uint16_t FMStepIdx = 1;


const int lastStepFM = (sizeof tabStepFM / sizeof(int)) - 1;
volatile int idxStep = 3;
void showStatus(bool basicUpdate = false, bool cleanFreq = false);
void applyBandConfiguration(bool extraSSBReset = false);

// Modulation
volatile uint8_t currentMode = FM;
const char* bandModeDesc[] = { "FM ", "LSB", "USB", "AM ", "CW " };
volatile uint8_t prevMode = FM;
uint8_t seekDirection = 1;

//Special logic for fast and responsive frequency surfing
uint32_t lastFreqChange = 0;
bool processFreqChange = 0;

bool isSSB()
{
    return currentMode == LSB || currentMode == USB || currentMode == CW;
}

int getSteps()
{
    if (isSSB())
    {
        if (idxStep >= amTotalSteps)
            return tabStep[idxStep];

        return tabStep[idxStep] * 1000;
    }

    if (idxStep >= amTotalSteps)
        idxStep = 0;

    return tabStep[idxStep];
}

int getLastStep()
{
    if (isSSB())
        return amTotalSteps + ssbTotalSteps - 1;

    return amTotalSteps - 1;
}


//Band table structure
struct Band
{
    uint8_t bandType;
    uint16_t minimumFreq;
    uint16_t maximumFreq;
    uint16_t currentFreq;
    uint16_t currentStepIdx;
    int8_t bandwidthIdx;     // Bandwidth table index (internal table in Si473x controller)
    char tag[3];
};

// Band settings
#define SW_LIMIT_LOW    1700
#define SW_LIMIT_HIGH   30000

Band band[] =
{
    {LW_BAND_TYPE, 149, 520, 300, 0, 4, "LW"},
    {MW_BAND_TYPE, 520, 1710, 1476, 3, 4, "MW"},
    {SW_BAND_TYPE, SW_LIMIT_LOW, 3500, 1900, 0, 4, "SW"},     // 160 Meter
    {SW_BAND_TYPE, 3500, 4500, 3700, 0, 5, "SW"},     // 80 Meter
    {SW_BAND_TYPE, 4500, 5600, 4850, 1, 4, "SW"},
    {SW_BAND_TYPE, 5600, 6800, 6000, 1, 4, "SW"},
    {SW_BAND_TYPE, 6800, 7300, 7100, 0, 4, "SW"},     // 40 Meter
    {SW_BAND_TYPE, 7200, 8500, 7200, 1, 4, "SW"},     // 41 Meter
    {SW_BAND_TYPE, 8500, 10000, 9604, 1, 4, "SW"},
    {SW_BAND_TYPE, 10000, 11200, 10100, 0, 4, "SW"},  // 30 Meter
    {SW_BAND_TYPE, 11200, 12500, 11940, 1, 4, "SW"},
    {SW_BAND_TYPE, 13400, 13900, 13600, 1, 4, "SW"},
    {SW_BAND_TYPE, 14000, 14500, 14200, 0, 4, "SW"},  // 20 Meter
    {SW_BAND_TYPE, 15000, 15900, 15300, 1, 4, "SW"},
    {SW_BAND_TYPE, 17200, 17900, 17600, 1, 4, "SW"},
    {SW_BAND_TYPE, 18000, 18300, 18100, 0, 4, "SW"},  // 17 Meter
    {SW_BAND_TYPE, 21000, 21400, 21200, 0, 4, "SW"},  // 15 Meter
    {SW_BAND_TYPE, 21400, 21900, 21500, 1, 4, "SW"},  // 13 Meter
    {SW_BAND_TYPE, 24890, 26200, 24940, 0, 4, "SW"},  // 12 Meter
    {SW_BAND_TYPE, 26200, 28000, 27500, 0, 4, "CB"},  // CB Band (11 Meter)
    {SW_BAND_TYPE, 28000, SW_LIMIT_HIGH, 28400, 0, 4, "SW"},  // 10 Meter
    {FM_BAND_TYPE, 6400, 10800, 7000, 1, 0, "  "},
    {FM_BAND_TYPE, 8400, 10800, 10570, 1, 0, "  "},
};

const int lastBand = (sizeof band / sizeof(Band)) - 1;
int bandIdx = 1;

uint8_t rssi = 0;
uint8_t stereo = 1;
uint8_t volume = DEFAULT_VOLUME;

Rotary encoder = Rotary(ENCODER_PIN_A, ENCODER_PIN_B);
SI4735 si4735;

// ---------------------
// ------- Utils -------
// ---------------------

const DCfont* LastFont = DEFAULT_FONT;
void oledSetFont(const DCfont* font)
{
    if (font && LastFont != font)
    {
        LastFont = font;
        oled.setFont(font);
    }
}

void oledPrint(const char* text, int offX = -1, int offY = -1, const DCfont* font = LastFont, bool invert = false)
{
    oledSetFont(font);
    if (invert)
        oled.invertOutput(invert);
    if (offX >= 0 && offY >= 0)
        oled.setCursor(offX, offY);
    oled.print(text);
    if (invert)
        oled.invertOutput(false);
}

void oledPrint(uint16_t u, int offX = -1, int offY = -1, const DCfont* font = LastFont, bool invert = false)
{
    oledSetFont(font);
    if (invert)
        oled.invertOutput(invert);
    if (offX >= 0 && offY >= 0)
        oled.setCursor(offX, offY);
    oled.print(u);
    if (invert)
        oled.invertOutput(false);
}

// --------------------------
// ------- Main logic -------
// --------------------------

//Initialize controller
void setup()
{
    //Clock speed configuration
    noInterrupts(); //cli()
    CLKPR = 0x80;   //Allow edit CLKPR register
#if FASTER_CPU
    CLKPR = 0;      //Full CPU clock speed
#else
    CLKPR = 1;      //Half CPU clock speed
#endif
    interrupts();   //sei()

    pinMode(13, OUTPUT);
#ifdef DEBUG
    Serial.begin(115200);
    Serial.write("[START]\n");
#ifdef DEBUG_BUTTONS_ONLY
    return;
#endif
#endif


    pinMode(ENCODER_PIN_A, INPUT_PULLUP);
    pinMode(ENCODER_PIN_B, INPUT_PULLUP);

    oled.begin(128, 64, sizeof(tiny4koled_init_128x64br), tiny4koled_init_128x64br);
    oled.clear();
    oled.on();
    oled.setFont(DEFAULT_FONT);

    if (digitalRead(ENCODER_BUTTON) == LOW)
    {
        oled.clear();
        saveAllReceiverInformation();
        EEPROM.write(eeprom_address, app_id);
        oled.setCursor(0, 0);
        oled.print("  EEPROM RESET");
        oled.setCursor(0, 2);
        for (uint8_t i = 0; i < 16; i++)
        {
            oled.print("-"); //Just fancy animation
            delay(60);
        }
        oled.clear();
    }
    else
    {
        oled.clear();
        oled.setCursor(0, 0);
        oled.invertOutput(true);
        oled.print("ATS-20 RECEIVER ");
        oled.invertOutput(false);
        oled.setCursor(0, 2);
        oled.print("  ATS_EX v1.00");
        oled.setCursor(0, 4);
        oled.print(" Goshante 2024\0");
        oled.setCursor(0, 6);
        oled.print("Source on github");
        delay(2000);
        oled.clear();
    }

    //Encoder interrupts
    attachInterrupt(digitalPinToInterrupt(ENCODER_PIN_A), rotaryEncoder, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ENCODER_PIN_B), rotaryEncoder, CHANGE);

    si4735.getDeviceI2CAddress(RESET_PIN);
    si4735.setup(RESET_PIN, MW_BAND_TYPE);
    si4735.setAvcAmMaxGain(48); //Maximum AM/SSB RF Gain

    delay(500);

    //Load settings from EEPROM
    if (EEPROM.read(eeprom_address) == app_id)
    {
        //Serial.write("read EEPROM\n");
        readAllReceiverInformation();
    }

    //Initialize current band settings and read frequency
    applyBandConfiguration();
    currentFrequency = previousFrequency = si4735.getFrequency();
    si4735.setVolume(volume);

    //Draw main screen
    oled.clear();
    showStatus();
}

#define ENCODER_MUTEDELAY          2
#define BANDWIDTH_DELAY            9 
#define STEP_DELAY                 6
#define BAND_DELAY                 2
#define VOLUME_DELAY               1 
#define AGC_DELAY                  1

#define buttonEvent                NULL


uint8_t volumeEvent(uint8_t event, uint8_t pin)
{
    if (muteVolume)
    {
        if (!BUTTONEVENT_ISDONE(event))
        {
            if ((BUTTONEVENT_SHORTPRESS != event) || (VOLUME_BUTTON == pin))
                doVolume(1);
        }
    }

#if (0 != VOLUME_DELAY)
#if (VOLUME_DELAY > 1)
    static uint8_t count;
    if (BUTTONEVENT_FIRSTLONGPRESS == event)
    {
        count = 0;
    }
#endif
    if (BUTTONEVENT_ISLONGPRESS(event))
        if (BUTTONEVENT_LONGPRESSDONE != event)
        {
#if (VOLUME_DELAY > 1)
            if (count++ == 0)
#endif
                doVolume(VOLUME_BUTTON == pin ? 1 : -1);
#if (VOLUME_DELAY > 1)
            count = count % VOLUME_DELAY;
#endif
        }
#else
    if (BUTTONEVENT_FIRSTLONGPRESS == event)
        event = BUTTONEVENT_SHORTPRESS;
#endif
    return event;
}

uint8_t encoderEvent(uint8_t event, uint8_t pin)
{
#if (0 != ENCODER_MUTEDELAY)
    static uint8_t waitBeforeMute;
    if (BUTTONEVENT_FIRSTLONGPRESS == event)
    {
        waitBeforeMute = ENCODER_MUTEDELAY;
    }
    else if ((BUTTONEVENT_LONGPRESS == event) && (0 != waitBeforeMute))
    {
        if (0 == --waitBeforeMute)
        {
            uint8_t x = muteVolume;
            muteVolume = si4735.getVolume();
            si4735.setVolume(x);
            showVolume();
        }
    }
#else
    if (BUTTONEVENT_FIRSTLONGPRESS == event)
        event = BUTTONEVENT_SHORTPRESS;
#endif
    return event;
}

uint8_t modeEvent(uint8_t event, uint8_t pin)
{
    if (BUTTONEVENT_FIRSTLONGPRESS == event)
        event = BUTTONEVENT_SHORTPRESS;
    return event;
}

uint8_t bandwidthEvent(uint8_t event, uint8_t pin)
{
#if (0 != BANDWIDTH_DELAY)
    if (BUTTONEVENT_ISLONGPRESS(event))
    {
        static uint8_t direction = 1;
        if (BUTTONEVENT_ISDONE(event))
            direction = 1 - direction;
        else
        {
            static uint8_t count;
            static uint8_t* bwFlag;
            static uint8_t bwMax;
            if (BUTTONEVENT_FIRSTLONGPRESS == event)
            {
                count = 0;
                if (AM == currentMode)
                {
                    bwFlag = &bwIdxAM;
                    bwMax = 6;
                }
                else if (FM == currentMode)
                {
                    bwFlag = &bwIdxFM;
                    bwMax = 4;
                }
                else
                {
                    bwFlag = &bwIdxSSB;
                    bwMax = 5;
                }
                if (0 == *bwFlag)
                    direction = 1;
                else if (*bwFlag == bwMax)
                    direction = 0;
            }
            if (0 == count)
            {
                if (((direction == 0) && (*bwFlag > 0)) || ((1 == direction) && (*bwFlag < bwMax)))
                {
                    doBandwidth(direction);
                }
            }
            count = (count + 1) % BANDWIDTH_DELAY;
        }
    }
#else
    if (BUTTONEVENT_FIRSTLONGPRESS == event)
        event = BUTTONEVENT_SHORTPRESS;
#endif
    return event;
}

uint8_t stepEvent(uint8_t event, uint8_t pin)
{
#if (0 != STEP_DELAY)
    if (FM != currentMode)
        if (BUTTONEVENT_ISLONGPRESS(event))
        {
            static uint8_t direction = 1;
            static uint8_t count;
            if (BUTTONEVENT_LONGPRESSDONE == event)
                direction = 1 - direction;
            else
            {
                if (BUTTONEVENT_FIRSTLONGPRESS == event)
                {
                    count = 0;
                    if (0 == idxStep)
                        direction = 1;
                    else
                    {
                        if (currentMode == FM)
                        {
                            if (lastStepFM == FMStepIdx)
                                direction = 0;
                        }
                        else
                        {
                            if (getLastStep() == idxStep)
                                direction = 0;
                        }
                    }
                }
                if (0 == count++)
                    if ((currentMode != FM && (idxStep != (direction ? getLastStep() : 0)))
                        || (currentMode == FM && (FMStepIdx != (direction ? lastStepFM : 0))))
                        doStep(direction);
                count = count % STEP_DELAY;
            }
        }
#else
    if (BUTTONEVENT_FIRSTLONGPRESS == event)
        event = BUTTONEVENT_SHORTPRESS;
#endif
    return event;
}

uint8_t agcEvent(uint8_t event, uint8_t pin)
{
#ifdef DEBUG
    buttonEvent(event, pin);
#endif
#if (0 != AGC_DELAY)
    if (FM != currentMode)
        if (BUTTONEVENT_ISLONGPRESS(event))
        {
            static uint8_t direction = 1;
            static uint8_t count;
            if (BUTTONEVENT_LONGPRESSDONE == event)
                direction = 1 - direction;
            else
            {
                if (BUTTONEVENT_FIRSTLONGPRESS == event)
                {
                    count = 0;
                    if (0 == Settings[SettingsIndex::ATT].param)
                        direction = 1;
                    else if (37 == Settings[SettingsIndex::ATT].param)
                        direction = 0;
                }
                if (0 == count++)
                    if ((!direction && Settings[SettingsIndex::ATT].param)
                        || (direction && (Settings[SettingsIndex::ATT].param < 37)))
                        doAttenuation(direction);
                count = count % AGC_DELAY;
            }
        }
#else
    if (BUTTONEVENT_FIRSTLONGPRESS == event)
        event = BUTTONEVENT_SHORTPRESS;
#endif
    return event;
}

uint8_t bandEvent(uint8_t event, uint8_t pin)
{
#ifdef DEBUG
    buttonEvent(event, pin);
#endif
#if (0 != BAND_DELAY)
    static uint8_t count;
    if (BUTTONEVENT_ISLONGPRESS(event))
    {
        if (BUTTONEVENT_LONGPRESSDONE != event)
        {
            if (BUTTONEVENT_FIRSTLONGPRESS == event)
            {
                count = 0;
            }
            if (count++ == 0)
            {
                if (BAND_BUTTON == pin)
                {
                    if (bandIdx < lastBand)
                        bandUp();
                }
                else
                {
                    if (bandIdx)
                        bandDown();
                }
            }
            count = count % BAND_DELAY;
        }
    }
#else
    if (BUTTONEVENT_FIRSTLONGPRESS == event)
        event = BUTTONEVENT_SHORTPRESS;
#endif
    return event;
}

// Handle encoder direction
void rotaryEncoder()
{
    uint8_t encoderStatus = encoder.process();
    if (encoderStatus)
    {
        if (encoderStatus == DIR_CW)
        {
            encoderCount = 1;
        }
        else
        {
            encoderCount = -1;
        }
    }
}

//EEPROM Save
void saveAllReceiverInformation()
{
    int addr_offset;
    EEPROM.update(eeprom_address, app_id);
    EEPROM.update(eeprom_address + 1, si4735.getVolume());
    EEPROM.update(eeprom_address + 2, bandIdx);
    EEPROM.update(eeprom_address + 3, currentMode);
    EEPROM.update(eeprom_address + 4, currentBFO >> 8);
    EEPROM.update(eeprom_address + 5, currentBFO & 0XFF);
    EEPROM.update(eeprom_address + 6, FMStepIdx);
    EEPROM.update(eeprom_address + 7, prevMode);

    addr_offset = 8;
    band[bandIdx].currentFreq = currentFrequency;

    for (int i = 0; i <= lastBand; i++)
    {
        EEPROM.update(addr_offset++, (band[i].currentFreq >> 8));
        EEPROM.update(addr_offset++, (band[i].currentFreq & 0xFF));
        EEPROM.update(addr_offset++, ((band[i].bandType != FM_BAND_TYPE && band[i].currentStepIdx >= amTotalSteps) ? 0 : band[i].currentStepIdx));
        EEPROM.update(addr_offset++, band[i].bandwidthIdx);
    }

    for (int i = 0; i < SettingsIndex::SETTINGS_MAX; i++)
        EEPROM.update(addr_offset++, Settings[i].param);
}

//EEPROM Load
void readAllReceiverInformation()
{
    int addr_offset;
    int bwIdx;
    volume = EEPROM.read(eeprom_address + 1);
    bandIdx = EEPROM.read(eeprom_address + 2);
    currentMode = EEPROM.read(eeprom_address + 3);
    currentBFO = EEPROM.read(eeprom_address + 4) << 8;
    currentBFO |= EEPROM.read(eeprom_address + 5);
    FMStepIdx = EEPROM.read(eeprom_address + 6);
    prevMode = EEPROM.read(eeprom_address + 7);

    addr_offset = 8;
    for (int i = 0; i <= lastBand; i++)
    {
        band[i].currentFreq = EEPROM.read(addr_offset++) << 8;
        band[i].currentFreq |= EEPROM.read(addr_offset++);
        band[i].currentStepIdx = EEPROM.read(addr_offset++);
        band[i].bandwidthIdx = EEPROM.read(addr_offset++);
    }

    for (int i = 0; i < SettingsIndex::SETTINGS_MAX; i++)
        Settings[i].param = EEPROM.read(addr_offset++);

    previousFrequency = currentFrequency = band[bandIdx].currentFreq;
    if (band[bandIdx].bandType == FM_BAND_TYPE)
        FMStepIdx = band[bandIdx].currentStepIdx;
    else
        idxStep = band[bandIdx].currentStepIdx;
    bwIdx = band[bandIdx].bandwidthIdx;
    if (idxStep >= amTotalSteps)
        idxStep = 0;

    if (isSSB())
    {
        loadSSBPatch();
        bwIdxSSB = (bwIdx > 5) ? 5 : bwIdx;
        si4735.setSSBAudioBandwidth(bandwidthSSB[bwIdxSSB].idx);
        // If SSB bandwidth 2 KHz or lower - it's better to enable cutoff filter
        if (bandwidthSSB[bwIdxSSB].idx == 0 || bandwidthSSB[bwIdxSSB].idx == 4 || bandwidthSSB[bwIdxSSB].idx == 5)
            si4735.setSSBSidebandCutoffFilter(0);
        else
            si4735.setSSBSidebandCutoffFilter(1);
    }
    else if (currentMode == AM)
    {
        bwIdxAM = bwIdx;
        si4735.setBandwidth(bandwidthAM[bwIdxAM].idx, 1);
    }
    else
    {
        bwIdxFM = bwIdx;
        si4735.setFmBandwidth(bandwidthFM[bwIdxFM].idx);
    }

    applyBandConfiguration();
}

//For saving features
void resetEepromDelay()
{
    storeTime = millis();
    previousFrequency = 0;
}

//Faster alternative for convertToChar
void utoa(char* out, uint16_t num)
{
    char* p = out;
    if (num == 0)
        *p++ = '0';

    else
    {
        for (uint16_t base = 10000; base > 0; base /= 10)
        {
            if (num >= base)
            {
                *p++ = '0' + num / base;
                num %= base;
            }
            else if (p != out)
                *p++ = '0';
        }
    }

    *p = '\0';
}

//Better than sprintf which has overwhelmingly large overhead, it helps to reduce binary size
void convertToChar(char* strValue, uint16_t value, uint8_t len, uint8_t dot = 0, uint8_t separator = 0, uint8_t space = ' ')
{
    char d;
    int i;
    for (i = (len - 1); i >= 0; i--)
    {
        d = value % 10;
        value = value / 10;
        strValue[i] = d + 48;
    }
    strValue[len] = '\0';
    if (dot > 0)
    {
        for (int i = len; i >= dot; i--)
        {
            strValue[i + 1] = strValue[i];
        }
        strValue[dot] = separator;
        len = dot;
    }
    i = 0;
    len--;
    while ((i < len) && ('0' == strValue[i]))
    {
        strValue[i++] = space;
    }
}

//Measure integer digit length
int ilen(uint16_t n)
{
    if (n < 10)
        return 1;
    else if (n < 100)
        return 2;
    else if (n < 1000)
        return 3;
    else if (n < 10000)
        return 4;
    else
        return 5;
}

//Split KHz frequency + BFO to KHz and .00 tail
void splitFreq(uint16_t& khz, uint16_t& tail)
{
    int32_t freq = (uint32_t(currentFrequency) * 1000) + currentBFO;
    khz = freq / 1000;
    tail = abs(freq % 1000) / 10;
}

//Draw frequency. 
//BFO and main frequency produce actual frequency that is displayed on LCD
//Can be optimized
void showFrequency(bool cleanDisplay = false)
{
    if (settingsActive)
        return;

    char* unit;
    char freqDisplay[7] = { 0, 0, 0, 0, 0, 0, 0 };
    char ssbSuffix[4] = { '.', '0', '0', 0 };
    static int prevLen = 0;
    uint16_t khzBFO, tailBFO;

    if (band[bandIdx].bandType == FM_BAND_TYPE)
    {
        convertToChar(freqDisplay, currentFrequency, 5, 3, '.', '/');
        unit = (char*)"MHz";
    }
    else
    {
        unit = (char*)"KHz";
        if (!isSSB())
            utoa(freqDisplay, currentFrequency);
        else
        {
            splitFreq(khzBFO, tailBFO);
            utoa(freqDisplay, khzBFO);
        }
    }

    int off = isSSB() ? 3 : 4;
    int len = isSSB() ? ilen(khzBFO) + 2 : ilen(currentFrequency);
    oledSetFont(FONT14X24SEVENSEG);
    if (len < prevLen || cleanDisplay)
    {
        oled.setCursor(0, 3);
        oledPrint("/////////", 0, 3); // This character is an empty space in my seven seg font.
    }

    if (!isSSB())
        off += 8 * (5 - len);
    else if ((currentFrequency + (currentBFO / 1000) < 1000))
        off += 8;

    oledPrint(freqDisplay, off, 3);
    if (isSSB())
    {
        utoa((ilen(tailBFO) == 1) ? &ssbSuffix[2] : &ssbSuffix[1], tailBFO);
        ssbSuffix[3] = 0;
        oledPrint(ssbSuffix);
    }

    if (!isSSB() || isSSB() && ilen(khzBFO) < 5)
        oledPrint(unit, 102, 4, DEFAULT_FONT);
    prevLen = len;
}

//This function is called by station seek logic
void showFrequencySeek(uint16_t freq)
{
    currentFrequency = freq;
    showFrequency();
}

bool checkStopSeeking()
{
    // Checks the touch and encoder
    return (bool)encoderCount || (digitalRead(ENCODER_BUTTON) == LOW);
}


//Update and draw main screen UI. 
//basicUpdate - update minimum as possible
//cleanFreq   - force clean frequency line
void showStatus(bool basicUpdate = false, bool cleanFreq = false)
{
    showFrequency(cleanFreq);
    showModulation();
    showStep();
    showBandwidth();
    if (!basicUpdate)
    {
        showVolume();
    }
}


//Converts settings value to UI value
void SettingParamToUI(char* buf, uint8_t idx)
{
    switch (Settings[idx].type)
    {
    case SettingType::ZeroAuto:
        if (Settings[idx].param == 0)
        {
            buf[0] = 'A';
            buf[1] = 'U';
            buf[2] = 'T';
            buf[3] = 0x0;
        }
        else
            convertToChar(buf, Settings[idx].param, 3);

        break;

    case SettingType::Num:
        convertToChar(buf, Settings[idx].param, 3);
        break;

    case SettingType::Switch:
        if (idx == SettingsIndex::DeEmp)
        {
            if (Settings[idx].param == 0)
            {
                buf[0] = '5';
                buf[1] = '0';
                buf[2] = 'u';
                buf[3] = 0x0;
            }
            else
            {
                buf[0] = '7';
                buf[1] = '5';
                buf[2] = 'u';
                buf[3] = 0x0;
            }
        }
        else
        {
            if (Settings[idx].param == 0)
            {
                buf[0] = 'O';
                buf[1] = 'f';
                buf[2] = 'f';
                buf[3] = 0x0;
            }
            else
            {
                buf[0] = 'O';
                buf[1] = 'n';
                buf[2] = ' ';
                buf[3] = 0x0;
            }
        }
        break;

    default:
        return;
    }
}

// If full false - update only value
void DrawSetting(uint8_t idx, bool full)
{
    if (!settingsActive)
        return;

    char buf[5] = { 0, 0, 0, 0, 0 };
    uint8_t yOffset = idx > 2 ? (idx - 3) * 2 : idx * 2;
    uint8_t xOffset = idx > 2 ? 60 : 0;
    if (full)
        oledPrint(Settings[idx].name, 5 + xOffset, 2 + yOffset, DEFAULT_FONT, idx == SettingSelected && !SettingEditing);
    SettingParamToUI(buf, idx);
    oledPrint(buf, 35 + xOffset, 2 + yOffset, DEFAULT_FONT, idx == SettingSelected && SettingEditing);
}

//Update and draw settings UI
void showSettings()
{
    for (uint8_t i = 0; i < SettingsIndex::SETTINGS_MAX; i++)
        DrawSetting(i, true);
}

//Switch between main screen and settings mode
void switchSettings()
{
    if (!settingsActive)
    {
        settingsActive = true;
        oled.clear();
        oledPrint("    SETTINGS    ", 0, 0, DEFAULT_FONT, true);
        SettingSelected = 0;
        SettingEditing = false;
        showSettings();
    }
    else
    {
        settingsActive = false;
        oled.clear();
        showStatus();
    }
}

//Draw curremt modulation
void showModulation()
{
    oledPrint(bandModeDesc[currentMode], 0, 0, DEFAULT_FONT, cmdBand && currentMode == FM);
    oled.print(" ");
    if (isSSB() && Settings[SettingsIndex::Sync].param == 1)
    {
        oled.invertOutput(true);
        oled.print("S");
        oled.invertOutput(false);
    }
    else
        oled.print(" ");
    showBandTag();
}

//Draw current band
void showBandTag()
{
    oledPrint(band[bandIdx].tag, 0, 6, DEFAULT_FONT, cmdBand && currentMode != FM);
}

//S-meter?
void showRSSI()
{
    //TODO: _____
    return;
    int bars = (rssi / 20.0) + 1;
    oled.setCursor(90, 3);
    oled.print("      ");
    oled.setCursor(90, 3);
    oled.print(".");
    for (int i = 0; i < bars; i++)
        oled.print('_');
    oled.print('|');

    if (currentMode == FM)
    {
        oled.setCursor(18, 0);
        oled.print("  ");
        oled.setCursor(18, 0);
        oled.invertOutput(true);
        if (si4735.getCurrentPilot())
        {
            oled.invertOutput(true);
            oled.print("s");
        }
        oled.invertOutput(false);
    }
}

//Draw volume level
void showVolume()
{
    if (settingsActive)
        return;
    char buf[3];
    if (muteVolume == 0)
        convertToChar(buf, si4735.getCurrentVolume(), 2, 0, 0);
    else
    {
        buf[0] = ' ';
        buf[1] = 'M';
        buf[2] = 0;
    }
    int off = 128 - (8 * 2) + 2 - 6;
    oledPrint(buf, off, 0, DEFAULT_FONT, cmdVolume);
}

//Draw battery charge
void showCharge()
{
    //TODO: Analog charge monitor
}

//Draw steps (with units)
void showStep()
{
    char buf[5];
    if (currentMode == FM)
    {
        if (tabStepFM[FMStepIdx] == 100)
        {
            buf[0] = ' ';
            buf[1] = ' ';
            buf[2] = '1';
            buf[3] = 'M';
            buf[4] = 0x0;
        }
        else
        {
            convertToChar(buf, tabStepFM[FMStepIdx] * 10, 3);
            buf[3] = 'k';
            buf[4] = '\0';
        }
    }
    else
    {
        if (tabStep[idxStep] == 1000)
        {
            buf[0] = ' ';
            buf[1] = ' ';
            buf[2] = '1';
            buf[3] = 'M';
            buf[4] = 0x0;
        }
        else if (isSSB() && idxStep >= amTotalSteps)
            convertToChar(buf, tabStep[idxStep], 4);
        else
        {
            convertToChar(buf, tabStep[idxStep], 3);
            buf[3] = 'k';
            buf[4] = '\0';
        }
    }

    int off = 50;
    oledPrint("St:", off - 16, 6, DEFAULT_FONT, cmdStep);
    oledPrint(buf, off + 8, 6, DEFAULT_FONT, cmdStep);
}

//Draw bandwidth (Ignored for CW mode)
void showBandwidth()
{
    char* bw;
    if (isSSB())
    {
        bw = (char*)bandwidthSSB[bwIdxSSB].desc;
        if (currentMode == CW)
            bw = "    ";
    }
    else if (currentMode == AM)
    {
        bw = (char*)bandwidthAM[bwIdxAM].desc;
    }
    else
    {
        bw = (char*)bandwidthFM[bwIdxFM].desc;
    }

    int off = 45;
    oledPrint(bw, off, 0, DEFAULT_FONT, cmdBw);
}

#if USE_RDS
char* stationName;
char bufferStatioName[20];
long rdsElapsed = millis();

char oldBuffer[15];

void showRDSStation()
{
    char* po, * pc;
    int col = 0;

    po = oldBuffer;
    pc = stationName;
    while (*pc)
    {
        if (*po != *pc)
        {
            oled.setCursor(col, 2);
            oled.print(*pc);
        }
        *po = *pc;
        po++;
        pc++;
        col += 10;
    }
    // strcpy(oldBuffer, stationName);
    delay(100);
}


void checkRDS()
{
    si4735.getRdsStatus();
    if (si4735.getRdsReceived())
    {
        if (si4735.getRdsSync() && si4735.getRdsSyncFound() && !si4735.getRdsSyncLost() && !si4735.getGroupLost())
        {
            stationName = si4735.getRdsText0A();
            if (stationName != NULL /* && si4735.getEndGroupB()  && (millis() - rdsElapsed) > 10 */)
            {
                showRDSStation();
                // si4735.resetEndGroupB();
                rdsElapsed = millis();
            }
        }
    }
}
#endif


void bandUp()
{
    band[bandIdx].currentFreq = currentFrequency;

    if (currentMode == FM)
        band[bandIdx].currentStepIdx = FMStepIdx;
    else
        band[bandIdx].currentStepIdx = idxStep;

    if (bandIdx < lastBand)
    {
        bandIdx++;
    }
    else
    {
        bandIdx = 0;
    }
    currentBFO = 0;
    if (isSSB())
        si4735.setSSBBfo(0);
    applyBandConfiguration();
}

void bandDown()
{
    band[bandIdx].currentFreq = currentFrequency;

    if (currentMode == FM)
        band[bandIdx].currentStepIdx = FMStepIdx;
    else
        band[bandIdx].currentStepIdx = idxStep;

    if (bandIdx > 0)
    {
        bandIdx--;
    }
    else
    {
        bandIdx = lastBand;
    }
    currentBFO = 0;
    if (isSSB())
        si4735.setSSBBfo(0);
    applyBandConfiguration();
}

// This function is required for using SSB. Si473x controllers do not support SSB by-default.
// But we can patch internal RAM of Si473x with special patch to make it work in SSB mode.
// Patch must be applied every time we enable SSB after AM or FM.
void loadSSBPatch()
{
    // This works, but i am not sure it's safe
    //si4735.setI2CFastModeCustom(700000);
    si4735.setI2CFastModeCustom(500000);
    si4735.queryLibraryId(); //Do we really need this? Research it.
    si4735.patchPowerUp();
    delay(50);
    si4735.downloadCompressedPatch(ssb_patch_content, size_content, cmd_0x15, cmd_0x15_size);
    si4735.setSSBConfig(bandwidthSSB[bwIdxSSB].idx, 1, 0, 1, 0, 1);
    si4735.setI2CStandardMode();
    ssbLoaded = true;
    idxStep = 0;
}

//Update receiver settings after changing band and modulation
void applyBandConfiguration(bool extraSSBReset = false)
{
    if (band[bandIdx].bandType == FM_BAND_TYPE)
    {
        currentMode = FM;
        si4735.setTuneFrequencyAntennaCapacitor(0);
        si4735.setFM(band[bandIdx].minimumFreq,
            band[bandIdx].maximumFreq,
            band[bandIdx].currentFreq,
            tabStepFM[band[bandIdx].currentStepIdx]);
        si4735.setSeekFmLimits(band[bandIdx].minimumFreq, band[bandIdx].maximumFreq);
        si4735.setSeekFmSpacing(1);
        ssbLoaded = false;
        si4735.setRdsConfig(1, 2, 2, 2, 2);
        si4735.setFifoCount(1);
        bwIdxFM = band[bandIdx].bandwidthIdx;
        si4735.setFmBandwidth(bandwidthFM[bwIdxFM].idx);
        si4735.setFMDeEmphasis(Settings[SettingsIndex::DeEmp].param == 0 ? 1 : 2);
    }
    else
    {
        if (band[bandIdx].bandType == MW_BAND_TYPE || band[bandIdx].bandType == LW_BAND_TYPE)
            si4735.setTuneFrequencyAntennaCapacitor(0);
        else
            si4735.setTuneFrequencyAntennaCapacitor(1);

        uint16_t minFreq = band[bandIdx].minimumFreq;
        uint16_t maxFreq = band[bandIdx].maximumFreq;
        if (band[bandIdx].bandType == SW_BAND_TYPE)
        {
            minFreq = SW_LIMIT_LOW;
            maxFreq = SW_LIMIT_HIGH;
        }

        if (ssbLoaded)
        {
            if (prevMode == AM || prevMode == FM)
                currentBFO = 0;
            si4735.setSSBAvcDivider(Settings[SettingsIndex::Sync].param == 0 ? 0 : 3); //Set Sync mode
            if (extraSSBReset)
                loadSSBPatch();
            si4735.setSSB(minFreq,
                maxFreq,
                band[bandIdx].currentFreq,
                band[bandIdx].currentStepIdx >= amTotalSteps ? 0 : tabStep[band[bandIdx].currentStepIdx],
                currentMode == CW ? LSB : currentMode);

            si4735.setSSBAutomaticVolumeControl(Settings[SettingsIndex::SVC].param);
            si4735.setSSBAvcDivider(Settings[SettingsIndex::Sync].param == 0 ? 0 : 3); //Set Sync mode
            //si4735.setSsbSoftMuteMaxAttenuation(Settings[SettingsIndex::SMMaxAtt].param);
            bwIdxSSB = band[bandIdx].bandwidthIdx;
            si4735.setSSBAudioBandwidth(currentMode == CW ? bandwidthSSB[0].idx : bandwidthSSB[bwIdxSSB].idx);
            si4735.setSSBBfo(currentBFO * -1);
        }
        else
        {
            currentMode = AM;
            si4735.setAM(minFreq,
                maxFreq,
                band[bandIdx].currentFreq,
                band[bandIdx].currentStepIdx >= amTotalSteps ? 0 : tabStep[band[bandIdx].currentStepIdx]);
            uint8_t att = Settings[SettingsIndex::ATT].param;
            uint8_t disableAgc = att > 0;
            uint8_t agcNdx;
            if (att > 1) agcNdx = att - 1;
            else agcNdx = 0;
            si4735.setAutomaticGainControl(disableAgc, agcNdx);
            si4735.setAutomaticGainControl((att > 0) ? 0 : 1, (att > 0) ? att - 1 : 0);
            si4735.setAmSoftMuteMaxAttenuation(Settings[SettingsIndex::SoftMute].param);
            bwIdxAM = band[bandIdx].bandwidthIdx;
            si4735.setBandwidth(bandwidthAM[bwIdxAM].idx, 1);
        }

        si4735.setAvcAmMaxGain(Settings[SettingsIndex::AutoVolControl].param);
        si4735.setSeekAmLimits(minFreq, maxFreq);
        si4735.setSeekAmSpacing((tabStep[band[bandIdx].currentStepIdx] > 10 || band[bandIdx].currentStepIdx >= amTotalSteps) ? 10 : tabStep[band[bandIdx].currentStepIdx]);
    }
    //delay(100);
    //oled.clear();
    currentFrequency = band[bandIdx].currentFreq;
    if (currentMode == FM)
        FMStepIdx = band[bandIdx].currentStepIdx;
    else
        idxStep = band[bandIdx].currentStepIdx;

    if (!settingsActive)
        showStatus(true, true);
    resetEepromDelay();
}

//Step value regulation
void doStep(int8_t v)
{
    if (currentMode == FM)
    {
        FMStepIdx = (v == 1) ? FMStepIdx + 1 : FMStepIdx - 1;
        if (FMStepIdx > lastStepFM)
            FMStepIdx = 0;
        else if (FMStepIdx < 0)
            FMStepIdx = lastStepFM;

        si4735.setFrequencyStep(tabStepFM[FMStepIdx]);
        band[bandIdx].currentStepIdx = FMStepIdx;
        //si4735.setSeekFmSpacing(tabStepFM[FMStepIdx]);
        showStep();
    }
    else
    {
        idxStep = (v == 1) ? idxStep + 1 : idxStep - 1;
        if (idxStep > getLastStep())
            idxStep = 0;
        else if (idxStep < 0)
            idxStep = getLastStep();
        else if (isSSB() && v == 1 && idxStep >= amTotalStepsSSB && idxStep < amTotalSteps)
            idxStep = amTotalSteps;
        else if (isSSB() && v != 1 && idxStep >= amTotalStepsSSB && idxStep < amTotalSteps)
            idxStep = amTotalStepsSSB - 1;


        if (!isSSB() || isSSB() && idxStep < amTotalSteps)
        {
            si4735.setFrequencyStep(tabStep[idxStep]);
            band[bandIdx].currentStepIdx = idxStep;
            si4735.setSeekAmSpacing((tabStep[idxStep] > 10) ? 10 : tabStep[idxStep]);
        }
        showStep();
    }
}

//Volume control
void doVolume(int8_t v)
{
    if (muteVolume)
    {
        si4735.setVolume(muteVolume);
        muteVolume = 0;
    }
    else
        if (v == 1)
            si4735.volumeUp();
        else
            si4735.volumeDown();
    showVolume();
}

//Settings: Attenuation
void doAttenuation(int8_t v)
{
    Settings[SettingsIndex::ATT].param = ((v == 1) ? Settings[SettingsIndex::ATT].param + 1 : Settings[SettingsIndex::ATT].param - 1);
    if (Settings[SettingsIndex::ATT].param < 0)
        Settings[SettingsIndex::ATT].param = 37;
    else if (Settings[SettingsIndex::ATT].param > 37)
        Settings[SettingsIndex::ATT].param = 0;

    uint8_t att = Settings[SettingsIndex::ATT].param;
    uint8_t disableAgc = att > 0;
    uint8_t agcNdx;
    if (att > 1) agcNdx = att - 1;
    else agcNdx = 0;
    si4735.setAutomaticGainControl(disableAgc, agcNdx);

    DrawSetting(SettingsIndex::ATT, false);
}

//Settings: Soft Mute
void doSoftMute(int8_t v)
{
    Settings[SettingsIndex::SoftMute].param = (v == 1) ? Settings[SettingsIndex::SoftMute].param + 1 : Settings[SettingsIndex::SoftMute].param - 1;
    if (Settings[SettingsIndex::SoftMute].param > 32)
        Settings[SettingsIndex::SoftMute].param = 0;
    else if (Settings[SettingsIndex::SoftMute].param < 0)
        Settings[SettingsIndex::SoftMute].param = 32;

    if (currentMode != FM)
        si4735.setAmSoftMuteMaxAttenuation(Settings[SettingsIndex::SoftMute].param);

    delay(MIN_ELAPSED_TIME);
    DrawSetting(SettingsIndex::SoftMute, false);
}

//Settings: SSB Soft Mute
//void doSSBSoftMute(int8_t v)
//{
//    Settings[SettingsIndex::SMMaxAtt].param = (v == 1) ? Settings[SettingsIndex::SMMaxAtt].param + 1 : Settings[SettingsIndex::SMMaxAtt].param - 1;
//    if (Settings[SettingsIndex::SMMaxAtt].param > 32)
//        Settings[SettingsIndex::SMMaxAtt].param = 0;
//    else if (Settings[SettingsIndex::SMMaxAtt].param < 0)
//        Settings[SettingsIndex::SMMaxAtt].param = 32;
//
//    if (isSSB())
//        si4735.setSsbSoftMuteMaxAttenuation(Settings[SettingsIndex::SMMaxAtt].param);
//
//    delay(MIN_ELAPSED_TIME);
//    DrawSetting(SettingsIndex::SMMaxAtt, false);
//}

//Settings: SSB AVC Switch
void doSSBAVC()
{
    if (Settings[SettingsIndex::SVC].param == 0)
        Settings[SettingsIndex::SVC].param = 1;
    else
        Settings[SettingsIndex::SVC].param = 0;

    if (isSSB())
    {
        si4735.setSSBAutomaticVolumeControl(Settings[SettingsIndex::SVC].param);
        band[bandIdx].currentFreq = currentFrequency;
        applyBandConfiguration(true);
    }

    DrawSetting(SettingsIndex::SVC, false);
    delay(MIN_ELAPSED_TIME);
}

//Settings: Automatic Volume Control
void doAvc(int8_t v)
{
    Settings[SettingsIndex::AutoVolControl].param = (v == 1) ? Settings[SettingsIndex::AutoVolControl].param + 2 : Settings[SettingsIndex::AutoVolControl].param - 2;
    if (Settings[SettingsIndex::AutoVolControl].param > 90)
        Settings[SettingsIndex::AutoVolControl].param = 12;
    else if (Settings[SettingsIndex::AutoVolControl].param < 12)
        Settings[SettingsIndex::AutoVolControl].param = 90;

    if (currentMode != FM)
        si4735.setAvcAmMaxGain(Settings[SettingsIndex::AutoVolControl].param);
    delay(MIN_ELAPSED_TIME);
    DrawSetting(SettingsIndex::AutoVolControl, false);
}

//Settings: Sync switch
void doSync()
{
    if (Settings[SettingsIndex::Sync].param == 0)
        Settings[SettingsIndex::Sync].param = 1;
    else
        Settings[SettingsIndex::Sync].param = 0;

    if (isSSB())
    {
        si4735.setSSBAvcDivider(Settings[SettingsIndex::Sync].param == 0 ? 0 : 3); //Set Sync mode
        band[bandIdx].currentFreq = currentFrequency;
        applyBandConfiguration(true);
    }

    DrawSetting(SettingsIndex::Sync, false);
    delay(MIN_ELAPSED_TIME);
}

//Settings: FM DeEmp switch (50 or 75)
void doDeEmp()
{
    if (Settings[SettingsIndex::DeEmp].param == 0)
        Settings[SettingsIndex::DeEmp].param = 1;
    else
        Settings[SettingsIndex::DeEmp].param = 0;

    if (currentMode == FM)
        si4735.setFMDeEmphasis(Settings[SettingsIndex::DeEmp].param == 0 ? 1 : 2);

    delay(MIN_ELAPSED_TIME);
    DrawSetting(SettingsIndex::DeEmp, false);
}

//Bandwidth regulation logic
void doBandwidth(uint8_t v)
{
    if (isSSB())
    {
        bwIdxSSB = (v == 1) ? bwIdxSSB + 1 : bwIdxSSB - 1;

        if (bwIdxSSB > 5)
            bwIdxSSB = 0;
        else if (bwIdxSSB < 0)
            bwIdxSSB = 5;

        band[bandIdx].bandwidthIdx = bwIdxSSB;

        si4735.setSSBAudioBandwidth(bandwidthSSB[bwIdxSSB].idx);

        // If SSB bandwidth 2 KHz or lower - it's better to enable cutoff filter
        if (bandwidthSSB[bwIdxSSB].idx == 0 || bandwidthSSB[bwIdxSSB].idx == 4 || bandwidthSSB[bwIdxSSB].idx == 5)
            si4735.setSSBSidebandCutoffFilter(0);
        else
            si4735.setSSBSidebandCutoffFilter(1);
    }
    else if (currentMode == AM)
    {
        bwIdxAM = (v == 1) ? bwIdxAM + 1 : bwIdxAM - 1;

        if (bwIdxAM > maxFilterAM)
            bwIdxAM = 0;
        else if (bwIdxAM < 0)
            bwIdxAM = maxFilterAM;

        band[bandIdx].bandwidthIdx = bwIdxAM;
        si4735.setBandwidth(bandwidthAM[bwIdxAM].idx, 1);
    }
    else
    {
        bwIdxFM = (v == 1) ? bwIdxFM + 1 : bwIdxFM - 1;
        if (bwIdxFM > 4)
            bwIdxFM = 0;
        else if (bwIdxFM < 0)
            bwIdxFM = 4;

        band[bandIdx].bandwidthIdx = bwIdxFM;
        si4735.setFmBandwidth(bandwidthFM[bwIdxFM].idx);
    }
    showBandwidth();
}

void disableCommand(bool* b, bool value, void (*showFunction)())
{
    static bool anyOn = false;
    if (anyOn)
    {
        cmdVolume = false;
        cmdStep = false;
        cmdBw = false;
        cmdBand = false;
        showVolume();
        showStep();
        showBandwidth();
        showModulation();

        anyOn = false;

    }
    if (b != NULL)
        *b = anyOn = value;
    if (showFunction != NULL)
        showFunction();

    elapsedRSSI = millis();
    countRSSI = 0;
}

bool clampSSBBand()
{
    int freq = currentFrequency + (currentBFO / 1000);

    if (freq > band[bandIdx].maximumFreq)
    {
        if (band[bandIdx + 1].bandType == FM_BAND_TYPE)
        {
            bandIdx = 0;
            currentFrequency = band[bandIdx].minimumFreq;
            band[bandIdx].currentFreq = currentFrequency;
            si4735.setFrequency(band[bandIdx].minimumFreq);
            currentBFO = 0;
            si4735.setSSBBfo(0);
            showFrequency(true);
            showModulation();
            return true;
        }
        else
        {
            bandIdx++;
            showModulation();
        }
    }
    else if (freq < band[bandIdx].minimumFreq)
    {
        if (bandIdx == 0)
        {
            bandIdx = 20;
            currentFrequency = band[bandIdx].maximumFreq;
            band[bandIdx].currentFreq = currentFrequency;
            si4735.setFrequency(band[bandIdx].maximumFreq);
            currentBFO = 0;
            si4735.setSSBBfo(0);
            showFrequency(true);
            showModulation();
            return true;
        }
        else
        {
            bandIdx--;
            showModulation();
        }
    }

    return false;
}

void loop()
{
    uint8_t x;
    bool skipButtonEvents = false;

    if (processFreqChange && millis() - lastFreqChange >= 60 && encoderCount == 0)
    {
        si4735.setFrequency(currentFrequency);
        processFreqChange = false;
    }

    //Encoder rotation check
    if (encoderCount != 0)
    {
        if (settingsActive)
        {
            if (!SettingEditing)
            {
                int8_t prev = SettingSelected;
                int8_t next = SettingSelected;

                if (encoderCount > 0)
                    next++;
                else
                    next--;

                if (next < 0)
                    SettingSelected = SettingsIndex::SETTINGS_MAX - 1;
                else if (next >= SettingsIndex::SETTINGS_MAX)
                    SettingSelected = 0;
                else
                    SettingSelected = next;

                DrawSetting(prev, true);
                DrawSetting(SettingSelected, true);
            }
            else
            {
                switch (SettingSelected)
                {
                case SettingsIndex::ATT:
                    doAttenuation(encoderCount);
                    break;

                case SettingsIndex::AutoVolControl:
                    doAvc(encoderCount);
                    break;

                case SettingsIndex::DeEmp:
                    doDeEmp();
                    break;

                case SettingsIndex::SVC:
                    doSSBAVC();
                    break;

                case SettingsIndex::SoftMute:
                    doSoftMute(encoderCount);
                    break;

                case SettingsIndex::Sync:
                    doSync();
                    break;
                }
            }
        }
        else if (cmdVolume)
            doVolume(encoderCount);
        //else if (cmdAgcAtt)
        //    doAttenuation(encoderCount);
        else if (cmdStep)
            doStep(encoderCount);
        else if (cmdBw)
            doBandwidth(encoderCount);
        //else if (cmdSoftMute)
        //    doSoftMute(encoderCount);
        //else if (cmdAvc)
        //    doAvc(encoderCount);
        else if (cmdBand)
        {
            if (encoderCount == 1)
                bandUp();
            else
                bandDown();
        }
        else if (isSSB())
        {
            //Special feature to make SSB feel like on expensive TECSUN receivers
            //BFO is now part of main frequency in SSB mode
            const int BFOMax = 16000;
            int step = encoderCount == 1 ? getSteps() : getSteps() * -1;
            int newBFO = currentBFO + step;
            int redundant = 0;

            if (newBFO > BFOMax)
            {
                redundant = (newBFO / BFOMax) * BFOMax;
                currentFrequency += redundant / 1000;
                newBFO -= redundant;
            }
            else if (newBFO < -BFOMax)
            {
                redundant = ((abs(newBFO) / BFOMax) * BFOMax);
                currentFrequency -= redundant / 1000;
                newBFO += redundant;
            }

            if (step < 0)
                seekDirection = 0;
            else
                seekDirection = 1;

            currentBFO = newBFO;

            si4735.setSSBBfo(currentBFO * -1); //Actually to move frequency forward you need to move BFO backwards
            if (redundant != 0)
            {
                si4735.setFrequency(currentFrequency);
                currentFrequency = si4735.getFrequency();
            }

            previousFrequency = 0; //Force EEPROM update
            if (!clampSSBBand()) //If we move outside of current band - switch it
                showFrequency();
            skipButtonEvents = true;
        }
        else
        {
            if (encoderCount == 1)
            {
                //si4735.frequencyUp();
                seekDirection = 1;
            }
            else
            {
                //si4735.frequencyDown();
                seekDirection = 0;
            }

            //Update frequency
            if (currentMode == FM)
                currentFrequency += tabStepFM[FMStepIdx] * encoderCount; //si4735.getFrequency() is too slow
            else
                currentFrequency += tabStep[idxStep] * encoderCount;
            uint16_t bMin = band[bandIdx].minimumFreq, bMax = band[bandIdx].maximumFreq;
            if (band[bandIdx].bandType = SW_BAND_TYPE)
            {
                bMin = SW_LIMIT_LOW;
                bMax = SW_LIMIT_HIGH;
            }

            //Special logic for fast and responsive frequency surfing
            if (currentFrequency > bMax)
                currentFrequency = bMin;
            else if (currentFrequency < bMin)
                currentFrequency = bMax;

            processFreqChange = true;
            lastFreqChange = millis();

            showFrequency();
            skipButtonEvents = true;
        }
        encoderCount = 0;
        resetEepromDelay();
        elapsedRSSI = millis();
        countRSSI = 0;
    }

    if (skipButtonEvents)
        goto saveAttempt;

    //Command-checkers
    if (BUTTONEVENT_SHORTPRESS == btn_Bandwidth.checkEvent(bandwidthEvent))
    {
        if (!settingsActive && currentMode != CW)
        {
            cmdBw = !cmdBw;
            disableCommand(&cmdBw, cmdBw, showBandwidth);
        }
    }
    if (BUTTONEVENT_SHORTPRESS == btn_BandUp.checkEvent(bandEvent))
    {
        if (!settingsActive)
        {
            cmdBand = !cmdBand;
            disableCommand(&cmdBand, cmdBand, showModulation);
        }
    }
    if (BUTTONEVENT_SHORTPRESS == btn_BandDn.checkEvent(bandEvent))
    {
        if (!settingsActive)
        {
            cmdVolume = false;
            cmdStep = false;
            cmdBw = false;
            cmdBand = false;
        }
        disableCommand(&settingsActive, settingsActive, switchSettings);
    }
    if (BUTTONEVENT_SHORTPRESS == btn_VolumeUp.checkEvent(volumeEvent))
    {
        if (!settingsActive && muteVolume == 0)
        {
            cmdVolume = !cmdVolume;
            disableCommand(&cmdVolume, cmdVolume, showVolume);
        }
    }
    if (BUTTONEVENT_SHORTPRESS == btn_VolumeDn.checkEvent(volumeEvent))
    {
        if (!cmdVolume)
        {
            uint8_t vol = si4735.getCurrentVolume();
            if (vol > 0 && muteVolume == 0)
            {
                muteVolume = si4735.getCurrentVolume();
                si4735.setVolume(0);
            }
            else if (muteVolume > 0)
            {
                si4735.setVolume(muteVolume);
                muteVolume = 0;
            }
            showVolume();
        }
    }
    if (BUTTONEVENT_SHORTPRESS == btn_Encoder.checkEvent(encoderEvent))
    {
        if (cmdBand)
        {
            cmdBand = false;
            showModulation();
        }
        else if (cmdStep)
        {
            cmdStep = false;
            showStep(); 
        }
        else if (cmdBw)
        {
            cmdBw = false;
            showBandwidth();
        }
        else if (cmdVolume)
        {
            cmdVolume = false;
            showVolume();
        }
        else if (settingsActive)
        {
            SettingEditing = !SettingEditing;
            DrawSetting(SettingSelected, true);
        }
        //Seek in SSB/CW is not allowed
        else if (currentMode == FM || currentMode == AM)
        {
            if (seekDirection)
                si4735.frequencyUp();
            else
                si4735.frequencyDown();

            si4735.seekStationProgress(showFrequencySeek, checkStopSeeking, seekDirection);
            delay(30);
            if (currentMode == FM)
            {
                float f = round(si4735.getFrequency() / 10.0);
                //Interval from 10 to 100 KHz
                currentFrequency = (uint16_t)f * 10;
                si4735.setFrequency(currentFrequency);
            }
            else
            {
                currentFrequency = si4735.getFrequency();
            }
            showFrequency();
        }
    }
    uint8_t agcEvent = btn_AGC.checkEvent(agcEvent);
    if (BUTTONEVENT_SHORTPRESS == agcEvent)
    {
        if (!settingsActive)
        {
            displayOn = !displayOn;
            if (displayOn)
                oled.on();
            else
                oled.off();
        }
    }
    if (BUTTONEVENT_LONGPRESS == agcEvent)
    {
        if (!settingsActive)
        {
            if (isSSB())
                doSync();
        }
    }
    if (BUTTONEVENT_SHORTPRESS == btn_Step.checkEvent(stepEvent))
    {
        if (!settingsActive)
        {
            cmdStep = !cmdStep;
            disableCommand(&cmdStep, cmdStep, showStep);
        }
    }
    if (BUTTONEVENT_SHORTPRESS == btn_Mode.checkEvent(modeEvent))
    {
        //Do nothing on FM mode (unfortunately no NBFM patch), otherwise switch AM modulation
        if (!settingsActive && currentMode != FM)
        {
            if (currentMode == AM)
            {
                //Patch Si473x memory every time when enabling SSB
                loadSSBPatch();
                prevMode = currentMode;
                currentMode = LSB;
            }
            else if (currentMode == LSB)
            {
                prevMode = currentMode;
                currentMode = USB;
            }
            else if (currentMode == USB)
            {
                prevMode = currentMode;
                currentMode = CW;
            }
            else if (currentMode == CW)
            {
                prevMode = currentMode;
                currentMode = AM;
                ssbLoaded = false;
                if (idxStep >= amTotalSteps)
                    idxStep = 0;

                int bfoKhz = 0;
                if (prevMode == USB || prevMode == LSB || prevMode == CW)
                    bfoKhz = currentBFO / 1000;
                band[bandIdx].currentFreq += bfoKhz;
                currentFrequency += bfoKhz;

                showFrequency(true);
            }

            band[bandIdx].currentFreq = currentFrequency;
            band[bandIdx].currentStepIdx = idxStep;
            applyBandConfiguration();
        }
    }

    //TODO: Implement S-Meter
    if ((millis() - elapsedRSSI) > MIN_ELAPSED_RSSI_TIME * 9)
    {
        si4735.getCurrentReceivedSignalQuality();
        int aux = si4735.getCurrentRSSI();
        if (rssi != aux)
        {
            rssi = aux;
            showRSSI();
        }

        if (countRSSI++ > 3)
        {
            disableCommand(NULL, false, NULL); //Why?
            countRSSI = 0;
        }
        elapsedRSSI = millis();
    }

saveAttempt:
    //Save EEPROM if anough time passed and frequency changed
    if (currentFrequency != previousFrequency)
    {
        if ((millis() - storeTime) > STORE_TIME)
        {
            //Serial.write("save EEPROM\n");
            saveAllReceiverInformation();
            storeTime = millis();
            previousFrequency = currentFrequency;
        }
    }
    //delay(10);
}