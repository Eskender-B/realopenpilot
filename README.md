# Real OpenPilot

Real OpenPilot tries to implement a real openpilot version of Commai's not so completely open sourced [openpilot](https://github.com/commaai/openpilot) android based driver asistance system.
The code is written in c++ and python and will run on a linux environment underneath Android on Android phones. This is achieved by running the code inside [termux](https://github.com/termux/termux-app) app.


## Status
It is at the very early stage. Current code only has the ff for now:
* [live-camera.c](live-camera.c) for 
	* Sending an Android intent to [termux-api](https://github.com/Eskender-B/termux-api) android app that accesses camera feed and streams it using Anonymous Unix socket.
	* Receiving the incoming camera feed and publishing it on ZMQ socket.

* [test-live-camera.py](test-live-camera.py)
	* Subscribes from ZMQ socket for testing

## TO DO
* So much

## Usage
For testing this code do:

	```
	$ make
	$ ./live-camera VideoStream
	```
In another terminal

	```
	$ export LD_PRELOAD=$PREFIX/lib/libpython3.6m.so # To make opencv work
	$ python test-live-camera.py
	```

## Dependency
* OpenCV see [here](https://github.com/Eskender-B/cross-compile-opencv-4Termux) for directly getting deb file or cross compiling
