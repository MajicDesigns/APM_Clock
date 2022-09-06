# A Clock built using Analog Panel Meters

Time is kept using a DS3231 clock modules with a I2C interface.

The display is implemented using 5 analog panel meters, usually used to display Voltage or current (eg, for a power supply). The 5 meters display Hours, Minutes, Day of Week, Date and Month with the faces relabeled to show:
- Hour: 0 thru 12
- Minute: 0 thru 60
- Day of week: Monday thru Sunday
- Date: 1 thru 31
- Month: Jan thru Dec

Allows setting the time using a MODE and SET switch, with an inactivity timeout. On timeout it abandons any changes.

Long press the MODE switch toggles summer time mode for easy set/reset and depressing the SET switch will show full scale on all meters (test mode).

More information on the Word Clock can be found in the blog article [Arduino++ blog](https://arduinoplusplus.wordpress.com/2022/09/06/analog-panel-meter-clock/)
