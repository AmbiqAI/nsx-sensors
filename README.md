# nsx-sensors

External NSX module for reusable I2C/SPI-attached sensor and accessory device
drivers.

Current drivers:

- MAX86150 ECG/PPG sensor
- MPU6050 IMU
- ICM-45605 6-axis IMU (SPI)
- INA228 power and current monitor
- SparkFun Qwiic LED Stick

The module is intentionally external to the unified SDK baseline. It builds on
focused transport modules such as `nsx-i2c` and exposes a more consistent
context-based initialization pattern across drivers.
