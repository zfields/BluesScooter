Blues Micro-Mobility Scooter
============================

Features
--------

* monitor power state
  * install voltage divider on "switch OUT" line of on/off switch
  * sensed via a GPIO connected to the voltage divider
  * used to control other behaviors, such as location tracking
    * when powered on - track location (either cancel geofence, or increase to a much larger diameter)
    * when powered off - save last location and set up geofence
* monitor main power (bike battery) charge/voltage
  * install voltage divider on "battery IN" line of on/off switch
* monitor orientation
  * can use the default accelerometer orientation
* horn beeps when a signal is sent from Notehub
  * use Twilio to generate signal
  * use dashboard to generate signal

### Stretch Goals
* receive alert when geofence is crossed
