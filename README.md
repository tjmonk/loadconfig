# loadconfig
Configuration Management Utility for VarServer

## Overview

The loadconfig utility manages system configuration by loading variable
data across one or more files.  It processes data from configuration files
one line at a time. Each line in a configuration file may be an @ directive, or
a var/value assignment.

- Lines begining with @ are directives
- Lines begining with # are comments
- Blank lines are ignored
- All other lines are assumed to be var/value pairs
- Every configuration file MUST begin with the @config directive

## Directives

The following directives are supported:

| | |
|---|---|
| directive | purpose |
| @config | specifies the description of the configuration file and must be present in the first line of every configuration file |
| @require | specifies another (mandatory) configuration file to process |
| @include | specifies another (optional) configuration file to process |
| @includedir | specifies a directory of configuration files to process |

## Variable Interpolation

The loadconfig utility supports variable interpolation in the configuration files.
Variables specified using the `${ }` notation will be resolved by the variable
server and replaced with the variable value before the line containing
the variable is processed.

This provides a powerful mechanism to generate variable values as combinations
of other variables.

Variable interpolation also works for directives allowing conditional processing
of configuration files depending on the value of a variable.  For example,
a ${hardware_model} variable can be used to load a configuration for
a specific hardware model by doing something like this:

```
@include ${hardware_model}.cfg
```

A sophisticated configuration tree can be processed using variable interpolation
like this.

## Example Configuration File
An example configuration file is shown below:

```
@config Main system configuration

# The main system configuration file is the configuration entry point
# and includes all other configurations

@include software.cfg
@require hardware.cfg

/sys/network/hostname  MyHostName
/sys/network/dhcp      1
/sys/network/ntp       0
```

## Prerequisites

The loadconfig utility requires the following components:

- varserver : variable server ( https://github.com/tjmonk/varserver )

The included test example below requires the following components:

- varcreate : create variables from a JSON file ( https://github.com/tjmonk/varcreate )

## Build

```
$ ./build.sh
```

## Test

```
$ varserver &

$ varcreate test/vars.json

$ loadconfig -v -f test/init.cfg
ProcessConfigFile: /etc/loadconfig/init.cfg
Processing System Configuration
Including /etc/loadconfig/hardware.cfg
ProcessConfigFile: /etc/loadconfig/hardware.cfg
Processing Hardware Configuration
Setting /sys/hw/id to bbg
Including /var/loadconfig/hw.cfg
ProcessConfigFile: /var/loadconfig/hw.cfg
Including /etc/loadconfig/bbg.cfg
ProcessConfigFile: /etc/loadconfig/bbg.cfg
Processing Beaglebone Green Configuration
Setting /sys/hw/model to Beaglebone Green
Setting /sys/hw/manufacturer to seeed
Setting /sys/hw/cpu to am335x
Setting /sys/hw/rev to A
Including /etc/loadconfig/app.cfg
ProcessConfigFile: /etc/loadconfig/app.cfg
Processing Application Configuration
Setting /sys/app/id to tgp
Including /var/loadconfig/app.cfg
ProcessConfigFile: /var/loadconfig/app.cfg
Including /etc/loadconfig/tgp.cfg
ProcessConfigFile: /etc/loadconfig/tgp.cfg
Processing The Gateway Project
Setting /sys/app/version to 1.0
Setting /sys/app/name to The Gateway Project
Setting /sys/app/shortname to tgp
Setting /sys/app/description to Event Driven Multi-Process Demonstrator Project
Setting /sys/app/guid to f791a739-e0c8-4241-92e3-53b293377aa4
Setting /sys/app/modified to 2023-06-01 17:44:45Z
Setting /sys/app/copyright to (c)2022 Trevor Monk
Setting /sys/app/license to MIT
Including /var/loadconfig/user.cfg
ProcessConfigFile: /var/loadconfig/user.cfg
```
