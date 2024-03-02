# ATS_EX Firmware for ATS-20 DSP Receiver
### Руководство на русском языке можно прочитать здесь (Russian README is here) :
[>>> Ссылка на русский README. <<<](/rus/README.md)
## Basic description
This is advanced firmware for **ATS-20** that is working on **Arduino Nano** and **Si473x** DSP receiver chip.
ATS_EX is created by **Goshante**, based on **PU2CLR** firmware and inspired by **swling.ru** firmware with closed source.


**Latest version:** v1.01 (02.03.2024)

**Download binary .hex link:** [>>> Click here <<<](https://github.com/goshante/ats20_ats_ex/releases/download/v1.01/ATS_EX_v1.01.hex)


<p align="center">
    <img src="img/ats20.png" alt="Icon" />
</p>

# Features

 - Fully **reworked interface**. No more ugly stretched fonts. Minimalistic readable interface. 7-Segment frequency font is inspired by swling firmware, but created by me.
 - Fully **reworked controls**. You can read user manual below.
 - **BFO is now part of main frequency** and regulated by frequency step, it is no more dedicated option that makes frequency surfing experience terrible. SSB mode has more precise steps.
 - **LW** Band: From **149** KHz to **520** KHz
 - **MW** Band: From **520** to **1710** KHz
 - **SW** Band: From **1700** to **30000** KHz (With a lot of sub-bands)
 - **FM** Band: From **64** to **108** MHz. (Two sub-bands from **64** and from **84** MHz)
 - Added **CW mode**
 - All **SW bands** now feel like one large band **from 1700 to 30000 KHz**. It's possible to switch between them, but they no longer restrict the frequency step to the sub-band limits.
 - **The tuning across frequencies has become as smooth as possible** in SSB mode, thanks to the merging of the receive frequency with the BFO. The rough frequency switching now occurs every 16 KHz (**the seamless tuning in both directions covers a full 32 Hz**). In Non-SSB modes now you also can tune faster, **encoder should be more responsive**.
 - **A lot of steps are available for every mode**. In AM you have 1/5/9/10/50/100k/1M steps, in SSB you have 1/5/9/10k steps and 10/25/50/100Hz steps for more precise tuning. In FM mode you have 10k/100k/1M steps.
 - Added settings page. You can configure **Gain Control and ATT**, **Soft Mute**, **Automatic Volume Control** (AVC), On or Off **AVC for SSB mode**,  **DeEmphasis** for FM mode and enable or disable **SSB Sync** mode.
 - Adjustable **screen brightness**.
 - Added **Mute button** and **Display on/off button**.
 - Added **Battery charge status** (Requires physical mod: Solder **VCC pin** through voltage divider to **A2** pin)
 - ~~Added **S-Meter**~~ in development
 - **Atm328p controller is now running on it's full clock**. Controls have to be more responsive. (Don't know how it impacts on battery drain.)
 - Code refactoring, optimizations
 - Fixed some bugs
 # How to flash it on my receiver?
 You can use anything that is able to flash .hex firmware to Arduino. You only need Micro USB cable and **USB UART** driver (It probably will be driver for **CH341**). I recommend you to use **AVRDUDESS** if you use Windows. It's easy GUI tool that can dump and flash images to Atmel microcontrollers. Just select **"Arduino Nano (ATmega328P)"** from **Presets**, select your actual **COM Port** and **path to firmware** .hex file. Select **"Write"** in **Flash** section and press **Go**. 

Or you can build it yourself. I use Visual Studio 2022 with VSMicro extension that using Arduino IDE 1.8. You can use just Arduino IDE, build it yourself and upload from IDE.

# User manual
**ATTENTION:** After flashing it's strongly **recommended to reset EEPROM memory**. To do this just hold the **Encoder Button** while turning receiver on.
### Buttons

 1. **BAND+** : Short press to enter **Band selection mode**. Select band with **Encoder Rotation** and confirm with **encoder button** or press **BAND+** again. In **settings mode** this button is switching between **settings pages**.
 2. **VOL+** : Short press to enter **Volume regulation mode**. Set volume with **Encoder Rotation** and confirm with **Encoder Button** or press **VOL+** again.
 3. **STEP** : Short press to enter **Step regulation mode**. Set step with **Encoder Rotation** and confirm with **Encoder Button** or press **STEP** again.
 4. **AGC** : Short press to **toggle display on and off**. Long press to toggle **Sync** mode while **SSB** is active.
 5. **BAND-** : Short press to open or close **Settings screen**.
 6. **VOL-** : Short press to **toggle volume mute on and off**.
 7. **BW** : Short press to enter **Bandwidth regulation mode**. Set step with **Encoder Rotation** and confirm with **Encoder Button** or press **BW** again.
 8. **MODE** : Short press to **switch between modulations**. On **FM** band **WFM** is the only available modulation and **MODE** button is disabled. On all other bands next modulations are available: **AM/USB/LSB/CW**. In all modulations *(except AM and WFM (FM))* you have improved frequency tuning without interrupting every step.
 9. **Encoder Rotation** : Frequency **Tune** or settings **navigation**.
 10. **Encoder Button** :  Short press to **scan** for stations or **confirm** your selection. Resets **EEPROM** memory when held on startup.
### Settings
Navigate in settings with **Encoder Rotation**, confirm selection with **Encoder Button**, change value with **Encoder Rotation** and save it with **Encoder Button**. Close settings with **BAND-** button. Navigate between **settings pages** with **BAND+** button.

<p align="center">
    <img src="img/ats20_settings.png" alt="Icon" />
</p>

**ATT** : **Attenuation** value. **AUT** means **Auto Gain Congrol**. This value can be **AUT** and from **1** to **37**.

**SM** : **Soft Mute**. This is number from **0** to **32**.

**AVC** :  Automatic Volume Control. This is number from **12** to **90**.

**SVC** : Enable or disable **AVC for SSB**.

**DeE** :  Only for **FM** mode. It's **DeEmphasis** value in microseconds. It can be only **50** or **75**.

**Syn** : Enable or disable **Sync mode for SSB**.

**Scr** : **Display brightness** value. This is number from **5** to **125**.

### Display elements description

<p align="center">
    <img src="img/ats20_display.png" alt="Icon" />
</p>

 **1**. **Current modulation**. From **149** to **30000** KHz you have **AM/LSB/USB/CW** modulations available. When **Sync** is active in **SSB** modes, the  **S** letter will appear near modulation status. In **FM** band you have only **FM** (or **WFM**) modulation. I don't have **NBFM patch** for **Si473x**, so it's not available now.
 
 **2**. **Bandwidth** status. Can be **AUTO** in **FM** mode. Not available in **CW** mode.
 
 **3**.  **Current frequency**. In **FM** and **SSB** modes it has fractional part. 
 
  **4**.  **Band name**. There are such bands as **LW, MW, SW, CB** and **FM**.

**5**.  **Step value**. It is different for all modulations, **SSB has more precise steps**, but doesn't have large. AM has harger steps, but doesn't have precise. If it doesn't have a units suffix, that means that it's **Hz**.

**6**.  **Frequency units**. Units are always displayed, except when decimal part of frequency in **SSB** is **5 digits long**.

**7**.  **Volume**. This is sound volume of receiver. Could be value between **0** and **63**. When mute is enabled it displays letter **M** instead of volume.