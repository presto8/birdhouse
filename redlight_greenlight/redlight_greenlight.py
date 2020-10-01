#!/usr/bin/env python

from flask import Flask, request, Response
import googlemaps       # sudo pip install googlemaps
import geopy.distance
import re
import os
import hashlib          # for md5
import toml
import sys

from thingsboard_api_tools import TbApi # sudo pip install git+git://github.com/eykamp/thingsboard_api_tools.git --upgrade


def build_config_path(basename="birdhouse.cfg"):
    if 'XDG_CONFIG_HOME' in os.environ:
        return os.path.join(os.environ['XDG_CONFIG_HOME'], basename)
    else:
        return os.path.join(os.environ['HOME'], '.config', basename)


def show_config_help(path):
    print(path + """ does not exist; please create it with the following contents:
motherShipUrl = "http://localhost:8080"
username = "fixme"
password = "fixme"
data_encoding = "utf-8"
google_geolocation_key = "fixme"
firmware_images_folder = "fixme"
""")


try:
    CFG = toml.load(build_config_path())
except FileNotFoundError:
    show_config_help(build_config_path())
    sys.exit(1)

tbapi = TbApi(CFG['motherShipUrl'], CFG['username'], CFG['password'])
gmaps = googlemaps.Client(key=CFG['google_geolocation_key'])
app = Flask(__name__)


def get_immediate_subdirectories(a_dir):
    return [name for name in os.listdir(a_dir) if os.path.isdir(os.path.join(a_dir, name))]


@app.route("/wifi_location")
def wifi_location():
    if request.json is None:
        return "did not provide any request data", 400

    hotspots = request.json.get("visibleHotspots", [])
    print("Geolocating for {} ".format(hotspots))

    try:
        results = gmaps.geolocate(wifi_access_points=hotspots)
    except Exception as ex:
        return "Exception while geolocating: {}".format(ex), 500

    if "error" in results:
        return "Received error from Google API!", 500

    try:
        gmaps_coord = (results["location"]["lat"], results["location"]["lng"])
        gmaps_acc = results["accuracy"]
    except Exception:
        return "Error parsing response from Google Location API!", 500

    known_coord = (request.json.get("latitude", 0), request.json.get("longitude", 0))
    print("Calculating distance between {} and {}...".format(known_coord, gmaps_coord))
    try:
        distance = geopy.distance.distance(known_coord, gmaps_coord)
    except Exception:
        return "geopy.distance had an error", 500

    outgoing_data = {"wifiDistance": distance.m, "wifiDistanceAccuracy": gmaps_acc}

    if 'device_token' in request.json:
        device_token = request.json["device_token"]
        print("Sending ", outgoing_data)
        try:
            tbapi.send_telemetry(device_token, outgoing_data)
        except Exception:
            return "Error sending location telemetry!", 500

    return outgoing_data


# Returns a copy of the latest version of the firmware
@app.route("/firmware")
def firmware():
    print("handling firmware request")
    return get_firmware(get_path_of_latest_firmware(CFG['firmware_images_folder']))


def get_path_of_latest_firmware(folder, current_major=0, current_minor=0):
    newest_firmware = None
    newest_major = current_major
    newest_minor = current_minor

    for file in os.listdir(folder):
        candidate = re.search(r"(\d+)\.(\d+).bin", file)
        if candidate:
            major = int(candidate.group(1))
            minor = int(candidate.group(2))

            if major > newest_major or (major == newest_major and minor > newest_minor):
                newest_major = major
                newest_minor = minor
                newest_firmware = os.path.join(folder, file)

    return newest_firmware


def get_firmware(full_filename):
    with open(full_filename, 'rb') as file:
        bin_image = file.read()

    byte_count = str(len(bin_image))
    md5 = hashlib.md5(bin_image).hexdigest()

    print("Sending firmware (" + byte_count + " bytes), with hash " + md5)

    response = Response(bin_image, mimetype="application/octet-stream")
    # response.headers["Content-transfer-encoding"] = "base64"
    response.headers["X-MD5"] = md5
    return response


def find_firmware_folder(current_version, mac_address):
    v = re.search(r"(\d+)\.(\d+)", current_version)
    current_major = int(v.group(1))
    current_minor = int(v.group(2))

    device_specific_subfolder = None

    # If there is a dedicated folder for this device, search there; if not, use the default firmware_images_folder
    subdirs = get_immediate_subdirectories(CFG['firmware_images_folder'])

    for subdir in subdirs:
        print(subdir)
        # Folders will have a name matching the pattern SOME_READABLE_PREFIX + underscore + MAC_ADDRESS
        if re.match(".*_" + mac_address.upper(), subdir):
            if device_specific_subfolder is not None:
                print("Error: found multiple folders for mac address " + mac_address)
                return None
            device_specific_subfolder = subdir

    if device_specific_subfolder is None:
        folder = CFG['firmware_images_folder']
    else:
        folder = os.path.join(CFG['firmware_images_folder'], device_specific_subfolder)

    print("Using firmware folder " + folder)

    if not os.path.isdir(folder):
        print("Error>>> " + folder + " is not a folder!")
        return

    return get_path_of_latest_firmware(folder, current_major, current_minor)


@app.route("/update/<status>")
def handle_update(status):
    # Returns the full file/path of the latest firmware, or None if we are already running the latest
    print("Handling update request")
    print(status)
    current_version = request.headers['HTTP_X_ESP8266_VERSION']
    mac = request.headers['HTTP_X_ESP8266_STA_MAC']
    print("Mac %s" % mac)
    # Other available headers
    # 'HTTP_CONNECTION': 'close',
    # 'HTTP_HOST': 'www.sensorbot.org:8989',
    # 'HTTP_USER_AGENT': 'ESP8266-http-Update',
    # 'HTTP_X_ESP8266_AP_MAC': '2E:3A:E8:08:2C:38',
    # 'HTTP_X_ESP8266_CHIP_SIZE': '4194304',
    # 'HTTP_X_ESP8266_FREE_SPACE': '2818048',
    # 'HTTP_X_ESP8266_MODE': 'sketch',
    # 'HTTP_X_ESP8266_SDK_VERSION': '2.2.1(cfd48f3)',
    # 'HTTP_X_ESP8266_SKETCH_MD5': '3f74331d79d8124c238361dcebbf3dc4',
    # 'HTTP_X_ESP8266_SKETCH_SIZE': '324512',
    # 'HTTP_X_ESP8266_STA_MAC': '2C:3A:E8:08:2C:38',
    # 'HTTP_X_ESP8266_VERSION': '0.120',

    # Use passed url params to display a debugging payload -- all will be read as strings; specify defaults in web.input() call to avoid exceptions for missing values
    # params = web.input(mqtt_status='Not specified')
    # mqtt_status = params.mqtt_status
    # print("MQTT status:", mqtt_status)

    newest_firmware = find_firmware_folder(current_version, mac)

    if newest_firmware:
        print("Upgrading birdhouse to " + newest_firmware)
        return get_firmware(newest_firmware)
    else:
        print("Birdhouse already at most recent version (" + current_version + ")")
        return None, 302


@app.route("/validatekey", methods=["GET"])
def validatekey():
    # Pass two args: name and key.  Returns "true" if key is the correct secret key for named device, "false" otherwise.
    # Used to validate whether users have input correct secret key when configuring devices.
    try:
        name = request.args['name']
        key = request.args['key']
    except KeyError:
        return "Please specify 'name' and 'key' params", 401

    device = tbapi.get_device_by_name(name)

    if device is None:
        return "bad_device"

    token = tbapi.get_device_token(device)

    return "true" if token == key else "false"


def main():
    app.run(port=8080, debug=True)


if __name__ == "__main__":
    main()
