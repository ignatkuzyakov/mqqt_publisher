# mqqt_publisher
API: https://api.data.gov.sg/v1/environment/air-temperature

## dependencies
```
sudo apt-get install -y libboost-all-dev mosquitto mosquitto-clients libmosquitto-dev openssl libssl-dev
```

## build
```
g++-13 mqqt_publisher.cpp -o mqqt_publisher -lssl -lcrypto -lmosquitto
```

## subscribe
```
mosquitto_sub -h test.mosquitto.org -p 8885 -t "api/status" --cafile ./certs/mosquitto.org.crt -u ro -P readonly
```
