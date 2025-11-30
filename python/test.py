import hue

import logging

logging.basicConfig(level=logging.DEBUG)

reader = hue.Reader("C:\\Users\\pooh\\Saved Games\\Frontier Developments\\Elite Dangerous")

reader.start()

for event in reader.events():
    print(event)