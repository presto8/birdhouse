import pytest
from redlight_greenlight import redlight_greenlight


@pytest.fixture
def app():
    app = redlight_greenlight.app
    return app


def test_hotspots(client):
    # missing JSON payload
    assert client.get('/hotspots').status_code == 400

    hotspots = [{"macAddress":"B0:B2:DC:D5:0F:1D","signalStrength":-78, "age": 0, "channel":1,"signalToNoiseRatio": 0 },{"macAddress":"2E:3A:E8:08:2C:38","signalStrength":-69, "age": 0, "channel":1,"signalToNoiseRatio": 0 },{"macAddress":"B6:E6:2D:25:87:C5","signalStrength":-31, "age": 0, "channel":1,"signalToNoiseRatio": 0 },{"macAddress":"B6:E6:2D:23:C9:C4","signalStrength":-41, "age": 0, "channel":1,"signalToNoiseRatio": 0 },{"macAddress":"90:84:0D:D6:79:3F","signalStrength":-77, "age": 0, "channel":1,"signalToNoiseRatio": 0 },{"macAddress":"E0:B9:E5:06:E0:B6","signalStrength":-92, "age": 0, "channel":1,"signalToNoiseRatio": 0 },{"macAddress":"8C:04:FF:37:DF:9B","signalStrength":-89, "age": 0, "channel":1,"signalToNoiseRatio": 0 },{"macAddress":"2A:F5:A2:80:56:1F","signalStrength":-88, "age": 0, "channel":3,"signalToNoiseRatio": 0 },{"macAddress":"EC:08:6B:90:77:A8","signalStrength":-89, "age": 0, "channel":1,"signalToNoiseRatio": 0 },{"macAddress":"B0:B9:8A:F9:22:D8","signalStrength":-88, "age": 0, "channel":6,"signalToNoiseRatio": 0 },{"macAddress":"86:EA:E8:E3:A6:30","signalStrength":-88, "age": 0, "channel":6,"signalToNoiseRatio": 0 },{"macAddress":"4E:7A:8A:4C:CD:B2","signalStrength":-89, "age": 0, "channel":6,"signalToNoiseRatio": 0 },{"macAddress":"FE:64:4B:0F:6E:78","signalStrength":-84, "age": 0, "channel":6,"signalToNoiseRatio": 0 },{"macAddress":"E8:37:7A:FA:C7:AA","signalStrength":-89, "age": 0, "channel":11,"signalToNoiseRatio": 0 },{"macAddress":"E6:3E:FC:8C:E8:70","signalStrength":-91, "age": 0, "channel":11,"signalToNoiseRatio": 0 }]
    request_data = dict(latitude=45.50080533, longitude=-122.64446517, device_token="AABBCC", visibleHotspots=hotspots, notelemetry=True)
    assert client.get('/hotspots', json=request_data).status_code == 200
