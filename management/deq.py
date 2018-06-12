# Copyright 2018, Chris Eykamp

# MIT License

# Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
# documentation files (the "Software"), to deal in the Software without restriction, including without limitation the
# rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit
# persons to whom the Software is furnished to do so, subject to the following conditions:

# The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
# Software.

# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
# WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
# COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
# OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

import re
import datetime, time, pytz
import sys
import deq_tools                                                    # pip install deq_tools

from thingsboard_api_tools import TbApi                             # pip install git+git://github.com/eykamp/thingsboard_api_tools.git --upgrade
from provision_config import motherShipUrl, username, password      # You'll need to create this!

tbapi = TbApi(motherShipUrl, username, password)

device_name = 'DEQ (SEL)'
deq_tz_name = 'America/Los_Angeles'


# Data is stored as if it were coming from one of our devices
device = tbapi.get_device_by_name(device_name)
device_token = tbapi.get_device_token(tbapi.get_id(device))


# This is the earliest timestamp we're interested in.  The first time we run this script, all data since this date will be imported. 
# Making the date unnecessarily early will make this script run very slowly.
earliest_ts = "2018/04/28T00:00"        # DEQ uses ISO datetime format: YYYY/MM/DDTHH:MM

station_id = 2              # 2 => SE Lafayette, 7 => Sauvie Island, 51 => Gresham Learning Center.  See bottom of this file more more stations.




def main():

    from_ts = get_from_ts(device)           # Our latest and value, or earliest_ts if this is the inogural run
    to_ts   = make_deq_date_from_ts(int(time.time() * 1000))       # Now

    print(from_ts, to_ts)

    exit()

    data = deq_tools.get_data(station_id, from_ts, to_ts)

    for d in data:

        outgoing_data = {}

        if "PM2.5 Est"           in data[d]:
            outgoing_data["pm25"] = data[d]["PM2.5 Est"]          
            
        if "Ambient Temperature" in data[d]:
            outgoing_data["temperature"] = data[d]["Ambient Temperature"]
            
        if "Barometric Pressure" in data[d]:
            outgoing_data["pressure"] = data[d]["Barometric Pressure"]

        if len(outgoing_data) == 0:
            continue

        (month, day, year, hour, mins) = re.split('[/ :]', d)
        if(hour == '24'):
            hour = '0'

        pst = pytz.timezone(deq_tz_name)

        date_time = pst.localize(datetime.datetime(int(year), int(month), int(day), int(hour), int(mins)))
        ts = int(time.mktime(date_time.timetuple()) * 1000)

        try:
            print("Sending", outgoing_data)
            tbapi.send_telemetry(device_token, outgoing_data, ts)
        except Exception as ex:
            print("Error sending telemetry (%s)" % outgoing_data)
            raise(ex)


    print("Done")


# ts is in milliseconds
def make_deq_date_from_ts(ts):
    return datetime.datetime.fromtimestamp(ts / 1000).strftime('%Y/%m/%dT%H:%M')


# This function returns the ts to use as the beginning of the data range in our data request to DEQ.  It will return the
# ts for the most recently inserted DEQ data, or if data hasn't yet been inserted, it will return the value we set in 
# earliest_ts at the top of this file.
def get_from_ts(device):
    # Key used to determine last available telemetry -- this convoluted statement extracts the first value in our key_mapping dict
    # This is a somewhat arbitrary choice, but it will ensure we don't miss any data
    key = key_mapping[list(key_mapping.keys())[0]]        
    telemetry = tbapi.get_latest_telemetry(device, key)

    if telemetry[key][0]["value"] is None:      # We haven't stored any telemetry yet
        ts = earliest_ts
    else:
        ts = make_deq_date_from_ts(telemetry[key][0]["ts"])

    return ts


main()
