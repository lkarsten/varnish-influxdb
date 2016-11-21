InfluxDB UDP export for Varnish Cache
=====================================

This is a C-implementation of exporter for Varnish Cache to InfluxDB.

It uses the UDP data format and aims to use as little resources as possible.

Future possible features:
* extract varnishlog Timestamp records to store ttfb and similar to InfluxDB.

As a minor bonus point, it illustrates how to use CMake when building software
that uses the Varnish shared memory segment.

Example
-------

With an InfluxDB server running on influxdb.example.com, and a UDP listener on
port 4444, use this command:

::

    $ influxstat influxdb.example.com 4444


Default tags are "service=varnish,hostname=$HOSTNAME". To add additional ones
add "-P foo=bar,tag2=value".

See https://github.com/influxdata/influxdb/blob/master/services/udp/README.md#config-examples
on how to enable UDP on the server side.


Contact
-------

Written by Lasse Karstensen <lasse.karstensen@gmail.com>.
