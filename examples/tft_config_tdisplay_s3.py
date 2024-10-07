import amoled
from machine import Pin, SPI

#LILYGO TDISPLAY-S3 AMOLED
TFT_SD0 = Pin(08,Pin.OUT)   # SERIAL OUTPUT SIGNAL
TFT_TE  = Pin(09,Pin.OUT)   # TEARING EFFET CONTROL
TFT_CS  = Pin(06,Pin.OUT)   # CHIP SELECT
TFT_SCK = Pin(47,Pin.OUT)   # SPICLK_P
TFT_MOSI = None
TFT_MISO = None
TFT_RST = Pin(17,Pin.OUT)   # RESET
TFT_D0  = Pin(18,Pin.OUT)   # D0 QSPI
TFT_D1  = Pin(07,Pin.OUT)   # D1 QSPI
TFT_D2  = Pin(48,Pin.OUT)   # D2 QSPI & SPICLK_N
TFT_D3  = Pin(05,Pin.OUT)   # D3 QSPI
TFT_CDE = Pin(38,Pin.OUT)   #GPIO38 NEEDED FOR SCREEN ON
TFT_TS_IN = Pin(21, Pin.IN, Pin.PULL_UP) #INTERRUPT

def config():
    spi = SPI(2, sck=TFT_SCK, mosi=None, miso=None, polarity=0, phase=0)
    panel = amoled.QSPIPanel(
            spi=spi, data=(TFT_D0, TFT_D1, TFT_D2, TFT_D3),
            dc=TFT_D1, cs=TFT_CS, pclk=80_000_000, width=240, height=536)
    return amoled.AMOLED(panel, type=0, reset=TFT_RST, bpp=16)