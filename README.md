# IRBlaster360 [![Build Status](https://travis-ci.org/phili76/IRBlaster360.svg?branch=platformio_prep)](https://travis-ci.org/phili76/IRBlaster360)

ESP8266 IR Blaster with FHEM Integration
[Link to FHEM forum thread](https://forum.fhem.de/index.php/topic,72950.0.html)

## New Features

* Daikin AC Sending (work in progress) 
>http://irblaster.local/json?plain=[{"type":"ac","acmode":"5","acfan":5,"actemp":32,"acpower":on,"acswingv":1,"acswingh":0}]

Options:  
- acmode: 0 Auto, 1 Cool, 2 Heat, 3 Fan, 4 Dry
- acfan:  0 Auto, 1-5 Fanspeed, 6 Quiet
- actemp: 10-32 , set temp in Celsius
- acpower: on or off
- acswingv: 1 for vertical swing
- acswingh: 1 for horizontal swing

TODO:
- timed poweron/off
- Powerful, Sensor, Eco

![rawgraph](/images/rawgraph.png)

## Features

* wireless signal strength on page
* copy url to clipboard
* webserver port configurable
* reboot button redirects to new webserver port
* rawgraph to visualisize ir data
* update & reboot button on config page
* rewrite html creation to speed up javascript execution
* save config back to configfile
* config page shows defaults in () if not set
* get current time from ntp
* NTP Server configurable
* uptime display in webserver footer with bootdate
* set default mDNS hostname if not configured
* timestamp on received and sent codes
* configure settings in webinterface(see pic)

![config](/images/config2.png)
