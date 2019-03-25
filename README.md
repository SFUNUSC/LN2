# LN2 Remastered

GEARS/8pi LN2 system code

Maintainer: Jonathan Williams

## Description

Not-yet-battle-tested code for running the liquid nitrogen system.  Planned upgrades:

* Independent per-detector scheduling
* Decoupling of data, logic, and fill hardware

## Usage

The program is split into `LN2_master` and `LN2_server` components, each in their own directories.  All of the program logic as well as the DAQ hardware interface lies in the `LN2_server` program.  Once the `LN2_server` program is running, it will listen for commands which may be sent from the `LN2_master` program.

## Installation

Use `make` to compile in both the master and server directories.  The `NIDAQmxBase` library is needed to use the (default) nidaq controller for the `LN2_server`.  This configuration has been tested using g++ and GNU make on Scientific Linux/CentOS 7.

The `LN2_server` may also be compiled with a 'test' controller using `make LN2_server_test` which can be used for testing since it doesn't interface with DAQ hardware (and therefore doesn't rely on external libraries).  This configuration has been tested using g++ and GNU make on Ubuntu 16.04.
