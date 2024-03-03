#pragma once

//Removed, but can be easy implemented. Required Si4735 chip, RDS is not supported in 32-34 chips
#define USE_RDS       0 
//On 1: Atm328p is running on full clock speed. 
//On 0: Atm328p is running on half clock speed. 
#define FASTER_CPU    1 

//EEPROM Settings
#define STORE_TIME 10000 // Inactive time to save our settings

//Band types
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

// Band settings
#define SW_LIMIT_LOW    1700
#define SW_LIMIT_HIGH   30000