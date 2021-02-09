.. _fdc2x1x:

FDC2X1X: Capacitance-to-Digital Converter
###################################

Overview
********

This sample application periodically reads raw data from the FDC2X1X sensor in polling mode 
or optionally with data ready trigger. It is able to read the 12-Bit and 28-Bit, as well as
the 2-Channel and 4-Channel versions (FDC2112, FDC2114, FDC2212, FDC2214). The 4-channel versions 
are chosen through devicetree properties. The default is FDC2112.


Wiring
*******

This sample uses the FDC2X1X sensor controlled using the I2C interface.
Connect supply **VDD** and **GND**. The supply voltage can be in
the 2.7V to 3.6V range.

Connect **SD** to a GPIO to control the Shutdown Mode.

Connect Interface: **SDA**, **SCL** and optionally connect **INTB** to a
interrupt capable GPIO.

For detailed description refer to the `FDC2X1X datasheet`_
at pages 4-5.


Building and Running
********************

This sample outputs sensor data to the console and can be read by any serial 
console program. It should work with any platform featuring a I2C interface.
In this example below the :ref:`nrf9160dk_nrf9160` board is used.


.. zephyr-app-commands::
   :zephyr-app: samples/sensor/fdc2x1x
   :board: nrf9160dk_nrf9160
   :goals: build flash
   :compact:
   
Sample Output: 2-Channel, 28-Bit (FDC2212)
=========================================

.. code-block:: console

        ch0: 17776649, ch1: 17767254
        ch0: 17776723, ch1: 17767624
        ch0: 17777168, ch1: 17767698
        ch0: 17777019, ch1: 17767402
        ch0: 17776945, ch1: 17767550

        <repeats endlessly>


Sample Output: 4-Channel, 12-Bit (FDC2114)
=========================================

.. code-block:: console

        ch0: 257, ch1: 256, ch2: 256, ch3: 251
        ch0: 257, ch1: 256, ch2: 256, ch3: 251
        ch0: 257, ch1: 256, ch2: 256, ch3: 251
        ch0: 257, ch1: 256, ch2: 256, ch3: 251
        ch0: 257, ch1: 256, ch2: 256, ch3: 251

        <repeats endlessly>


References
**********

FDC2X1X Datasheet and Product Info:
 https://www.ti.com/product/FDC2114

.. _FDC2X1X datasheet: https://www.ti.com/lit/gpn/fdc2114
