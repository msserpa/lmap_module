#!/bin/bash
make; sudo dmesg --clear; sudo make mod-uninstall; sudo make mod-install;
