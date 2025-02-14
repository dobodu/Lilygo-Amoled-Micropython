#ifndef __AMOLED_H__
#define __AMOLED_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "py/obj.h"
#include "mpfile/mpfile.h"

#include "amoled_qspi_bus.h"

#define LCD_CMD_NOP          0x00 // This command is empty command
#define LCD_CMD_SWRESET      0x01 // Software reset registers (the built-in frame buffer is not affected)
#define LCD_CMD_RDDID        0x04 // Read 24-bit display ID
#define LCD_CMD_RDDST        0x09 // Read display status
#define LCD_CMD_RDDPM        0x0A // Read display power mode
#define LCD_CMD_RDD_MADCTL   0x0B // Read display MADCTL
#define LCD_CMD_RDD_COLMOD   0x0C // Read display pixel format
#define LCD_CMD_RDDIM        0x0D // Read display image mode
#define LCD_CMD_RDDSM        0x0E // Read display signal mode
#define LCD_CMD_RDDSR        0x0F // Read display self-diagnostic result
#define LCD_CMD_SLPIN        0x10 // Go into sleep mode (DC/DC, oscillator, scanning stopped, but memory keeps content)
#define LCD_CMD_SLPOUT       0x11 // Exit sleep mode
#define LCD_CMD_PTLON        0x12 // Turns on partial display mode
#define LCD_CMD_NORON        0x13 // Turns on normal display mode
#define LCD_CMD_INVOFF       0x20 // Recover from display inversion mode
#define LCD_CMD_INVON        0x21 // Go into display inversion mode
#define LCD_CMD_GAMSET       0x26 // Select Gamma curve for current display
#define LCD_CMD_DISPOFF      0x28 // Display off (disable frame buffer output)
#define LCD_CMD_DISPON       0x29 // Display on (enable frame buffer output)
#define LCD_CMD_CASET        0x2A // Set column address
#define LCD_CMD_RASET        0x2B // Set row address
#define LCD_CMD_RAMWR        0x2C // Write frame memory
#define LCD_CMD_RAMRD        0x2E // Read frame memory
#define LCD_CMD_PTLAR        0x30 // Define the partial area
#define LCD_CMD_VSCRDEF      0x33 // Vertical scrolling definition
#define LCD_CMD_TEOFF        0x34 // Turns of tearing effect
#define LCD_CMD_TEON         0x35 // Turns on tearing effect

#define LCD_CMD_MADCTL       0x36     // Memory data access control
#define LCD_CMD_MH_BIT       (1 << 2) // Display data latch order, 0: refresh left to right, 1: refresh right to left
#define LCD_CMD_BGR_BIT      (1 << 3) // RGB/BGR order, 0: RGB, 1: BGR
#define LCD_CMD_ML_BIT       (1 << 4) // Line address order, 0: refresh top to bottom, 1: refresh bottom to top
#define LCD_CMD_MV_BIT       (1 << 5) // Row/Column order, 0: normal mode, 1: reverse mode
#define LCD_CMD_MX_BIT       (1 << 6) // Column address order, 0: left to right, 1: right to left
#define LCD_CMD_MY_BIT       (1 << 7) // Row address order, 0: top to bottom, 1: bottom to top

#define LCD_CMD_VSCSAD       0x37 // Vertical scroll start address
#define LCD_CMD_IDMOFF       0x38 // Recover from IDLE mode
#define LCD_CMD_IDMON        0x39 // Fall into IDLE mode (8 color depth is displayed)
#define LCD_CMD_COLMOD       0x3A // Defines the format of RGB picture data
#define LCD_CMD_RAMWRC       0x3C // Memory write continue
#define LCD_CMD_RAMRDC       0x3E // Memory read continue
#define LCD_CMD_SETTSCANL    0x44 // Set tear scanline, tearing effect output signal when display module reaches line N
#define LCD_CMD_GETSCANL     0x45 // Get scanline
#define LCD_CMD_WRDISBV      0x51 // Write display brightness
#define LCD_CMD_RDDISBV      0x52 // Read display brightness value
//Below is specific for SH8601H
#define LCD_CMD_WRCTRLD1     0x53 // Write CTRL Display 1
#define LCD_CMD_RDCTRLD1	 0x54 // Read CTRL Display 1
#define LCD_CMD_WRCTRLD2     0x55 // Write CTRL Display 2
#define LCD_CMD_RDCTRLD2	 0x56 // Read CTRL Display 2
#define LCD_CMD_WRCE	 	 0x57 // Write Cde Sunlight Readability Enhancement
#define LCD_CMD_RDCE	 	 0x58 // Read Cde Sunlight Readability Enhancement
#define LCD_CMD_HBM_WRDISBV	 0x63 // Write Display Brightness Value in HBM Mode
#define LCD_CMD_HBM_RDDISBV  0x64 // Read Display Brightness Value in HBM Mode
#define LCD_CMD_HBMCTL 		 0x66 // HBM Control

#define LCD_CMD_SETHBMMODE  0xB0 // Set High Brightness Mode (only for RM67162)
#define LCD_CMD_SETDISPMODE 0xC2 // Set DSI Mode
#define LCD_CMD_SETSPIMODE  0xC4 // Set DSPI Mode
#define LCD_CMD_SWITCHMODE	0xFE // Switch Command Mode
#define LCD_CMD_READMODE	0xFF // Read Command Status

//RM680B0 and RM67162 factory registers
#define LCD_FAC_OVSSCONTROL 0x05 // OVSS Control
#define LCD_FAC_OVSSVOLTAGE 0x73 // OVSS Control
#define LCD_FAC_MIPI		0x26 // MIPI
#define LCD_FAC_SPI			0x24 // SPI
#define LCD_FAC_SWIRE1		0x5A // SWIRE
#define LCD_FAC_SWIRE2		0x5B // SWIRE

//RM67162 MADCTRL and RGB
#define RM67162_MADCTL_MY 0x80
#define RM67162_MADCTL_MX 0x40
#define RM67162_MADCTL_MV 0x20
#define RM67162_MADCTL_ML 0x10
#define RM67162_MADCTL_BGR 0x08
#define RM67162_MADCTL_MH 0x04
#define RM67162_MADCTL_RGB 0x00

//RM690B0 MADCTRL and RGB
#define RM690B0_MADCTL_MY 0x80
#define RM690B0_MADCTL_MX 0x40
#define RM690B0_MADCTL_MV 0x20
#define RM690B0_MADCTL_ML 0x10
#define RM690B0_MADCTL_BGR 0x08
#define RM690B0_MADCTL_MH 0x04
#define RM690B0_MADCTL_RGB 0x00

//SH8601 MADCTRL and RGB (SH8601 does not support rotations)
#define SH8601_MADCTL_BGR 0x08
#define SH8601_MADCTL_X_FLIP 0x02 // Flip Horizontal
#define SH8601_MADCTL_Y_FLIP 0x05 // Flip Vertical
#define SH8601_MADCTL_RGB 0x00

// Color definitions

#define BLACK   0x0000
#define BLUE    0x1F00
#define RED     0x00F8
#define GREEN   0xE007
#define CYAN    0xFF07
#define MAGENTA 0x1FF8
#define YELLOW  0xE0FF
#define WHITE   0xFFFF

#define COLOR_SPACE_RGB        (0)
#define COLOR_SPACE_BGR        (1)
#define COLOR_SPACE_MONOCHROME (2)

typedef struct _Point {
    mp_float_t x;
    mp_float_t y;
} Point;

typedef struct _Polygon {
    int length;
    Point *points;
} Polygon;

typedef struct _amoled_rotation_t {
    uint8_t madctl;
    uint16_t width;
    uint16_t height;
    uint16_t colstart;
    uint16_t rowstart;
} amoled_rotation_t;

typedef struct _amoled_AMOLED_obj_t {
    mp_obj_base_t base;
    mp_obj_base_t *bus_obj;
    amoled_panel_p_t *lcd_panel_p;
    mp_obj_t reset;
	mp_file_t *fp;              //File object
	uint16_t *pixel_buffer;		// resident buffer if buffer_size given
    bool reset_level;
    uint8_t color_space;
	
	// m_malloc'd pointers
    void *work;                 // work buffer for jpg & png decoding
    uint8_t *scanline_ringbuf;  // png scanline_ringbuf
    uint8_t *palette;           // png palette
    uint8_t *trans_palette;     // png trans_palette
    uint8_t *gamma_table;       // png gamma_table
	size_t  buffer_size;

	// Display parameters
    uint16_t width;
    uint16_t height;
    uint16_t max_width_value;
    uint16_t max_height_value;
    uint8_t rotation;
    amoled_rotation_t rotations[4];   // list of rotation tuples
    int x_gap;
    int y_gap;
	uint8_t type;
    uint32_t bpp;
    uint8_t fb_bpp;
    uint8_t madctl_val; // save current value of LCD_CMD_MADCTL register
    uint8_t colmod_cal; // save surrent value of LCD_CMD_COLMOD register

	//Frame Buffer related
    bool auto_refresh;
	bool hold_display;
	// frame_buffer is the whole display frame buffer
    size_t frame_buffer_size;
    uint16_t *frame_buffer;
	// partial_frame_buffer is a temporary frame buffer
	size_t partial_frame_buffer_size;
	uint16_t *partial_frame_buffer;  
	
} amoled_AMOLED_obj_t;

mp_obj_t amoled_AMOLED_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args);
extern const mp_obj_type_t amoled_AMOLED_type;

#ifdef  __cplusplus
}
#endif

#endif
