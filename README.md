This provides a websocketbased webfft for the rtl-sdr.

This is only a proof of concept as it only supports one connection.

Installation:
- Install librtlsdr
- Install python-websockify
- make
- ./run.sh
- http://localhost:8080


![Screenshot](http://i.imgur.com/DxPryek.png)

This is based on [rtlizer](https://github.com/csete/rtlizer) and (for the FFT color precalculation) on [gqrx](https://github.com/csete/gqrx)
