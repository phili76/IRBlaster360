# IRBlaster360 [![Build Status](https://travis-ci.org/phili76/IRBlaster360.svg?branch=platformio_prep)](https://travis-ci.org/phili76/IRBlaster360)

ESP8266 IR Blaster with FHEM Integration
[Link to FHEM forum thread](https://forum.fhem.de/index.php/topic,72950.0.html)

## New Features

* wireless signal strength on page
* copy url to clipboard
* webserver port configurable
* reboot button redirects to new webserver port

![rawgraph](/images/rawgraph.png)

## Features

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
