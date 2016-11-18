InfluxDB UDP export for Varnish Cache
=====================================

This is a C-implementation of exporter for Varnish Cache to InfluxDB.

It uses the UDP data format and aims to use as little resources as possible.

Feature(s):
* export Varnish counters as often as you want.

Future possible features:
* extract varnishlog Timestamp records to store ttfb and similar to InfluxDB.

As a minor bonus point, it illustrates how to use CMake when building software
that uses the Varnish shared memory segment.

Contact
-------

Written by Lasse Karstensen <lasse.karstensen@gmail.com>.
