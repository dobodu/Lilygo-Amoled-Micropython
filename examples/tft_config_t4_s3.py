import amoled
from machine import Pin, SPI


#LILYGO T4-S3 TDISPLAY AMOLED
TFT_SD0 = Pin(08,Pin.OUT)   # SERIAL OUTPUT SIGNAL
TFT_CS  = Pin(11,Pin.OUT)   #CHECKED       CHIP SELECT
TFT_SCK = Pin(15,Pin.OUT)   #CHECKED       SPICLK_P
TFT_RST = Pin(13,Pin.OUT)   # RESET
TFT_D0  = Pin(14,Pin.OUT)   #CHECKED       D0 QSPI
TFT_D1  = Pin(10,Pin.OUT)   #CHECKED       D1 QSPI
TFT_D2  = Pin(16,Pin.OUT)   #CHECKED       D2 QSPI & SPICLK_N
TFT_D3  = Pin(12,Pin.OUT)   #CHECKED       D3 QSPI
TFT_CDE = Pin(09,Pin.OUT)   #CHECKED      GPIO09 NEEDED FOR SCREEN ON

def config():
    spi = SPI(2, sck=TFT_SCK, mosi=None, miso=None, polarity=0, phase=0)
    panel = amoled.QSPIPanel(
            spi=spi, data=(TFT_D0, TFT_D1, TFT_D2, TFT_D3),
            dc=TFT_D1, cs=TFT_CS, pclk=80_000_000, width=450, height=600)
            
    return amoled.AMOLED(panel, type=1, reset=TFT_RST, bpp=16)