# LN2 Remastered

GEARS/8pi LN2 system code

Maintainer: Jonathan Williams

## Description

Code for running the liquid nitrogen system at the SFU NSL.

## Usage

The program is split into `LN2_master` and `LN2_server` components, each in their own directories.  All of the program logic as well as the DAQ hardware interface lies in the `LN2_server` program.  Once the `LN2_server` program is running, it will listen for commands which may be sent from the `LN2_master` program.

## Commands

These are some of the commands which the `LN2_master` program can use to control the `LN2_server` program while it is running.

|**Command**|**Description**|
|:---:|:---:|
| `./LN2_master begin` | Begins the run.  The LN2 filling process will occur based on the schedule defined in schedule.dat. |
| `./LN2_master end` | Ends the run.  If currently filling, ends the filling process. |
| `./LN2_master fill detector_name` | Starts the dewar filling process immediately for the detector with name `detector_name` defined in schedule.dat. |
| `./LN2_master on X` | Manually turns on the DAQ switch `X`, where `X` is an integer (from 0 to 7 on the NIDAQ controller). |
| `./LN2_master off` | Manually turns off all DAQ switches, closing all valves. |
| `./LN2_master measure X` | Shows the voltage reading on DAQ channel `X`, where `X` is an integer (from 0 to 7 on the NIDAQ controller). |
| `./LN2_master table` | Prints recent sensor data in a table format. |
| `./LN2_master exit` | Ends the run and exits the `LN2_server` program. |


## Installation

Use `make` to compile in both the master and server directories.  The `NIDAQmxBase` library is needed to use the (default) nidaq controller for the `LN2_server`.  This configuration has been tested using g++ and GNU make on Scientific Linux/CentOS 7.

The `LN2_server` may also be compiled with a 'test' controller using `make LN2_server_test` which can be used for testing since it doesn't interface with DAQ hardware (and therefore doesn't rely on external libraries).  This configuration has been tested using g++ and GNU make on Ubuntu 16.04.
