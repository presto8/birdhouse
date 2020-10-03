import pytest
from redlight_greenlight import redlight_greenlight
import hashlib
from unittest.mock import MagicMock


@pytest.fixture
def app():
    app = redlight_greenlight.app
    return app


def test_wifi_location(client):
    # missing JSON payload should return HTTP 400
    assert client.post('/wifi_location').status_code == 400

    # send a lat, long, and hotspots, but no device_token
    # should get HTTP 200 with result but no telemetry upload
    hotspots = [{"macAddress":"B0:B2:DC:D5:0F:1D","signalStrength":-78, "age": 0, "channel":1,"signalToNoiseRatio": 0 },{"macAddress":"2E:3A:E8:08:2C:38","signalStrength":-69, "age": 0, "channel":1,"signalToNoiseRatio": 0 },{"macAddress":"B6:E6:2D:25:87:C5","signalStrength":-31, "age": 0, "channel":1,"signalToNoiseRatio": 0 },{"macAddress":"B6:E6:2D:23:C9:C4","signalStrength":-41, "age": 0, "channel":1,"signalToNoiseRatio": 0 },{"macAddress":"90:84:0D:D6:79:3F","signalStrength":-77, "age": 0, "channel":1,"signalToNoiseRatio": 0 },{"macAddress":"E0:B9:E5:06:E0:B6","signalStrength":-92, "age": 0, "channel":1,"signalToNoiseRatio": 0 },{"macAddress":"8C:04:FF:37:DF:9B","signalStrength":-89, "age": 0, "channel":1,"signalToNoiseRatio": 0 },{"macAddress":"2A:F5:A2:80:56:1F","signalStrength":-88, "age": 0, "channel":3,"signalToNoiseRatio": 0 },{"macAddress":"EC:08:6B:90:77:A8","signalStrength":-89, "age": 0, "channel":1,"signalToNoiseRatio": 0 },{"macAddress":"B0:B9:8A:F9:22:D8","signalStrength":-88, "age": 0, "channel":6,"signalToNoiseRatio": 0 },{"macAddress":"86:EA:E8:E3:A6:30","signalStrength":-88, "age": 0, "channel":6,"signalToNoiseRatio": 0 },{"macAddress":"4E:7A:8A:4C:CD:B2","signalStrength":-89, "age": 0, "channel":6,"signalToNoiseRatio": 0 },{"macAddress":"FE:64:4B:0F:6E:78","signalStrength":-84, "age": 0, "channel":6,"signalToNoiseRatio": 0 },{"macAddress":"E8:37:7A:FA:C7:AA","signalStrength":-89, "age": 0, "channel":11,"signalToNoiseRatio": 0 },{"macAddress":"E6:3E:FC:8C:E8:70","signalStrength":-91, "age": 0, "channel":11,"signalToNoiseRatio": 0 }]
    request_data = dict(latitude=45.50080533, longitude=-122.64446517, visibleHotspots=hotspots)
    response = client.post('/wifi_location', json=request_data)
    assert response.status_code == 200
    assert response.json['wifiDistanceAccuracy'] == 30
    assert int(response.json['wifiDistance']) == 6


def test_firmware(client, tmpdir):
    fw_dir = tmpdir.mkdir('firmwares')
    redlight_greenlight.CFG['firmware_images_folder'] = fw_dir

    # firmware not found should yield 404
    assert client.get('/firmware').status_code == 404

    # create a firmware
    fw_file = fw_dir / "1.0.bin"
    fw_contents = b"hello, world\x01\x02"
    fw_file.write(fw_contents)

    # check firmware is returned along with correct hash
    resp = client.get('/firmware')
    assert resp.status_code == 200
    assert resp.data == fw_contents
    assert resp.headers['X-MD5'] == hashlib.md5(fw_contents).hexdigest()


def test_validate_token(client):
    # Mock the tbapi so we can unit test offline
    tbapi = redlight_greenlight.tbapi = MagicMock(redlight_greenlight.tbapi)
    tbapi.get_device_by_name = lambda x: 'valid_name'
    tbapi.get_device_token = lambda x: 'valid_token'

    # missing parameters should return 401
    assert client.get('/validate_token').status_code == 401

    # wrong token
    resp = client.get('/validate_token', query_string=dict(name='unknown_name', token='abcd'))
    assert resp.data == b"false"

    # valid request
    resp = client.get('/validate_token', query_string=dict(name='valid_name', token='valid_token'))
    assert resp.data == b"true"


