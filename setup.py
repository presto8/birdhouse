from setuptools import setup, find_packages

setup(
    name='birdhouse',
    version='0.1',
    python_requires='>=3.7',
    packages=find_packages(),
    include_package_data=True,
    install_requires=[
        'pytest',
        'pytest-mock',
        'Flask',
        'googlemaps',
        'geopy',
        'toml',
        'thingsboard_api_tools @ https://github.com/eykamp/thingsboard_api_tools/archive/master.zip',
    ],
    entry_points='''
        [console_scripts]
        rlgl=redlight_greenlight.redlight_greenlight:main
    ''',

    author="Christopher Eykamp",
    author_email="chris@eykamp.com",
    description="Sensorbot",
    keywords="python sensorbot aqi air quality",
    url="https://github.com/eykamp/birdhouse",
)
