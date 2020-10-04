#!/bin/bash

# create the calendar data from thunderbird profile
cp /nas/thunderbird/ib5vsgs6.shared/calendar-data/local.sqlite cpp/local.sqlite
cd cpp
./calendar
