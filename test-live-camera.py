# Simple image subscription tester
# Saves the last frame to test.png upon Ctrl-C

import zmq
import numpy as np
import cv2
import time

row = 240
col = 320
channel = 4 

context = zmq.Context()
subscriber = context.socket(zmq.SUB)
subscriber.connect("tcp://localhost:9999")
subscriber.setsockopt_string(zmq.SUBSCRIBE, '')

try:
    num = 0
    img = None
    t_ = time.time()
    while True:
        t0 = time.time()
        frame = subscriber.recv(0, False, True) 
        img = np.frombuffer(frame, np.uint8).reshape(row, col, channel)
        img = cv2.split(img)
        img = cv2.merge([img[0], img[1], img[2]]) 
        num += 1
        t1 = time.time()
        print("Frame " + str(num) + " at " + str((t1-t0)*1000) + " ms interval")

except KeyboardInterrupt:
    if num!=0:
        print("Avg rate = " + str((time.time()-t_)*1000/num))
        cv2.imwrite("test.png", img)
    
