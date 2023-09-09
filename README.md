# mqqt_publisher

## dependencies
```
sudo apt-get install -y libboost-all-dev mosquitto mosquitto-clients libmosquitto-dev openssl libssl-dev
```

## build
```
g++ mqqt_publisher.cpp -o mqqt_publisher -lssl -lcrypto -lmosquitto
```

## subscribe
```
mosquitto_sub -h test.mosquitto.org -p 8885 -t "api/temperature/S60" --cafile ./certs/mosquitto.org.crt -u ro -P readonly
```
