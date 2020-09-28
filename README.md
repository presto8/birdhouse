# birdhouse
Tools for the birdhouse project.

Read more: http://sensorbot.org


Components
==========

- `redlight_greenlight` runs on sensorbot.org and is used to provide support
  services to the devices and other scripts.  For example, when
  installing firmware on a new device, the firmware loader can get a compiled
  copy of the firmware off the server, making it easier for people to flash a
  new device without compiling their own firmware from source.  Likewise with
  the device key confirmation service -- this can be used during provisioning a
  new device to make sure people entered their key properly.


Developer installation
======================

This section will walk you through setting up an isolated development
environment that does not impact the rest of your system. This has been tested
on Linux, other instructions may be necessary for other operating systems.

Requirements:
- Python >= 3.7
- Virtualenv

First, set up virtualenv to isolate the Python modules:

    virtualenv venv   # if necessary: add --python=$(which python3.7) to force specific python version
    source venv/bin/activate
    pip install --editable .

Now try and run the `rlgl` executable to make sure everything is working:

    rlgl

Finally, create a symbolic link to be able to run rlgl even when the virtualenv
is not activated.

    sudo ln -s $PWD/venv/bin/rlgl /usr/local/bin/rlgl

The virtualenv doesn't really need to be activated again except to run the tests.
