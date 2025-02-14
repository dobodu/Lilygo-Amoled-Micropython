/* LILYGO AMOLED DRIVER FOR BOTH RM67162 AND RM690B0
  and WAVESHARE SH8601 

By Dobodu on behalf of 

RussHugues ST7789.mpy library
https://github.com/russhughes/st7789_mpy

Nspsck RM67162_Micropython_QSPI 
https://github.com/nspsck/RM67162_Micropython_QSPI

Xinyuan-LilyGO LilyGo-AMOLED-Series
https://github.com/Xinyuan-LilyGO/LilyGo-AMOLED-Series

This micropython C library is a standalone graphic library for

Lilygo T-Display S3 Amoled 1.91"
Lilygo T4-S3 Amoled 2.4"
Waveshare ESP32-S3 Touch Amoled 1.8"

License if public*/


#include "amoled.h"
#include "amoled_qspi_bus.h"

#include "py/obj.h"
#include "py/runtime.h"
#include "mphalport.h"
#include "py/gc.h"
#include "py/objstr.h"

#include "esp_lcd_panel_io.h"
#include "driver/spi_master.h"

#include "mpfile/mpfile.h"
#include "jpg/tjpgd565.h"

#include <string.h>
#include <math.h>
#include <wchar.h>

#define AMOLED_DRIVER_VERSION "09.02.2025"

#if MICROPY_VERSION >= MICROPY_MAKE_VERSION(1, 23, 0) 
#undef STATIC
#define STATIC static
#endif

#define _swap_int16_t(a, b) { int16_t t = a; a = b; b = t; }
#define ABS(N) (((N) < 0) ? (-(N)) : (N))
#define mp_hal_delay_ms(delay) (mp_hal_delay_us(delay * 1000))

//#define MAX_BUFFER_SIZE_IN_PIXEL  4800 // 600 * 8 = 4800

const char* color_space_desc[] = {
    "RGB",
    "BGR",
    "MONOCHROME"
};

/* Rotation memento (for RM690B0 and RM67162, SH8601 does not support it)

# = USB PORT
 
   +-----+  +----+  +---#-+  +----+
   |  1  |  |  2 |  |  3  |  # 4  |            
   +-#---+  |    #  +-----+  |    |
            +----+           +----+
*/

// Rotation Matrix { madctl, width, height, colstart, rowstart }
STATIC const amoled_rotation_t ORIENTATIONS_RM690B0[4] = {
    { RM690B0_MADCTL_RGB,										  450, 600, 16, 0},
    { RM690B0_MADCTL_MX | RM690B0_MADCTL_MV | RM690B0_MADCTL_RGB, 600, 450, 0, 16},
    { RM690B0_MADCTL_MX | RM690B0_MADCTL_MY | RM690B0_MADCTL_RGB, 450, 600, 16, 0},
    { RM690B0_MADCTL_MV | RM690B0_MADCTL_MY | RM690B0_MADCTL_RGB, 600, 450, 0, 16}
};

STATIC const amoled_rotation_t ORIENTATIONS_RM67162[4] = {
    { RM67162_MADCTL_RGB,										  240, 536, 0, 0},
    { RM67162_MADCTL_MX | RM67162_MADCTL_MV | RM67162_MADCTL_RGB, 536, 240, 0, 0},
    { RM67162_MADCTL_MX | RM67162_MADCTL_MY | RM67162_MADCTL_RGB, 240, 536, 0, 0},
    { RM67162_MADCTL_MV | RM67162_MADCTL_MY | RM67162_MADCTL_RGB, 536, 240, 0, 0}
};

STATIC const amoled_rotation_t ORIENTATIONS_SH8601[4] = {
    { SH8601_MADCTL_RGB, 												368, 448, 0, 0},
    { SH8601_MADCTL_X_FLIP | SH8601_MADCTL_RGB, 						368, 448, 0, 0},
    { SH8601_MADCTL_Y_FLIP | SH8601_MADCTL_RGB, 						368, 448, 0, 0},
    { SH8601_MADCTL_X_FLIP | SH8601_MADCTL_Y_FLIP | SH8601_MADCTL_RGB, 	368, 448, 0, 0}
};

int mod(int x, int m) {
    int r = x % m;
    return (r < 0) ? r + m : r;
}

int maxx(uint16_t x1, uint16_t x2) {
	return (x1 > x2) ? x1 : x2;
}

int minx(uint16_t x1, uint16_t x2) {
	return (x1 < x2) ? x1 : x2;
}

/*----------------------------------------------------------------------------------------------------
Below are transmission related functions.
-----------------------------------------------------------------------------------------------------*/

// send a buffer to the panel display memory using the panel tx_color
STATIC void write_color(amoled_AMOLED_obj_t *self, const void *buf, int len) {
    if (self->lcd_panel_p) {
            self->lcd_panel_p->tx_color(self->bus_obj, 0, buf, len);
    } else {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("Failed to find the panel object."));
    }
}

// send a buffer to the panel IC register using the panel tx_color
STATIC void write_spi(amoled_AMOLED_obj_t *self, int cmd, const void *buf, int len) {
    if (self->lcd_panel_p) {
            self->lcd_panel_p->tx_param(self->bus_obj, cmd, buf, len);
    } else {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("Failed to find the panel object."));
    }
}

/*----------------------------------------------------------------------------------------------------
Below are initialization related functions.
-----------------------------------------------------------------------------------------------------*/

STATIC void frame_buffer_alloc(amoled_AMOLED_obj_t *self, int len) {
    self->frame_buffer_size = len;
    self->frame_buffer = m_malloc(self->frame_buffer_size);
    
    if (self->frame_buffer == NULL) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("Failed to allocate Frame Buffer."));
    }
    memset(self->frame_buffer, 0, self->frame_buffer_size);
}

STATIC void set_rotation(amoled_AMOLED_obj_t *self, uint8_t rotation) {
    self->madctl_val &= 0x1F;
    self->madctl_val |= self->rotations[rotation].madctl;

    write_spi(self, LCD_CMD_MADCTL, (uint8_t[]) { self->madctl_val }, 1);

    self->width = self->rotations[rotation].width;
    self->max_width_value = self->width - 1;
    self->height = self->rotations[rotation].height;
    self->max_height_value = self->height - 1;
    self->x_gap = self->rotations[rotation].colstart;
    self->y_gap = self->rotations[rotation].rowstart;
}

STATIC void amoled_AMOLED_print(const mp_print_t *print,
                                 mp_obj_t          self_in,
                                 mp_print_kind_t   kind)
{
    (void) kind;
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(
        print,
        "<AMOLED bus=%p, reset=%p, color_space=%s, bpp=%u>, version=%s",
        self->bus_obj,
        self->reset,
        color_space_desc[self->color_space],
        self->bpp,
        AMOLED_DRIVER_VERSION
    );
}


mp_obj_t amoled_AMOLED_make_new(const mp_obj_type_t *type,
                                 size_t               n_args,
                                 size_t               n_kw,
                                 const mp_obj_t      *all_args)
{
    enum {
        ARG_bus,
		ARG_type,
        ARG_reset,
        ARG_reset_level,
        ARG_color_space,
        ARG_bpp,
        ARG_auto_refresh
    };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_bus,               MP_ARG_OBJ | MP_ARG_REQUIRED, {.u_obj = MP_OBJ_NULL}     },
	    { MP_QSTR_type,              MP_ARG_INT | MP_ARG_KW_ONLY,  {.u_int = 1}    			  },
        { MP_QSTR_reset,             MP_ARG_OBJ | MP_ARG_KW_ONLY,  {.u_obj = MP_OBJ_NULL}     },
        { MP_QSTR_reset_level,       MP_ARG_BOOL | MP_ARG_KW_ONLY, {.u_bool = false}          },
        { MP_QSTR_color_space,       MP_ARG_INT | MP_ARG_KW_ONLY,  {.u_int = COLOR_SPACE_RGB} },
        { MP_QSTR_bpp,               MP_ARG_INT | MP_ARG_KW_ONLY,  {.u_int = 16}              },
		{ MP_QSTR_auto_refresh,		 MP_ARG_INT | MP_ARG_KW_ONLY,  {.u_bool = true}          },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(
        n_args,
        n_kw,
        all_args,
        MP_ARRAY_SIZE(allowed_args),
        allowed_args,
        args
    );

    // create new object
    amoled_AMOLED_obj_t *self = m_new_obj(amoled_AMOLED_obj_t);
    self->base.type = &amoled_AMOLED_type;

    self->bus_obj = (mp_obj_base_t *)MP_OBJ_TO_PTR(args[ARG_bus].u_obj);
#ifdef MP_OBJ_TYPE_GET_SLOT
    self->lcd_panel_p = (amoled_panel_p_t *)MP_OBJ_TYPE_GET_SLOT(self->bus_obj->type, protocol);
#else
    self->lcd_panel_p = (amoled_panel_p_t *)self->bus_obj->type->protocol;
#endif

	//Display type 0 = TDisplay S3 RM61672 / 1 = T4-S3 RM690B0 / 2 = WAVESHARE SH8601
	self->type = args[ARG_type].u_int;

    // self->max_width_value etc will be initialized in the rotation later.
    self->width = ((amoled_qspi_bus_obj_t *)self->bus_obj)->width;
    self->height = ((amoled_qspi_bus_obj_t *)self->bus_obj)->height;
	
	self->auto_refresh = args[ARG_auto_refresh].u_bool;

    // 2 bytes for each pixel. so maximum will be width * height * 2
    frame_buffer_alloc(self, self->width * self->height * 2);
    
    self->reset       = args[ARG_reset].u_obj;
    self->reset_level = args[ARG_reset_level].u_bool;
    self->color_space = args[ARG_color_space].u_int;
    self->bpp         = args[ARG_bpp].u_int;

    // reset
    if (self->reset != MP_OBJ_NULL) {
        mp_hal_pin_obj_t reset_pin = mp_hal_get_pin_obj(self->reset);
        mp_hal_pin_output(reset_pin);
    }

	// set RGB or BGR
    switch (self->color_space) {
        case COLOR_SPACE_RGB:
            self->madctl_val = 0;
        break;

        case COLOR_SPACE_BGR:
            self->madctl_val |= (1 << 3);
        break;

        default:
            mp_raise_ValueError(MP_ERROR_TEXT("unsupported color space"));
        break;
    }

	// set BPP
    switch (self->bpp) {
        case 16:
            self->colmod_cal = 0x55;
            self->fb_bpp = 16;
        break;

        case 18:
            self->colmod_cal = 0x66;
            self->fb_bpp = 18;
        break;

        case 24:
            self->colmod_cal = 0x77;
            self->fb_bpp = 24;
        break;

        default:
            mp_raise_ValueError(MP_ERROR_TEXT("unsupported pixel width"));
        break;
    }

    bzero(&self->rotations, sizeof(self->rotations));
	switch (self->type) {
        case 0:
            memcpy(&self->rotations, ORIENTATIONS_RM67162, sizeof(ORIENTATIONS_RM67162));
        break;
        case 1:
            memcpy(&self->rotations, ORIENTATIONS_RM690B0, sizeof(ORIENTATIONS_RM690B0));
        break;
		case 2:
            memcpy(&self->rotations, ORIENTATIONS_SH8601, sizeof(ORIENTATIONS_SH8601));
        break;			
		default:
            mp_raise_ValueError(MP_ERROR_TEXT("Unsupported display type"));
        break;
	}
    set_rotation(self, 0);
    return MP_OBJ_FROM_PTR(self);
}

STATIC mp_obj_t amoled_AMOLED_version()
{   
    return mp_obj_new_str(AMOLED_DRIVER_VERSION, 10);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(amoled_AMOLED_version_obj, amoled_AMOLED_version);


STATIC mp_obj_t amoled_AMOLED_deinit(mp_obj_t self_in)
{
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(self_in);

    if (self->lcd_panel_p) {
        self->lcd_panel_p->deinit(self->bus_obj);
    }

    gc_free(self->frame_buffer);

    //m_del_obj(amoled_AMOLED_obj_t, self); 
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(amoled_AMOLED_deinit_obj, amoled_AMOLED_deinit);


STATIC mp_obj_t amoled_AMOLED_reset(mp_obj_t self_in)
{
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(self_in);

    if (self->reset != MP_OBJ_NULL) {
        mp_hal_pin_obj_t reset_pin = mp_hal_get_pin_obj(self->reset);
        mp_hal_pin_write(reset_pin, self->reset_level);
        mp_hal_delay_ms(300);    
        mp_hal_pin_write(reset_pin, !self->reset_level);
        mp_hal_delay_ms(200);    
    } else {
        write_spi(self, LCD_CMD_SWRESET, NULL, 0);
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(amoled_AMOLED_reset_obj, amoled_AMOLED_reset);


//Init function for RM67162 an RM690B0

STATIC mp_obj_t amoled_AMOLED_init(mp_obj_t self_in)
{
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(self_in);

	switch (self->type) {
        case 0:
            //Init mode for RM67162 
			write_spi(self, LCD_CMD_SWITCHMODE, (uint8_t[]) {0x05}, 1);  				// 0x05 SWITCH TO MANUFACTURING PAGE 4 COMMAND
			write_spi(self, LCD_FAC_OVSSCONTROL, (uint8_t[]) {0x05}, 1);				// OVSS control set elvss -3.95v
			write_spi(self, LCD_CMD_SWITCHMODE, (uint8_t[]) {0x01}, 1);					// 0x05 SWITCH TO MANUFACTURING PAGE 1 COMMAND
			write_spi(self, LCD_FAC_OVSSVOLTAGE, (uint8_t[]) {0x25}, 1);				// SET OVSS voltage level.= -4.0V
			write_spi(self, LCD_CMD_SWITCHMODE, (uint8_t[]) {0x00}, 1);  				// 0x00 SWITCH TO USER COMMAND
			write_spi(self, LCD_CMD_COLMOD, (uint8_t[]) { self->colmod_cal }, 1);		// Interface Pixel Format 0x75 16bpp x76 18bpp 0x77 24bpp
			write_spi(self, LCD_CMD_SETTSCANL, (uint8_t[]) {0x00, 0x80}, 2);  			// SET TEAR SCANLINE TO N = 0x0080 = 128
			write_spi(self, LCD_CMD_TEON, (uint8_t[]) {0x00}, 1);  						// TEAR ON
			write_spi(self, LCD_CMD_WRDISBV, (uint8_t[]) {0x00}, 1); 					// WRITE BRIGHTNESS MIN VALUE 0x00
			write_spi(self, LCD_CMD_SLPOUT, NULL, 0);     								// SLEEP OUT
			mp_hal_delay_ms(120); 
			write_spi(self, LCD_CMD_MADCTL, (uint8_t[]) { self->madctl_val,}, 1);		// WRITE MADCTL VALUES
			write_spi(self, LCD_CMD_DISPON, NULL, 0);									// DISPLAY ON
			write_spi(self, LCD_CMD_WRDISBV, (uint8_t[]) {0xFF}, 1);					// WRITE MAX BRIGHTNESS VALUE 0xFF 
        break;
        case 1:
			//Init mode for RM690B0
			write_spi(self, LCD_CMD_SWITCHMODE, (uint8_t[]) {0x20}, 1);  				// 0x20 SWITCH TO MANUFACTURING PANEL COMMAND
			write_spi(self, LCD_FAC_MIPI,(uint8_t[]) {0x0A}, 1);						// MIPI OFF
			write_spi(self, LCD_FAC_SPI,(uint8_t[]) {0x80}, 1);							// SPI Write ram
			write_spi(self, LCD_FAC_SWIRE1,(uint8_t[]) {0x51}, 1);						// ! 230918:SWIRE FOR BV6804
			write_spi(self, LCD_FAC_SWIRE2,(uint8_t[]) {0x2E}, 1);						// ! 230918:SWIRE FOR BV6804
			write_spi(self, LCD_CMD_SWITCHMODE, (uint8_t[]) {0x00}, 1);  				// 0x00 SWITCH TO USER COMMAND
			write_spi(self, LCD_CMD_CASET, (uint8_t[]) {0x00, 0x10, 0x01, 0xD1}, 4);  	// SET COLUMN START ADRESSE SC = 0x0010 = 16 and EC = 0x01D1 = 465 (450 columns but an 16 offset)
			write_spi(self, LCD_CMD_RASET, (uint8_t[]) {0x00, 0x00, 0x02, 0x57}, 4);	// SET ROW START ADRESS SP = 0 and EP = 0x256 = 599 (600 lines)
			write_spi(self, LCD_CMD_COLMOD, (uint8_t[]) { self->colmod_cal }, 1);		// Interface Pixel Format 0x75 16bpp x76 18bpp 0x77 24bpp 
			write_spi(self, LCD_CMD_SETDISPMODE, (uint8_t[]) {0x00}, 1); 				// Set DSI Mode to 0x00 = Internal Timmings
			//	write_spi(self, LCD_CMD_SETSPIMODE, (uint8_t[]) {0xA1}, 1); 			// 0xA1 = 1010 0001, first bit = SPI interface write RAM enable
			write_spi(self, LCD_CMD_SETTSCANL, (uint8_t[]) {0x01, 0x66}, 2);  			// SET TEAR SCANLINE TO N = 0x166 = 358
			write_spi(self, LCD_CMD_TEON, (uint8_t[]) {0x00}, 1); 						// TE ON
			write_spi(self, LCD_CMD_WRDISBV, (uint8_t[]) {0x00}, 1); 					// WRITE BRIGHTNESS VALUE 0x00
			write_spi(self, LCD_CMD_SLPOUT, NULL, 1); 									// SLEEP OUT
			mp_hal_delay_ms(120); 
			write_spi(self, LCD_CMD_MADCTL, (uint8_t[]) { self->madctl_val }, 1);		// WRITE MADCTL VALUES
			write_spi(self, LCD_CMD_DISPON, NULL, 1); 									// DISPLAY ON
			write_spi(self, LCD_CMD_WRDISBV, (uint8_t[]) {0xFF}, 1); 					// WRITE MAX BRIGHTNESS VALUE 0xFF   
		 break;
		 case 2:
			write_spi(self, LCD_CMD_SLPOUT, NULL, 0);									// SLEEP OUT
			mp_hal_delay_ms(120);
			write_spi(self, LCD_CMD_SETTSCANL, (uint8_t[]) {0x01, 0x2C}, 2);			// SET TEAR SCANLINE TO N = 0x012C = 300
			write_spi(self, LCD_CMD_COLMOD, (uint8_t[]) { self->colmod_cal }, 1);		// Interface Pixel Format 0x55 16bpp x66 18bpp 0x77 24bpp
			write_spi(self, LCD_CMD_MADCTL, (uint8_t[]) { self->madctl_val,}, 1);		// WRITE MADCTL VALUES
			write_spi(self, LCD_CMD_TEON, (uint8_t[]) {0x00}, 1);						// TEAR ON
			write_spi(self, LCD_CMD_WRCTRLD1, (uint8_t[]) {0x20}, 1);					// DISPLAY ON
			mp_hal_delay_ms(10);
			write_spi(self, LCD_CMD_CASET, (uint8_t[]) {0x00, 0x00, 0x01, 0x6F}, 4);  	// SET COLUMN START ADRESSE SC = 0x0000 = 0 and EC = 0x016F = 367
			write_spi(self, LCD_CMD_RASET, (uint8_t[]) {0x00, 0x00, 0x01, 0xBF}, 4);	// SET ROW START ADRESS SP = 0x0000 = 0 and EP = 0x01BF = 447
			write_spi(self, LCD_CMD_WRDISBV, (uint8_t[]) {0x00}, 1);					// WRITE BRIGHTNESS MIN VALUE 0x00
			mp_hal_delay_ms(10);
			write_spi(self, LCD_CMD_DISPON, NULL, 0);									// DISPLAY ON
			mp_hal_delay_ms(10);
			write_spi(self, LCD_CMD_WRDISBV, (uint8_t[]) {0xFF}, 1);					// WRITE BRIGHTNESS MAX VALUE 0xFF		
		break;
	}
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(amoled_AMOLED_init_obj, amoled_AMOLED_init);


STATIC mp_obj_t amoled_AMOLED_send_cmd(size_t n_args, const mp_obj_t *args_in)
{
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args_in[0]);
    uint8_t cmd = mp_obj_get_int(args_in[1]);
    uint8_t c_bits = mp_obj_get_int(args_in[2]);
    uint8_t len = mp_obj_get_int(args_in[3]);

    if (len <= 0) {
        write_spi(self, cmd, NULL, 0);
    } else {
        write_spi(self, cmd, (uint8_t[]){c_bits}, len);
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_send_cmd_obj, 4, 4, amoled_AMOLED_send_cmd);

/*-----------------------------------------------------------------------------------------------------
Below are drawing functions.
------------------------------------------------------------------------------------------------------*/

STATIC uint16_t colorRGB(uint8_t r, uint8_t g, uint8_t b) {
    uint16_t c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3);
    return ((((c) >> 8) & 0x00FF) | (((c) << 8) & 0xFF00));
}


STATIC mp_obj_t amoled_AMOLED_colorRGB(size_t n_args, const mp_obj_t *args_in) {
    return MP_OBJ_NEW_SMALL_INT(colorRGB(
        (uint8_t)mp_obj_get_int(args_in[1]),
        (uint8_t)mp_obj_get_int(args_in[2]),
        (uint8_t)mp_obj_get_int(args_in[3])));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_colorRGB_obj, 4, 4, amoled_AMOLED_colorRGB);


STATIC void set_area(amoled_AMOLED_obj_t *self, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    if ((x0 <= x1) & (x1 <= self->max_width_value) & (y0 <= y1) & (y1 <= self->max_height_value)) {

		/* As RM69090 driver need offset (see ORIENTATIONS_GENERAL) then the memory area needs to follow offsets*/
		x0 += self->x_gap;
		y0 += self->y_gap;
		x1 += self->x_gap;
		y1 += self->y_gap;

		uint8_t bufx[4] = {
			((x0 >> 8) & 0xFF),	// SC9 and SC8 are > 8 bits 
			(x0 & 0xFF),		// SC7 to SC0 are < 8 bits
			((x1 >> 8) & 0xFF),	// EC9 and EC8
			(x1 & 0xFF)};		// and so
		uint8_t bufy[4] = {
			((y0 >> 8) & 0xFF),	// SP9 and SP8...
			(y0 & 0xFF),
			((y1 >> 8) & 0xFF),
			(y1 & 0xFF)};
		uint8_t bufz[1] = { 0x00 };
	
		write_spi(self, LCD_CMD_CASET, bufx, 4);
		write_spi(self, LCD_CMD_RASET, bufy, 4);
		write_spi(self, LCD_CMD_RAMWR, bufz, 0);  /* strict copy of Lilygo AMOLED */
	}
}

//This function send a part of the frame_buffer to the display memory
STATIC void refresh_display(amoled_AMOLED_obj_t *self, uint16_t x, uint16_t y, uint16_t w, uint16_t h){
	
	// The SC[9:0] must be divisible by 2 (EVEN)
	uint16_t SC = x & 0xFE; 
	uint16_t SR = y & 0xFE;
	// EC[9:0]-SC[9:0]+1 must can be divisible by 2 (EVEN)
	// As EC is EVEN SC has to be odd
	uint16_t EC = (x+w-1) | 0x01;
	uint16_t ER = (y+h-1) | 0x01;
	
	//uint16_t EC = x + w - 1;
	//uint16_t ER = y + h - 1;
	size_t p_buf_idx;
	size_t buf_idx;
	
	if (self->auto_refresh) {
		/*if (EC & 0x1){ // Make odd if not
			} else {
			EC++;
			} 
		if (ER & 0x1){
			} else {
			ER++;
			}*/
		uint16_t w1 = EC - SC + 1 ; // Corrected width
		uint16_t h1 = ER - SR + 1;

		self->partial_frame_buffer_size = w1 * h1 * 2;
		self->partial_frame_buffer = m_malloc(self->partial_frame_buffer_size);

		//Copy frame_buffer to partial_frame_buffer
		p_buf_idx = 0;
		for (uint16_t line = 0; line < h1; line++) {
			buf_idx = ((SR + line) * self->width) + SC;
			memcpy(&self->partial_frame_buffer[p_buf_idx], &self->frame_buffer[buf_idx], 2 * w1);
			p_buf_idx += w1;
		}
		
		//Set display areas and write the partial frame buffer
		set_area(self, SC, SR, EC, ER);
		write_color(self, self->partial_frame_buffer, self->partial_frame_buffer_size);

		//Than partial frame buffer  memory and return
		m_free(self->partial_frame_buffer);
	}
}

STATIC mp_obj_t amoled_AMOLED_refresh(size_t n_args, const mp_obj_t *args_in) {
	amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args_in[0]);
	bool save_auto_refresh = self->auto_refresh;
	
	self->auto_refresh = true;
	refresh_display(self, 0, 0, self->width, self->height);
	self->auto_refresh = save_auto_refresh;
	
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_refresh_obj, 1, 1, amoled_AMOLED_refresh);


// This fill the frame buffer area 
// It has no dimension check, all should be done previously
STATIC void fill_frame_buffer(amoled_AMOLED_obj_t *self, uint16_t color, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
	
	size_t buf_idx;
	
    if (self->frame_buffer == NULL) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("No framebuffer available."));
    }

    color = (color << 16) | color;
	
	for (uint16_t line = 0; line < h; line++) {
		buf_idx = ((y + line) * self->width) + x;
		wmemset(&self->frame_buffer[buf_idx],color,w);
	}

    if ((!self->hold_display) & (self->auto_refresh)) {
		refresh_display(self,x,y,w,h);
	}
}


STATIC void pixel(amoled_AMOLED_obj_t *self, uint16_t x, uint16_t y, uint16_t color) {
	
	size_t buf_idx;
	if ((x <= self->max_width_value) & (y <= self->max_height_value)) {
		buf_idx = (y * self->width) + x;
		self->frame_buffer[buf_idx] = color;
		if (!self->hold_display & self->auto_refresh) {
			refresh_display(self,x,y,1,1);
		}
	}
}

STATIC mp_obj_t amoled_AMOLED_pixel(size_t n_args, const mp_obj_t *args_in) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args_in[0]);
    uint16_t x = mp_obj_get_int(args_in[1]);
    uint16_t y = mp_obj_get_int(args_in[2]);
    uint16_t color = mp_obj_get_int(args_in[3]);

    pixel(self, x, y, color);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_pixel_obj, 4, 4, amoled_AMOLED_pixel);


STATIC mp_obj_t amoled_AMOLED_fill(size_t n_args, const mp_obj_t *args_in) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args_in[0]);
    uint16_t color = mp_obj_get_int(args_in[1]);
	
    fill_frame_buffer(self, color, 0, 0, self->width , self->height);
    
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_fill_obj, 2, 2, amoled_AMOLED_fill);


STATIC void fast_hline(amoled_AMOLED_obj_t *self, uint16_t x, uint16_t y, uint16_t len, uint16_t color) {

	if ((x <= self->max_width_value) & (y <= self->max_height_value) & (len >0)){
		if (x + len > self->max_width_value) {
			len = self->max_width_value - x;
		} 
		fill_frame_buffer(self, color, x, y, len, 1);
	}
}

STATIC mp_obj_t amoled_AMOLED_hline(size_t n_args, const mp_obj_t *args_in) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args_in[0]);
    uint16_t x = mp_obj_get_int(args_in[1]);
    uint16_t y = mp_obj_get_int(args_in[2]);
    uint16_t len = mp_obj_get_int(args_in[3]);
    uint16_t color = mp_obj_get_int(args_in[4]);

    fast_hline(self, x, y, len, color);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_hline_obj, 5, 5, amoled_AMOLED_hline);


STATIC void fast_vline(amoled_AMOLED_obj_t *self, uint16_t x, uint16_t y, uint16_t len, uint16_t color) {
	if ((x <= self->max_width_value) & (y <= self->max_height_value) & (len >0)){
		if (y + len > self->max_height_value) {
			len = self->max_height_value - y;
		} 
		fill_frame_buffer(self, color, x, y, 1, len);
	}
}

STATIC mp_obj_t amoled_AMOLED_vline(size_t n_args, const mp_obj_t *args_in) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args_in[0]);
    uint16_t x = mp_obj_get_int(args_in[1]);
    uint16_t y = mp_obj_get_int(args_in[2]);
    uint16_t len = mp_obj_get_int(args_in[3]);
    uint16_t color = mp_obj_get_int(args_in[4]);

    fast_vline(self, x, y, len, color);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_vline_obj, 5, 5, amoled_AMOLED_vline);

STATIC void line(amoled_AMOLED_obj_t *self, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color) {
    bool steep = ABS(y1 - y0) > ABS(x1 - x0);
	bool saved_hold_display = self->hold_display;
	
    if (steep) {
        _swap_int16_t(x0, y0);
        _swap_int16_t(x1, y1);
    }

    if (x0 > x1) {
        _swap_int16_t(x0, x1);
        _swap_int16_t(y0, y1);
    }

    int16_t dx = x1 - x0, dy = ABS(y1 - y0);
    int16_t err = dx >> 1, ystep = -1, xs = x0, dlen = 0;

    if (y0 < y1) {
        ystep = 1;
    }
	
	self->hold_display = true;
	
    // Split into steep and not steep for FastH/V separation
    if (steep) {
        for (; x0 <= x1; x0++) {
            dlen++;
            err -= dy;
            if (err < 0) {
                err += dx;
                fast_vline(self, y0, xs, dlen, color);
                dlen = 0;
                y0 += ystep;
                xs = x0 + 1;
            }
        }
        if (dlen) {
            fast_vline(self, y0, xs, dlen, color);
        }
    } else {
        for (; x0 <= x1; x0++) {
            dlen++;
            err -= dy;
            if (err < 0) {
                err += dx;
                fast_hline(self, xs, y0, dlen, color);
                dlen = 0;
                y0 += ystep;
                xs = x0 + 1;
            }
        }
        if (dlen) {
            fast_hline(self, xs, y0, dlen, color);
        }
    }
	// Restore hold_display status
	self->hold_display = saved_hold_display;
	if (!self->hold_display & self->auto_refresh) {
		refresh_display(self,x0,y0,x1-x0,(y1>y0)?(y1-y0):(y0-y1));
	}
}


STATIC mp_obj_t amoled_AMOLED_line(size_t n_args, const mp_obj_t *args_in) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args_in[0]);
    uint16_t x0 = mp_obj_get_int(args_in[1]);
    uint16_t y0 = mp_obj_get_int(args_in[2]);
    uint16_t x1 = mp_obj_get_int(args_in[3]);
    uint16_t y1 = mp_obj_get_int(args_in[4]);
    uint16_t color = mp_obj_get_int(args_in[5]);

    line(self, x0, y0, x1, y1, color);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_line_obj, 6, 6, amoled_AMOLED_line);


STATIC void rect(amoled_AMOLED_obj_t *self, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {

	if (x + w > self->width || y + h > self->height || w == 0 || h ==0) {
		return;
	} 
	if (h == 1){
		fast_hline(self, x, y, w, color);
		return;
	}
	if (w == 1){
		fast_vline(self, x, y, h, color);
		return;
	}
	self->hold_display = true;
    fast_hline(self, x, y, w, color);
    fast_hline(self, x, y + h - 1, w, color);
    fast_vline(self, x, y, h, color);
    fast_vline(self, x + w - 1, y, h, color);
	self->hold_display = false;
	refresh_display(self,x,y,w,h);
}

STATIC mp_obj_t amoled_AMOLED_rect(size_t n_args, const mp_obj_t *args_in) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args_in[0]);
    uint16_t x = mp_obj_get_int(args_in[1]);
    uint16_t y = mp_obj_get_int(args_in[2]);
    uint16_t w = mp_obj_get_int(args_in[3]);
    uint16_t h = mp_obj_get_int(args_in[4]);
    uint16_t color = mp_obj_get_int(args_in[5]);

    rect(self, x, y, w, h, color);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_rect_obj, 6, 6, amoled_AMOLED_rect);

STATIC void fill_rect(amoled_AMOLED_obj_t *self, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {

/* Check are done to see if COL and ROW START ARE EVEN and COL and ROW END minus START are also divisible by 2*/

	if (x + w > self->width || y + h > self->height || w == 0 || h ==0) {
		return;
	}
	fill_frame_buffer(self, color, x, y, w, h);
}

STATIC mp_obj_t amoled_AMOLED_fill_rect(size_t n_args, const mp_obj_t *args_in) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args_in[0]);
    uint16_t x = mp_obj_get_int(args_in[1]);
    uint16_t y = mp_obj_get_int(args_in[2]);
    uint16_t w = mp_obj_get_int(args_in[3]);
    uint16_t l = mp_obj_get_int(args_in[4]);
    uint16_t color = mp_obj_get_int(args_in[5]);

    fill_rect(self, x, y, w, l, color);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_fill_rect_obj, 6, 6, amoled_AMOLED_fill_rect);

STATIC void trian(amoled_AMOLED_obj_t *self, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color) {

	uint16_t xmin = minx(minx(x0,x1),x2);
	uint16_t xmax = maxx(maxx(x0,x1),x2);
	uint16_t ymin = minx(minx(y0,y1),y2);
	uint16_t ymax = maxx(maxx(y0,y1),y2);

	self->hold_display = true;
	line(self, x0, y0, x1, y1, color);
	line(self, x1, y1, x2, y2, color);
	line(self, x0, y0, x2, y2, color);
	self->hold_display = false;
	refresh_display(self,xmin,ymin,xmax-xmin,ymax-ymin);
}

STATIC mp_obj_t amoled_AMOLED_trian(size_t n_args, const mp_obj_t *args_in) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args_in[0]);
    uint16_t x0 = mp_obj_get_int(args_in[1]);
    uint16_t y0 = mp_obj_get_int(args_in[2]);
	uint16_t x1 = mp_obj_get_int(args_in[3]);
    uint16_t y1 = mp_obj_get_int(args_in[4]);
	uint16_t x2 = mp_obj_get_int(args_in[5]);
    uint16_t y2 = mp_obj_get_int(args_in[6]);
    uint16_t color = mp_obj_get_int(args_in[7]);

    trian(self, x0, y0, x1, y1, x2, y2, color);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_trian_obj, 8, 8, amoled_AMOLED_trian);

STATIC void fill_trian(amoled_AMOLED_obj_t *self, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color) {

	uint16_t xmin = minx(minx(x0,x1),x2);
	uint16_t xmax = maxx(maxx(x0,x1),x2);
	mp_float_t dx02;
	mp_float_t dx01;
	mp_float_t dx12;
	mp_float_t x01;
	mp_float_t x02;
	mp_float_t x12;
	
	//Sort corners by y value (y0 < y1 < y2)
	if (y1 < y0) {
		_swap_int16_t(x0, x1);
        _swap_int16_t(y0, y1);
	}
	if (y2 < y0) {
		_swap_int16_t(x0, x2);
        _swap_int16_t(y0, y2);
	}	
	if (y2 < y1) {
		_swap_int16_t(x1, x2);
        _swap_int16_t(y1, y2);
	}		
	
	if (y2 == y0) {
		fast_hline(self,xmin,y0,xmax-xmin,color);
		return;
	}

	self->hold_display = true;
	
	dx02 = (float)(x2 - x0) / (float)(y2 - y0);
	x02 = x0;
	x01 = x0;
	/*Check if triangle has flat bottom*/
	if (y1 > y0) {
		dx01 = (float)(x1 - x0) / (float)(y1 - y0);
		for(uint16_t y=y0; y<=y1; y++) {
			if (x01 <= x02) {
				fast_hline(self,(int)x01,y,(int)(x02-x01),color);
			} else {
				fast_hline(self,(int)x02,y,(int)(x01-x02),color);
			}
			x02 += dx02;
			x01 += dx01;
		}
	}
	/*Check if triangle has flat top*/
	if (y2 > y1) {
		dx12 = (float)(x2 - x1) / (float)(y2 - y1);
		x12 = x1 + dx12; //we alreardy proceed up to y1 so 
		for(uint16_t y=y1+1; y<=y2; y++) {
			if (x02 <= x12) {
				fast_hline(self,(int)x02,y,(int)(x12-x02),color);
			} else {
				fast_hline(self,(int)x12,y,(int)(x02-x12),color);
			}
			x02 += dx02;
			x12 += dx12;		
		}
	}
	self->hold_display = false;
	refresh_display(self,xmin,y0,xmax-xmin,y2-y0);
}

STATIC mp_obj_t amoled_AMOLED_fill_trian(size_t n_args, const mp_obj_t *args_in) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args_in[0]);
    uint16_t x0 = mp_obj_get_int(args_in[1]);
    uint16_t y0 = mp_obj_get_int(args_in[2]);
	uint16_t x1 = mp_obj_get_int(args_in[3]);
    uint16_t y1 = mp_obj_get_int(args_in[4]);
	uint16_t x2 = mp_obj_get_int(args_in[5]);
    uint16_t y2 = mp_obj_get_int(args_in[6]);
    uint16_t color = mp_obj_get_int(args_in[7]);

    fill_trian(self, x0, y0, x1, y1, x2, y2, color);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_fill_trian_obj, 8, 8, amoled_AMOLED_fill_trian);


STATIC void fill_bubble_rect(amoled_AMOLED_obj_t *self, uint16_t xs, uint16_t ys, uint16_t w, uint16_t h, uint16_t color) {
    if (xs + w > self->width || ys + h > self->height) {
        return;
    }
    int bubble_size;
    if (w < h) {
        bubble_size = w / 4;
    } else {
        bubble_size = h / 4;
    }
    
    int xm = xs + bubble_size;
    int ym = ys + bubble_size;
    int x = 0;
    int y = bubble_size;
    int p = 1 - bubble_size;
    
	self->hold_display = true;
	
    if ((w < (bubble_size * 2)) | (h < (bubble_size * 2))){
        return;
    } else {
        fill_rect(self, xs, ys + bubble_size - 1, w, h - bubble_size * 2, color);
    }

    while (x <= y) {
        // top left to right
        fast_hline(self, xm - x, ym - y, w - bubble_size * 2 + x * 2 - 1, color);
        fast_hline(self, xm - y, ym - x, w - bubble_size * 2 + y * 2 - 1, color);
        
        // bottom left to right
        fast_hline(self, xm - x, ym + h - bubble_size * 2 + y - 1, w - bubble_size * 2 + x * 2 - 1, color);
        fast_hline(self, xm - y, ym + h - bubble_size * 2 + x - 1, w - bubble_size * 2 + y * 2 - 1, color);
        
        if (p < 0) {
            p += 2 * x + 3;
        } else {
            p += 2 * (x - y) + 5;
            y -= 1;
        } 
        x += 1;
    }
	self->hold_display = false;
	refresh_display(self,xs,ys,w,h);
}

STATIC mp_obj_t amoled_AMOLED_fill_bubble_rect(size_t n_args, const mp_obj_t *args_in) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args_in[0]);
    uint16_t x = mp_obj_get_int(args_in[1]);
    uint16_t y = mp_obj_get_int(args_in[2]);
    uint16_t w = mp_obj_get_int(args_in[3]);
    uint16_t h = mp_obj_get_int(args_in[4]);
    uint16_t color = mp_obj_get_int(args_in[5]);

    fill_bubble_rect(self, x, y, w, h, color);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_fill_bubble_rect_obj, 6, 6, amoled_AMOLED_fill_bubble_rect);


STATIC void bubble_rect(amoled_AMOLED_obj_t *self, uint16_t xs, uint16_t ys, uint16_t w, uint16_t h, uint16_t color) {
    if (xs + w > self->width || ys + h > self->height) {
        return;
    }
    int bubble_size;
    if (w < h) {
        bubble_size = w / 4;
    } else {
        bubble_size = h / 4;
    }
    
    int xm = xs + bubble_size;
    int ym = ys + bubble_size;
    int x = 0;
    int y = bubble_size;
    int p = 1 - bubble_size;
    
	self->hold_display = true;
    if ((w < (bubble_size * 2)) | (h < (bubble_size * 2))){
        return;
    } else {
        fast_hline(self, xs + bubble_size - 1, ys, w - bubble_size * 2, color);
        fast_hline(self, xs + bubble_size - 1, ys + h - 1, w - bubble_size * 2, color);
        fast_vline(self, xs, ys + bubble_size - 1, h - bubble_size * 2, color);
        fast_vline(self, xs + w -1, ys + bubble_size - 1, h - bubble_size * 2, color);
    }

    while (x <= y){
        // top left
        pixel(self, xm - x, ym - y, color);
        pixel(self, xm - y, ym - x, color);
        
        // top right
        pixel(self, xm + w - bubble_size * 2 + x - 1, ym - y, color);
        pixel(self, xm + w - bubble_size * 2 + y - 1, ym - x, color);
        
        // bottom left
        pixel(self, xm - x, ym + h - bubble_size * 2 + y - 1, color);
        pixel(self, xm - y, ym + h - bubble_size * 2 + x - 1, color);
        
        // bottom right
        pixel(self, xm + w - bubble_size * 2 + x - 1, ym + h - bubble_size * 2 + y - 1, color);
        pixel(self, xm + w - bubble_size * 2 + y - 1, ym + h - bubble_size * 2 + x - 1, color);
        
        if (p < 0) {
            p += 2 * x + 3;
        } else {
            p += 2 * (x - y) + 5;
            y -= 1;
        }
        x += 1;
    }
	self->hold_display = false;
	refresh_display(self,xs,ys,w,h);
}

STATIC mp_obj_t amoled_AMOLED_bubble_rect(size_t n_args, const mp_obj_t *args_in) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args_in[0]);
    uint16_t x = mp_obj_get_int(args_in[1]);
    uint16_t y = mp_obj_get_int(args_in[2]);
    uint16_t w = mp_obj_get_int(args_in[3]);
    uint16_t h = mp_obj_get_int(args_in[4]);
    uint16_t color = mp_obj_get_int(args_in[5]);

    bubble_rect(self, x, y, w, h, color);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_bubble_rect_obj, 6, 6, amoled_AMOLED_bubble_rect);


STATIC void circle(amoled_AMOLED_obj_t *self, uint16_t xm, uint16_t ym, uint16_t r, uint16_t color) {
    int x = 0;
    int y = r;
    int p = 1 - r;

	self->hold_display = true;
    while (x <= y) {
        pixel(self, xm + x, ym + y, color);
        pixel(self, xm + x, ym - y, color);
        pixel(self, xm - x, ym + y, color);
        pixel(self, xm - x, ym - y, color);
        pixel(self, xm + y, ym + x, color);
        pixel(self, xm + y, ym - x, color);
        pixel(self, xm - y, ym + x, color);
        pixel(self, xm - y, ym - x, color);

        if (p < 0) {
            p += 2 * x + 3;
        } else {
            p += 2 * (x - y) + 5;
            y -= 1;
        }
        x += 1;
    }
	self->hold_display = false;
	refresh_display(self,xm-r,ym-r,2*r+1,2*r+1);
}

STATIC mp_obj_t amoled_AMOLED_circle(size_t n_args, const mp_obj_t *args_in) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args_in[0]);
    uint16_t xm = mp_obj_get_int(args_in[1]);
    uint16_t ym = mp_obj_get_int(args_in[2]);
    uint16_t r = mp_obj_get_int(args_in[3]);
    uint16_t color = mp_obj_get_int(args_in[4]);

    circle(self, xm, ym, r, color);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_circle_obj, 5, 5, amoled_AMOLED_circle);


STATIC void fill_circle(amoled_AMOLED_obj_t *self, uint16_t xm, uint16_t ym, uint16_t r, uint16_t color) {
    int x = 0;
    int y = r;
    int p = 1 - r;
	
	self->hold_display = true;
    while (x <= y) {
        fast_vline(self, xm + x, ym - y, 2 * y, color);
        fast_vline(self, xm - x, ym - y, 2 * y, color);
        fast_vline(self, xm + y, ym - x, 2 * x, color);
        fast_vline(self, xm - y, ym - x, 2 * x, color);

        if (p < 0) {
            p += 2 * x + 3;
        } else {
            p += 2 * (x - y) + 5;
            y -= 1;
        }
        x += 1;
    }
	self->hold_display = false;
	refresh_display(self,xm-r,ym-r,2*r+1,2*r+1);
}

STATIC mp_obj_t amoled_AMOLED_fill_circle(size_t n_args, const mp_obj_t *args_in) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args_in[0]);
    uint16_t xm = mp_obj_get_int(args_in[1]);
    uint16_t ym = mp_obj_get_int(args_in[2]);
    uint16_t r = mp_obj_get_int(args_in[3]);
    uint16_t color = mp_obj_get_int(args_in[4]);

    fill_circle(self, xm, ym, r, color);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_fill_circle_obj, 5, 5, amoled_AMOLED_fill_circle);


// Return the center of a polygon as an (x, y) tuple
STATIC mp_obj_t amoled_AMOLED_polygon_center(size_t n_args, const mp_obj_t *args) {
    size_t poly_len;
    mp_obj_t *polygon;
    mp_obj_get_array(args[1], &poly_len, &polygon);

    mp_float_t sum = 0.0;
    int vsx = 0;
    int vsy = 0;

    if (poly_len > 0) {
        for (int idx = 0; idx < poly_len; idx++) {
            size_t point_from_poly_len;
            mp_obj_t *point_from_poly;
            mp_obj_get_array(polygon[idx], &point_from_poly_len, &point_from_poly);
            if (point_from_poly_len < 2) {
                mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Polygon data error"));
            }

            mp_int_t v1x = mp_obj_get_int(point_from_poly[0]);
            mp_int_t v1y = mp_obj_get_int(point_from_poly[1]);

            mp_obj_get_array(polygon[(idx + 1) % poly_len], &point_from_poly_len, &point_from_poly);
            if (point_from_poly_len < 2) {
                mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Polygon data error"));
            }

            mp_int_t v2x = mp_obj_get_int(point_from_poly[0]);
            mp_int_t v2y = mp_obj_get_int(point_from_poly[1]);

            mp_float_t cross = v1x * v2y - v1y * v2x;
            sum += cross;
            vsx += (int)((v1x + v2x) * cross);
            vsy += (int)((v1y + v2y) * cross);
        }

        mp_float_t z = 1.0 / (3.0 * sum);
        vsx = (int)(vsx * z);
        vsy = (int)(vsy * z);
    } else {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Polygon data error"));
    }

    mp_obj_t center[2] = {mp_obj_new_int(vsx), mp_obj_new_int(vsy)};
    return mp_obj_new_tuple(2, center);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_polygon_center_obj, 2, 2, amoled_AMOLED_polygon_center);


static void RotatePolygon(Polygon *polygon, Point center, mp_float_t angle) {
    if (polygon->length == 0) {
        return;         /* reject null polygons */

    }
    mp_float_t cosAngle = MICROPY_FLOAT_C_FUN(cos)(angle);
    mp_float_t sinAngle = MICROPY_FLOAT_C_FUN(sin)(angle);

    for (int i = 0; i < polygon->length; i++) {
        mp_float_t dx = (polygon->points[i].x - center.x);
        mp_float_t dy = (polygon->points[i].y - center.y);

        polygon->points[i].x = center.x + (int)0.5 + (dx * cosAngle - dy * sinAngle);
        polygon->points[i].y = center.y + (int)0.5 + (dx * sinAngle + dy * cosAngle);
    }
}

//
// public-domain code by Darel Rex Finley, 2007
// https://alienryderflex.com/polygon_fill/
//

#define MAX_POLY_CORNERS 32
STATIC void PolygonFill(amoled_AMOLED_obj_t *self, Polygon *polygon, Point location, uint16_t color) {
    int nodes, nodeX[MAX_POLY_CORNERS], pixelY, i, j, swap;

    int minX = INT_MAX;
    int maxX = INT_MIN;
    int minY = INT_MAX;
    int maxY = INT_MIN;

    for (i = 0; i < polygon->length; i++) {
        if (polygon->points[i].x < minX) {
            minX = (int)polygon->points[i].x;
        }

        if (polygon->points[i].x > maxX) {
            maxX = (int)polygon->points[i].x;
        }

        if (polygon->points[i].y < minY) {
            minY = (int)polygon->points[i].y;
        }

        if (polygon->points[i].y > maxY) {
            maxY = (int)polygon->points[i].y;
        }
    }

	self->hold_display = true;
    //  Loop through the rows
    for (pixelY = minY; pixelY < maxY; pixelY++) {
        //  Build a list of nodes.
        nodes = 0;
        j = polygon->length - 1;
        for (i = 0; i < polygon->length; i++) {
            if ((polygon->points[i].y < pixelY && polygon->points[j].y >= pixelY) ||
                (polygon->points[j].y < pixelY && polygon->points[i].y >= pixelY)) {
                if (nodes < MAX_POLY_CORNERS) {
                    nodeX[nodes++] = (int)(polygon->points[i].x +
                        (pixelY - polygon->points[i].y) /
                        (polygon->points[j].y - polygon->points[i].y) *
                        (polygon->points[j].x - polygon->points[i].x));
                } else {
                    mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Polygon too complex increase MAX_POLY_CORNERS."));
                }
            }
            j = i;
        }

        //  Sort the nodes, via a simple “Bubble” sort.
        i = 0;
        while (i < nodes - 1) {
            if (nodeX[i] > nodeX[i + 1]) {
                swap = nodeX[i];
                nodeX[i] = nodeX[i + 1];
                nodeX[i + 1] = swap;
                if (i) {
                    i--;
                }
            } else {
                i++;
            }
        }		
        //  Fill the pixels between node pairs.
        for (i = 0; i < nodes; i += 2) {
            if (nodeX[i] >= maxX) {
                break;
            }

            if (nodeX[i + 1] > minX) {
                if (nodeX[i] < minX) {
                    nodeX[i] = minX;
                }

                if (nodeX[i + 1] > maxX) {
                    nodeX[i + 1] = maxX;
                }

                fast_hline(self, (int)location.x + nodeX[i], (int)location.y + pixelY, nodeX[i + 1] - nodeX[i] + 1, color);
            }
        }
    }
	/*Adjust display refresh*/
	minX = minX + (int)location.x;
    maxX = maxX + (int)location.x;
    minY = minY + (int)location.y;
    maxY = maxY + (int)location.y;
	self->hold_display = false;	
	refresh_display(self,minX,minY,maxX - minX,maxY - minY);
}


STATIC mp_obj_t amoled_AMOLED_polygon(size_t n_args, const mp_obj_t *args) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args[0]);

    size_t poly_len;
    mp_obj_t *polygon;
    mp_obj_get_array(args[1], &poly_len, &polygon);
	uint16_t xmax;
	uint16_t xmin;
	uint16_t ymin;
	uint16_t ymax;
	uint16_t x0;
	uint16_t y0;
	uint16_t x1;
	uint16_t y1;
	
    self->work = NULL;

    if (poly_len > 0) {
        mp_int_t x = mp_obj_get_int(args[2]);
        mp_int_t y = mp_obj_get_int(args[3]);
        mp_int_t color = mp_obj_get_int(args[4]);

        mp_float_t angle = 0.0f;
        if (n_args > 5 && mp_obj_is_float(args[5])) {
            angle = mp_obj_float_get(args[5]);
        }

        mp_int_t cx = 0;
        mp_int_t cy = 0;

        if (n_args > 6) {
            cx = mp_obj_get_int(args[6]);
            cy = mp_obj_get_int(args[7]);
        }

        self->work = m_malloc(poly_len * sizeof(Point));
        if (self->work) {
            Point *point = (Point *)self->work;

            for (int idx = 0; idx < poly_len; idx++) {
                size_t point_from_poly_len;
                mp_obj_t *point_from_poly;
                mp_obj_get_array(polygon[idx], &point_from_poly_len, &point_from_poly);
                if (point_from_poly_len < 2) {
                    mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Polygon data error"));
                }

                mp_int_t px = mp_obj_get_int(point_from_poly[0]);
                mp_int_t py = mp_obj_get_int(point_from_poly[1]);
                point[idx].x = px;
                point[idx].y = py;
            }

            Point center;
            center.x = cx;
            center.y = cy;

            Polygon polygon;
            polygon.length = poly_len;
            polygon.points = self->work;

            if (angle > 0) {
                RotatePolygon(&polygon, center, angle);
            }

			xmax = (int)point[0].x + x;
			xmin = xmax;
			ymax = (int)point[0].y + y;
			ymin = ymax;
			
			self->hold_display = true;

            for (int idx = 1; idx < poly_len; idx++) {
				x0 = (int)point[idx - 1].x + x;
				y0 = (int)point[idx - 1].y + y;
				x1 = (int)point[idx].x + x;
				y1 = (int)point[idx].y + y;
				
				xmax = (x0>xmax) ? x0 : xmax;
				xmin = (x0<xmin) ? x0 : xmin;
				ymax = (y0>ymax) ? y0 : ymax;
				ymin = (y0<ymin) ? y0 : ymin;
				
                line(self,x0,y0,x1,y1, color);
            }
			
			self->hold_display = false;
			refresh_display(self,xmin,ymin,xmax-xmin,ymax-ymin);
			
            m_free(self->work);
            self->work = NULL;
        } else {
            mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Polygon data error"));
        }
    } else {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Polygon data error"));
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_polygon_obj, 5, 8, amoled_AMOLED_polygon);


STATIC mp_obj_t amoled_AMOLED_fill_polygon(size_t n_args, const mp_obj_t *args) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args[0]);

    size_t poly_len;
    mp_obj_t *polygon;
    mp_obj_get_array(args[1], &poly_len, &polygon);

    self->work = NULL;

    if (poly_len > 0) {
        mp_int_t x = mp_obj_get_int(args[2]);
        mp_int_t y = mp_obj_get_int(args[3]);
        mp_int_t color = mp_obj_get_int(args[4]);

        mp_float_t angle = 0.0f;
        if (n_args > 5) {
            angle = mp_obj_float_get(args[5]);
        }

        mp_int_t cx = 0;
        mp_int_t cy = 0;

        if (n_args > 6) {
            cx = mp_obj_get_int(args[6]);
            cy = mp_obj_get_int(args[7]);
        }

        self->work = m_malloc(poly_len * sizeof(Point));
        if (self->work) {
            Point *point = (Point *)self->work;

            for (int idx = 0; idx < poly_len; idx++) {
                size_t point_from_poly_len;
                mp_obj_t *point_from_poly;
                mp_obj_get_array(polygon[idx], &point_from_poly_len, &point_from_poly);
                if (point_from_poly_len < 2) {
                    mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Polygon data error"));
                }

                point[idx].x = mp_obj_get_int(point_from_poly[0]);
                point[idx].y = mp_obj_get_int(point_from_poly[1]);
            }

            Point center = {cx, cy};
            Polygon polygon = {poly_len, self->work};

            if (angle != 0) {
                RotatePolygon(&polygon, center, angle);
            }

            Point location = {x, y};
            PolygonFill(self, &polygon, location, color);

            m_free(self->work);
            self->work = NULL;
        } else {
            mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Polygon data error"));
        }

    } else {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Polygon data error"));
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_fill_polygon_obj, 5, 8, amoled_AMOLED_fill_polygon);


STATIC mp_obj_t amoled_AMOLED_bitmap(size_t n_args, const mp_obj_t *args_in) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args_in[0]);

    int x_start = mp_obj_get_int(args_in[1]);
    int y_start = mp_obj_get_int(args_in[2]);
    int x_end   = mp_obj_get_int(args_in[3]);
    int y_end   = mp_obj_get_int(args_in[4]);

    x_start += self->x_gap;
    x_end += self->x_gap;
    y_start += self->y_gap;
    y_end += self->y_gap;

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args_in[5], &bufinfo, MP_BUFFER_READ);
    set_area(self, x_start, y_start, x_end, y_end);
    size_t len = ((x_end - x_start) * (y_end - y_start) * self->fb_bpp / 8);
    write_color(self, bufinfo.buf, len);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_bitmap_obj, 6, 6, amoled_AMOLED_bitmap);


STATIC mp_obj_t amoled_AMOLED_text(size_t n_args, const mp_obj_t *args) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    uint8_t single_char_s;
    const uint8_t *source = NULL;
    size_t source_len = 0;
	size_t buf_idx;

    // extract arguments
    mp_obj_module_t *font = MP_OBJ_TO_PTR(args[1]);  		// Arg n°1 is the font pointer (font)

    if (mp_obj_is_int(args[2])) {
        mp_int_t c = mp_obj_get_int(args[2]);    			// Arg n°2 is wether a 1 byte single caracter  (c)
        single_char_s = (c & 0xff);
        source = &single_char_s;
        source_len = 1;
    } else if (mp_obj_is_str(args[2])) { 					// or a sting
        source = (uint8_t *) mp_obj_str_get_str(args[2]);
        source_len = strlen((char *)source);
    } else if (mp_obj_is_type(args[2], &mp_type_bytes)) {	// or a byte_array
        mp_obj_t text_data_buff = args[2];
        mp_buffer_info_t text_bufinfo;
        mp_get_buffer_raise(text_data_buff, &text_bufinfo, MP_BUFFER_READ);	// text_bufinfo is activated text_data_buff receives data 
        source = text_bufinfo.buf;							// in every case the string is named source 
        source_len = text_bufinfo.len;						// its length is source_len
    } else {
        mp_raise_TypeError(MP_ERROR_TEXT("text requires either int, str or bytes."));
        return mp_const_none;
    }

    mp_int_t x = mp_obj_get_int(args[3]);					// Arg n°3 is x_position x
	mp_int_t x0 = x;
    mp_int_t y = mp_obj_get_int(args[4]);					// Arg n°4 is y_position y
	
    mp_obj_dict_t *dict = MP_OBJ_TO_PTR(font->globals);		// dict points to Font object (font)
    const uint8_t width = mp_obj_get_int(mp_obj_dict_get(dict, MP_OBJ_NEW_QSTR(MP_QSTR_WIDTH)));	 	// witdh is the font width
    const uint8_t height = mp_obj_get_int(mp_obj_dict_get(dict, MP_OBJ_NEW_QSTR(MP_QSTR_HEIGHT)));		// height...
    const uint8_t first = mp_obj_get_int(mp_obj_dict_get(dict, MP_OBJ_NEW_QSTR(MP_QSTR_FIRST)));		// first character
    const uint8_t last = mp_obj_get_int(mp_obj_dict_get(dict, MP_OBJ_NEW_QSTR(MP_QSTR_LAST)));			// last char.

    mp_obj_t font_data_buff = mp_obj_dict_get(dict, MP_OBJ_NEW_QSTR(MP_QSTR_FONT));						// font_data_buff is the font buff
    mp_buffer_info_t bufinfo;																			// bufinfo is the buffer interrupt
    mp_get_buffer_raise(font_data_buff, &bufinfo, MP_BUFFER_READ);										// 
    const uint8_t *font_data = bufinfo.buf;																// font_data is bufinfo.buf data pointer  

    uint16_t fg_color;
    uint16_t bg_color;

    if (n_args > 5) {	// Arg n°5 is the front color, WHITE by default
        fg_color = mp_obj_get_int(args[5]);
    } else {
        fg_color = WHITE;
    }

    if (n_args > 6) {	// Arg n°5 is the backgroun color
        bg_color = mp_obj_get_int(args[6]);
    } else {
        bg_color = BLACK;
    }

    uint8_t wide = width / 8; // wide = width in Bytes for a single char (ex 16bit large font is 2 bytes per line)
    uint8_t chr;
	
    while (source_len--) {	// for the full source (in bytes)
        chr = *source++;	// for every characteres in string as char
        if (chr >= first && chr <= last) {	// if string character is in the font character range 
			if (x + width > self->max_width_value) {
				return mp_const_none;  // return if char is away from dsplay
			}
            uint16_t chr_idx = (chr - first) * (height * wide);	// chr_index is the charactere index in the font file 
			for (uint8_t line = 0; line < height; line++) {		// for every line of the font character
				buf_idx = (y + line) * self->width + x;	// buf_idx is the frame buffer start index for each line
				for (uint8_t line_byte = 0; line_byte < wide; line_byte++) { 	//for wide bytes of every line 
                    uint8_t chr_data = font_data[chr_idx];					 	// get corresponding data
                    for (uint8_t bit = 8; bit; bit--) {						 	// for every bits of the font
						if (chr_data >> (bit - 1) & 1) {	// 1 = Front color / 0 = back_color
                            self->frame_buffer[buf_idx] = fg_color;	
                        } else {
                            self->frame_buffer[buf_idx] = bg_color;
                        }
                        buf_idx++;	// next frame buffer index and proceed next font bit
                    }
                    chr_idx++;													// next font line_byte
                }																			
            }																// next line
            x += width;				// next chart ==> x0 moves to next place
        }	// if not in font character range = Do nothing
    } // all source character proceeded
	refresh_display(self,x0,y,x - x0,height);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_text_obj, 5, 7, amoled_AMOLED_text);

STATIC uint32_t bs_bit = 0;
uint8_t *bitmap_data = NULL;

STATIC uint8_t get_color(uint8_t bpp) {
    uint8_t color = 0;
    int i;

    for (i = 0; i < bpp; i++) {
        color <<= 1;
        color |= (bitmap_data[bs_bit / 8] & 1 << (7 - (bs_bit % 8))) > 0;
        bs_bit++;
    }
    return color;
}

STATIC mp_obj_t amoled_AMOLED_text_len(size_t n_args, const mp_obj_t *args) {
    //amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    uint8_t single_char_s;
    const uint8_t *source = NULL;
    size_t source_len = 0;

    // extract arguments
    mp_obj_module_t *font = MP_OBJ_TO_PTR(args[1]);  		// Arg n°1 is the font pointer (font)

    if (mp_obj_is_int(args[2])) {
        mp_int_t c = mp_obj_get_int(args[2]);    			// Arg n°2 is wether a 1 byte single caracter  (c)
        single_char_s = (c & 0xff);
        source = &single_char_s;
        source_len = 1;
    } else if (mp_obj_is_str(args[2])) { 					// or a sting
        source = (uint8_t *) mp_obj_str_get_str(args[2]);
        source_len = strlen((char *)source);
    } else if (mp_obj_is_type(args[2], &mp_type_bytes)) {	// or a byte_array
        mp_obj_t text_data_buff = args[2];
        mp_buffer_info_t text_bufinfo;
        mp_get_buffer_raise(text_data_buff, &text_bufinfo, MP_BUFFER_READ);	// text_bufinfo is activated text_data_buff receives data 
        source = text_bufinfo.buf;							// in every case the string is named source 
        source_len = text_bufinfo.len;						// its length is source_len
    } else {
        mp_raise_TypeError(MP_ERROR_TEXT("text requires either int, str or bytes."));
        return mp_const_none;
    }
	
    mp_obj_dict_t *dict = MP_OBJ_TO_PTR(font->globals);		// dict points to Font object (font)
    const uint8_t width = mp_obj_get_int(mp_obj_dict_get(dict, MP_OBJ_NEW_QSTR(MP_QSTR_WIDTH)));	 	// witdh is the font width
	
    uint16_t print_width = source_len * width;
	
    return mp_obj_new_int(print_width);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_text_len_obj, 3, 3, amoled_AMOLED_text_len);


STATIC mp_obj_t amoled_AMOLED_write_len(size_t n_args, const mp_obj_t *args) {
    mp_obj_module_t *font = MP_OBJ_TO_PTR(args[1]);
    mp_obj_dict_t *dict = MP_OBJ_TO_PTR(font->globals);
    mp_obj_t widths_data_buff = mp_obj_dict_get(dict, MP_OBJ_NEW_QSTR(MP_QSTR_WIDTHS));
    mp_buffer_info_t widths_bufinfo;
    mp_get_buffer_raise(widths_data_buff, &widths_bufinfo, MP_BUFFER_READ);
    const uint8_t *widths_data = widths_bufinfo.buf;

    uint16_t print_width = 0;

    mp_obj_t map_obj = mp_obj_dict_get(dict, MP_OBJ_NEW_QSTR(MP_QSTR_MAP));
    GET_STR_DATA_LEN(map_obj, map_data, map_len);
    GET_STR_DATA_LEN(args[2], str_data, str_len);
    const byte *s = str_data, *top = str_data + str_len;

    while (s < top) {
        unichar ch;
        ch = utf8_get_char(s);
        s = utf8_next_char(s);

        const byte *map_s = map_data, *map_top = map_data + map_len;
        uint16_t char_index = 0;

        while (map_s < map_top) {
            unichar map_ch;
            map_ch = utf8_get_char(map_s);
            map_s = utf8_next_char(map_s);

            if (ch == map_ch) {
                print_width += widths_data[char_index];
                break;
            }
            char_index++;
        }
    }

    return mp_obj_new_int(print_width);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_write_len_obj, 3, 3, amoled_AMOLED_write_len);

//
//	write(font_module, s, x, y[, fg, bg, background_tuple, fill])
//		background_tuple (bitmap_buffer, width, height)
//

STATIC mp_obj_t amoled_AMOLED_write(size_t n_args, const mp_obj_t *args) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    mp_obj_module_t *font = MP_OBJ_TO_PTR(args[1]);
	size_t buf_idx;

    mp_int_t x = mp_obj_get_int(args[3]);
	mp_int_t x0 = x;
    mp_int_t y = mp_obj_get_int(args[4]);
	
    mp_int_t fg_color;
    mp_int_t bg_color;

    fg_color = (n_args > 5) ? mp_obj_get_int(args[5]) : WHITE; // Arg 5 if front Color
    bg_color = (n_args > 6) ? mp_obj_get_int(args[6]) : BLACK; // Aarg 6 is back color

    mp_obj_t *tuple_data = NULL;
    size_t tuple_len = 0;

    mp_buffer_info_t background_bufinfo;
    uint16_t background_width = 0;
    uint16_t background_height = 0;
    uint16_t *background_data = NULL;

    if (n_args > 7) {		//Arg 7 is backgroung tupple Data
        mp_obj_tuple_get(args[7], &tuple_len, &tuple_data);
        if (tuple_len > 2) {
            mp_get_buffer_raise(tuple_data[0], &background_bufinfo, MP_BUFFER_READ);
            background_data = background_bufinfo.buf;
            background_width = mp_obj_get_int(tuple_data[1]);
            background_height = mp_obj_get_int(tuple_data[2]);
        }
    }

    bool fill = (n_args > 8) ? mp_obj_is_true(args[8]) : false;  //Arg8 is Fill bool for buffer background

    mp_obj_dict_t *dict = MP_OBJ_TO_PTR(font->globals);
    const uint8_t bpp = mp_obj_get_int(mp_obj_dict_get(dict, MP_OBJ_NEW_QSTR(MP_QSTR_BPP)));
    const uint8_t height = mp_obj_get_int(mp_obj_dict_get(dict, MP_OBJ_NEW_QSTR(MP_QSTR_HEIGHT)));  // height is the font height
    const uint8_t offset_width = mp_obj_get_int(mp_obj_dict_get(dict, MP_OBJ_NEW_QSTR(MP_QSTR_OFFSET_WIDTH)));
    //const uint8_t max_width = mp_obj_get_int(mp_obj_dict_get(dict, MP_OBJ_NEW_QSTR(MP_QSTR_MAX_WIDTH)));

    mp_obj_t widths_data_buff = mp_obj_dict_get(dict, MP_OBJ_NEW_QSTR(MP_QSTR_WIDTHS));  
    mp_buffer_info_t widths_bufinfo;
    mp_get_buffer_raise(widths_data_buff, &widths_bufinfo, MP_BUFFER_READ);
    const uint8_t *widths_data = widths_bufinfo.buf;	// widths_data is char by char width 

    mp_obj_t offsets_data_buff = mp_obj_dict_get(dict, MP_OBJ_NEW_QSTR(MP_QSTR_OFFSETS));
    mp_buffer_info_t offsets_bufinfo;
    mp_get_buffer_raise(offsets_data_buff, &offsets_bufinfo, MP_BUFFER_READ);
    const uint8_t *offsets_data = offsets_bufinfo.buf;  // offsets_data is char offset in data in order to reach each char data

    mp_obj_t bitmaps_data_buff = mp_obj_dict_get(dict, MP_OBJ_NEW_QSTR(MP_QSTR_BITMAPS));
    mp_buffer_info_t bitmaps_bufinfo;
    mp_get_buffer_raise(bitmaps_data_buff, &bitmaps_bufinfo, MP_BUFFER_READ);
    bitmap_data = bitmaps_bufinfo.buf; //bitmap_data is background data

    // if fill is set, and background bitmap data is available copy the background
    // bitmap data into the buffer. The background buffer must be the size of the
    // widest character in the font.
    if (fill && background_data) {
        //memcpy(self->frame_buffer, background_data, background_width * background_height * 2);
    }

    mp_obj_t map_obj = mp_obj_dict_get(dict, MP_OBJ_NEW_QSTR(MP_QSTR_MAP));
    GET_STR_DATA_LEN(map_obj, map_data, map_len);
    GET_STR_DATA_LEN(args[2], str_data, str_len);
    const byte *s = str_data, *top = str_data + str_len;  //s point the string data and top is the last data
	
    while (s < top) {
        unichar ch;
        ch = utf8_get_char(s); // ch = current char
        s = utf8_next_char(s); // s = next

        const byte *map_s = map_data, *map_top = map_data + map_len;
        uint16_t char_index = 0;

        while (map_s < map_top) {
            unichar map_ch;
            map_ch = utf8_get_char(map_s);
            map_s = utf8_next_char(map_s);

			buf_idx = 0;  // Init buffer index
				
            if (ch == map_ch) {
                uint8_t width = widths_data[char_index];    //width is the character width
				if (x + width > self->max_width_value) {
					return mp_const_none;  // return if char is away from dsplay
				}
                bs_bit = 0;
                switch (offset_width) {
                    case 1:
                        bs_bit = offsets_data[char_index * offset_width];
                        break;

                    case 2:
                        bs_bit = (offsets_data[char_index * offset_width] << 8) +
                            (offsets_data[char_index * offset_width + 1]);
                        break;

                    case 3:
                        bs_bit = (offsets_data[char_index * offset_width] << 16) +
                            (offsets_data[char_index * offset_width + 1] << 8) +
                            (offsets_data[char_index * offset_width + 2]);
                        break;
                }

                uint16_t color = 0;
							
                for (uint16_t line = 0; line < height; line++) {  // for every line of char	
					buf_idx = (y + line) * self->width + x;	// buf_idx is the frame buffer start index for each line
                    for (uint16_t line_bits = 0; line_bits < width; line_bits++) { //for every bit of every line
                        if (background_data && (line_bits <= background_width && line <= background_height)) {
                            if (get_color(bpp) == bg_color) {
                                color = background_data[(line * background_width + line_bits)];
                            } else {
                                color = fg_color;
                            }
                        } else {
                            color = get_color(bpp) ? fg_color : bg_color;  //color = front_color else back_color
                        }
                        self->frame_buffer[buf_idx] = color;
						buf_idx++;
                    }
				}			
                x += width;
                break;
            }
            char_index++;
        }
    }
    refresh_display(self,x0,y,x - x0,height);
	return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_write_obj, 5, 9, amoled_AMOLED_write);

static mp_obj_t amoled_AMOLED_draw(size_t n_args, const mp_obj_t *args) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    char single_char_s[] = {0, 0};
    const char *s;

    mp_obj_module_t *hershey = MP_OBJ_TO_PTR(args[1]);

    if (mp_obj_is_int(args[2])) {
        mp_int_t c = mp_obj_get_int(args[2]);
        single_char_s[0] = c & 0xff;
        s = single_char_s;
    } else {
        s = mp_obj_str_get_str(args[2]);
    }

    mp_int_t x = mp_obj_get_int(args[3]);
    mp_int_t y = mp_obj_get_int(args[4]);

    mp_int_t color = (n_args > 5) ? mp_obj_get_int(args[5]) : WHITE;

    mp_float_t scale = 1.0;
    if (n_args > 6) {
        if (mp_obj_is_float(args[6])) {
            scale = mp_obj_float_get(args[6]);
        }
        if (mp_obj_is_int(args[6])) {
            scale = (mp_float_t)mp_obj_get_int(args[6]);
        }
    }

    mp_obj_dict_t *dict = MP_OBJ_TO_PTR(hershey->globals);
    mp_obj_t *index_data_buff = mp_obj_dict_get(dict, MP_OBJ_NEW_QSTR(MP_QSTR_INDEX));
    mp_buffer_info_t index_bufinfo;
    mp_get_buffer_raise(index_data_buff, &index_bufinfo, MP_BUFFER_READ);
    uint8_t *index = index_bufinfo.buf;

    mp_obj_t *font_data_buff = mp_obj_dict_get(dict, MP_OBJ_NEW_QSTR(MP_QSTR_FONT));
    mp_buffer_info_t font_bufinfo;
    mp_get_buffer_raise(font_data_buff, &font_bufinfo, MP_BUFFER_READ);
    int8_t *font = font_bufinfo.buf;

    int16_t from_x = x;
    int16_t from_y = y;
    int16_t to_x = x;
    int16_t to_y = y;
    int16_t pos_x = x;
    int16_t pos_y = y;
    bool penup = true;
    char c;
    int16_t ii;

    while ((c = *s++)) {
        if (c >= 32 && c <= 127) {
            ii = (c - 32) * 2;

            int16_t offset = index[ii] | (index[ii + 1] << 8);
            int16_t length = font[offset++];
            int16_t left = (int)(scale * (font[offset++] - 0x52) + 0.5);
            int16_t right = (int)(scale * (font[offset++] - 0x52) + 0.5);
            int16_t width = right - left;

            if (length) {
                int16_t i;
                for (i = 0; i < length; i++) {
                    if (font[offset] == ' ') {
                        offset += 2;
                        penup = true;
                        continue;
                    }

                    int16_t vector_x = (int)(scale * (font[offset++] - 0x52) + 0.5);
                    int16_t vector_y = (int)(scale * (font[offset++] - 0x52) + 0.5);

                    if (!i || penup) {
                        from_x = pos_x + vector_x - left;
                        from_y = pos_y + vector_y;
                    } else {
                        to_x = pos_x + vector_x - left;
                        to_y = pos_y + vector_y;

                        line(self, from_x, from_y, to_x, to_y, color);
                        from_x = to_x;
                        from_y = to_y;
                    }
                    penup = false;
                }
            }
            pos_x += width;
        }
    }

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_draw_obj, 5, 7, amoled_AMOLED_draw);

static mp_obj_t amoled_AMOLED_draw_len(size_t n_args, const mp_obj_t *args) {
    char single_char_s[] = {0, 0};
    const char *s;

    mp_obj_module_t *hershey = MP_OBJ_TO_PTR(args[1]);

    if (mp_obj_is_int(args[2])) {
        mp_int_t c = mp_obj_get_int(args[2]);
        single_char_s[0] = c & 0xff;
        s = single_char_s;
    } else {
        s = mp_obj_str_get_str(args[2]);
    }

    mp_float_t scale = 1.0;
    if (n_args > 3) {
        if (mp_obj_is_float(args[3])) {
            scale = mp_obj_float_get(args[3]);
        }
        if (mp_obj_is_int(args[3])) {
            scale = (mp_float_t)mp_obj_get_int(args[3]);
        }
    }

    mp_obj_dict_t *dict = MP_OBJ_TO_PTR(hershey->globals);
    mp_obj_t *index_data_buff = mp_obj_dict_get(dict, MP_OBJ_NEW_QSTR(MP_QSTR_INDEX));
    mp_buffer_info_t index_bufinfo;
    mp_get_buffer_raise(index_data_buff, &index_bufinfo, MP_BUFFER_READ);
    uint8_t *index = index_bufinfo.buf;

    mp_obj_t *font_data_buff = mp_obj_dict_get(dict, MP_OBJ_NEW_QSTR(MP_QSTR_FONT));
    mp_buffer_info_t font_bufinfo;
    mp_get_buffer_raise(font_data_buff, &font_bufinfo, MP_BUFFER_READ);
    int8_t *font = font_bufinfo.buf;

    int16_t print_width = 0;
    char c;
    int16_t ii;

    while ((c = *s++)) {
        if (c >= 32 && c <= 127) {
            ii = (c - 32) * 2;

            int16_t offset = (index[ii] | (index[ii + 1] << 8)) + 1;
            int16_t left =  font[offset++] - 0x52;
            int16_t right = font[offset++] - 0x52;
            int16_t width = right - left;
            print_width += width;
        }
    }

    return mp_obj_new_int((int)(print_width * scale + 0.5));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_draw_len_obj, 3, 4, amoled_AMOLED_draw_len);

/*----------------------------------------------------------------------------------------------------
Below are JPG related functions.
-----------------------------------------------------------------------------------------------------*/

// User defined device identifier
typedef struct {
    mp_file_t *fp;              // File pointer for input function
    uint8_t *fbuf;              // Pointer to the frame buffer for output function
    unsigned int wfbuf;         // Width of the frame buffer [pix]
    unsigned int left;          // jpg crop left column
    unsigned int top;           // jpg crop top row
    unsigned int right;         // jpg crop right column
    unsigned int bottom;        // jpg crop bottom row
    amoled_AMOLED_obj_t *self;  // display object
    // for buffer input function
    uint8_t *data;
    unsigned int dataIdx;
    unsigned int dataLen;
} IODEV;

// file input function
// Returns number of bytes read (zero on error)
// jd = Decompression object
// buff = Pointer to read buffer
// nbytes = Number of bytes to read/remove

static unsigned int in_func(JDEC *jd, uint8_t *buff, unsigned int nbyte) {               
    IODEV *dev = (IODEV *)jd->device;   // Device identifier for the session (5th argument of jd_prepare function)
    unsigned int nread;

    // Read data from input stream
    if (buff) {
        nread = (unsigned int)mp_readinto(dev->fp, buff, nbyte);
        return nread;
    }

    // Remove data from input stream if buff was NULL
    mp_seek(dev->fp, nbyte, SEEK_CUR);
    return 0;
}

// fast output function
// returns 1:Ok, 0:Aborted
// jd = Decompression object
// bitmap = Bitmap data to be output
// rect = Rectangular region of output image

static int out_fast(JDEC *jd,void *bitmap, JRECT *rect) {
    IODEV *dev = (IODEV *)jd->device;
    uint8_t *src, *dst;
    uint16_t y, bws, bwd;

    // Copy the decompressed RGB rectangular to the frame buffer (assuming RGB565)
    src = (uint8_t *)bitmap;
    dst = dev->fbuf + 2 * (rect->top * dev->wfbuf + rect->left);    // Left-top of destination rectangular assuming 16bpp = 2 bytes
    bws = 2 * (rect->right - rect->left + 1);                       // Width of source rectangular [byte]
    bwd = 2 * dev->wfbuf;                                           // Width of frame buffer [byte]
    for (y = rect->top; y <= rect->bottom; y++) {
        memcpy(dst, src, bws);                                      // Copy a line
        src += bws;
        dst += bwd;                                                 // Next line
    }
    return 1;     // Continue to decompress
}


//
// Draw jpg from a file at x, y
//

static mp_obj_t amoled_AMOLED_jpg(size_t n_args, const mp_obj_t *args) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args[0]);
	const char *filename = mp_obj_str_get_str(args[1]);
	mp_int_t x = mp_obj_get_int(args[2]);
	mp_int_t y = mp_obj_get_int(args[3]);

    int (*outfunc)(JDEC *, void *, JRECT *);

    JRESULT res;	// Result code of TJpgDec API
    JDEC jdec;		// Decompression object
    self->work = (void *)m_malloc(3100);	// Pointer to the work area
	IODEV  devid;	// User defined device identifier
    size_t bufsize;
	
	self->fp = mp_open(filename, "rb");
	devid.fp = self->fp;
	
	if (devid.fp) {
		// Prepare to decompress
		res = jd_prepare(&jdec, in_func, self->work, 3100, &devid);
		if (res == JDR_OK) {
			// Initialize output device
			bufsize = 2 * jdec.width * jdec.height;
			outfunc = out_fast;
			
			if (self->buffer_size && (bufsize > self->buffer_size)) {
				mp_raise_msg_varg(&mp_type_OSError, MP_ERROR_TEXT("buffer too small. %ld bytes required."), (long) bufsize);
			}

			if (self->buffer_size == 0) {
				self->pixel_buffer = m_malloc(bufsize);
			}

			if (!self->pixel_buffer)
				mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("out of memory"));

			devid.fbuf	= (uint8_t *) self->pixel_buffer;
			//devid.fbuf	= (uint16_t *) self->pixel_buffer;
			devid.wfbuf = jdec.width;
			devid.self	= self;
			res			= jd_decomp(&jdec, outfunc, 0); // Start to decompress with 1/1 scaling
			
			if (res == JDR_OK) {
				//set_window(self, x, y, x + jdec.width - 1, y + jdec.height - 1);
				//write_bus(self, (uint8_t *) self->pixel_buffer, bufsize);
				
				size_t jpg_idx=0;
				size_t buf_idx=0;
				uint16_t color;
				
				for(uint16_t line=0; line < jdec.height; line++) {
					buf_idx = (y + line)*self->width + x;
					for(uint16_t col=0; col < jdec.width; col++) {
						color = devid.fbuf[jpg_idx+1] << 8 | devid.fbuf[jpg_idx];
						self->frame_buffer[buf_idx] = color;
						buf_idx++;
						jpg_idx += 2;
					}
				}
			} else {
				mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("jpg decompress failed."));
			}
			if (self->buffer_size == 0) {
				m_free(self->pixel_buffer); // Discard frame buffer
				self->pixel_buffer = MP_OBJ_NULL;
			}
			devid.fbuf = MP_OBJ_NULL;
		} else {
			mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("jpg prepare failed."));
		}
		mp_close(devid.fp);
	}
	m_free(self->work); // Discard work area
	//Refresh display
	refresh_display(self,0,0,self->width,self->height);
	//refresh_display(self,x,y,jdec.width,jdec.height);
	return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_jpg_obj, 4, 4, amoled_AMOLED_jpg);


// output function for jpg_decode
// Retuns 1:Ok, 0:Aborted
// jd = Decompression object
// bitmap = Bitmap data to be output
// rect = Rectangular region of output image

static int out_crop(JDEC *jd, void *bitmap, JRECT *rect) {                      
    IODEV *dev = (IODEV *)jd->device;

    if (dev->left <= rect->right &&
        dev->right >= rect->left &&
        dev->top <= rect->bottom &&
        dev->bottom >= rect->top) {
			uint16_t left = MAX(dev->left, rect->left);
			uint16_t top = MAX(dev->top, rect->top);
			uint16_t right = MIN(dev->right, rect->right);
			uint16_t bottom = MIN(dev->bottom, rect->bottom);
			uint16_t dev_width = dev->right - dev->left + 1;
			uint16_t rect_width = rect->right - rect->left + 1;
			uint16_t width = (right - left + 1) * 2;
			uint16_t row;

			for (row = top; row <= bottom; row++) {
				memcpy(
					(uint16_t *)dev->fbuf + ((row - dev->top) * dev_width) + left - dev->left,
					(uint16_t *)bitmap + ((row - rect->top) * rect_width) + left - rect->left,
					width);
			}
	}
    return 1;     // Continue to decompress
}

//
// Decode a jpg file and return it or a portion of it as a tuple containing
// a blittable buffer, the width and height of the buffer.
//

static mp_obj_t amoled_AMOLED_jpg_decode(size_t n_args, const mp_obj_t *args) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args[0]);
	const char	*filename;
	mp_int_t	x = 0, y = 0, width = 0, height = 0;

	if (n_args == 2 || n_args == 6) {
		filename = mp_obj_str_get_str(args[1]);
		if (n_args == 6) {
			x	   = mp_obj_get_int(args[2]);
			y	   = mp_obj_get_int(args[3]);
			width  = mp_obj_get_int(args[4]);
			height = mp_obj_get_int(args[5]);
		}
		self->work = (void *) m_malloc(3100); // Pointer to the work area

		JRESULT res;   // Result code of TJpgDec API
		JDEC	jdec;  // Decompression object
		IODEV	devid; // User defined device identifier
		size_t	bufsize = 0;

		self->fp = mp_open(filename, "rb");
		devid.fp = self->fp;
		if (devid.fp) {
			// Prepare to decompress
			res = jd_prepare(&jdec, in_func, self->work, 3100, &devid);
			if (res == JDR_OK) {
				if (n_args < 6) {
					x	   = 0;
					y	   = 0;
					width  = jdec.width;
					height = jdec.height;
				}
				// Initialize output device
				devid.left	 = x;
				devid.top	 = y;
				devid.right	 = x + width - 1;
				devid.bottom = y + height - 1;

				bufsize			   = 2 * width * height;
				self->pixel_buffer = m_malloc(bufsize);
				if (self->pixel_buffer) {
					memset(self->pixel_buffer, 0xBEEF, bufsize);
				} else {
					mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("out of memory"));
				}

				devid.fbuf	= (uint8_t *) self->pixel_buffer;
				devid.wfbuf = jdec.width;
				devid.self	= self;
				res			= jd_decomp(&jdec, out_crop, 0); // Start to decompress with 1/1 scaling
				if (res != JDR_OK) {
					mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("jpg decompress failed."));
				}

			} else {
				mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("jpg prepare failed."));
			}
			mp_close(devid.fp);
		}
		m_free(self->work); // Discard work area

		mp_obj_t result[3] = {
			mp_obj_new_bytearray(bufsize, (mp_obj_t *) self->pixel_buffer),
			mp_obj_new_int(width),
			mp_obj_new_int(height)};

		return mp_obj_new_tuple(3, result);
	}

	mp_raise_TypeError(MP_ERROR_TEXT("jpg_decode requires either 2 or 6 arguments"));
	return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_jpg_decode_obj, 2, 6, amoled_AMOLED_jpg_decode);


/*---------------------------------------------------------------------------------------------------
Below are screencontroler related functions
----------------------------------------------------------------------------------------------------*/


STATIC mp_obj_t amoled_AMOLED_mirror(mp_obj_t self_in,
                                      mp_obj_t mirror_x_in,
                                      mp_obj_t mirror_y_in)
{
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(self_in);

    if (mp_obj_is_true(mirror_x_in)) {
        self->madctl_val |= (1 << 6);
    } else {
        self->madctl_val &= ~(1 << 6);
    }
    if (mp_obj_is_true(mirror_y_in)) {
        self->madctl_val |= (1 << 7);
    } else {
        self->madctl_val &= ~(1 << 7);
    }
    write_spi(self, LCD_CMD_MADCTL, (uint8_t[]) { self->madctl_val }, 1);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(amoled_AMOLED_mirror_obj, amoled_AMOLED_mirror);


STATIC mp_obj_t amoled_AMOLED_swap_xy(mp_obj_t self_in, mp_obj_t swap_axes_in)
{
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (mp_obj_is_true(swap_axes_in)) {
        self->madctl_val |= 1 << 5;
    } else {
        self->madctl_val &= ~(1 << 5);
    }
    write_spi(self, LCD_CMD_MADCTL, (uint8_t[]) { self->madctl_val }, 1);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(amoled_AMOLED_swap_xy_obj, amoled_AMOLED_swap_xy);


STATIC mp_obj_t amoled_AMOLED_set_gap(mp_obj_t self_in,
                                       mp_obj_t x_gap_in,
                                       mp_obj_t y_gap_in)
{
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(self_in);
    self->x_gap = mp_obj_get_int(x_gap_in);
    self->y_gap = mp_obj_get_int(y_gap_in);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(amoled_AMOLED_set_gap_obj, amoled_AMOLED_set_gap);


STATIC mp_obj_t amoled_AMOLED_invert_color(mp_obj_t self_in, mp_obj_t invert_in)
{
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (mp_obj_is_true(invert_in)) {
        write_spi(self, LCD_CMD_INVON, NULL, 0);
    } else {
        write_spi(self, LCD_CMD_INVOFF, NULL, 0);
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(amoled_AMOLED_invert_color_obj, amoled_AMOLED_invert_color);


STATIC mp_obj_t amoled_AMOLED_disp_off(mp_obj_t self_in)
{
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(self_in);
    write_spi(self, LCD_CMD_SLPIN, NULL, 0);
    write_spi(self, LCD_CMD_DISPOFF, NULL, 0);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(amoled_AMOLED_disp_off_obj, amoled_AMOLED_disp_off);


STATIC mp_obj_t amoled_AMOLED_disp_on(mp_obj_t self_in)
{
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(self_in);
    write_spi(self, LCD_CMD_SLPOUT, NULL, 0);
    write_spi(self, LCD_CMD_DISPON, NULL, 0);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(amoled_AMOLED_disp_on_obj, amoled_AMOLED_disp_on);


STATIC mp_obj_t amoled_AMOLED_backlight_on(mp_obj_t self_in)
{
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(self_in);
    write_spi(self, LCD_CMD_WRDISBV, (uint8_t[]) { 0xFF }, 1);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(amoled_AMOLED_backlight_on_obj, amoled_AMOLED_backlight_on);


STATIC mp_obj_t amoled_AMOLED_backlight_off(mp_obj_t self_in)
{
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(self_in);
    write_spi(self, LCD_CMD_WRDISBV, (uint8_t[]) { 0x00 }, 1);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(amoled_AMOLED_backlight_off_obj, amoled_AMOLED_backlight_off);


STATIC mp_obj_t amoled_AMOLED_brightness(mp_obj_t self_in, mp_obj_t brightness_in)
{
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_int_t brightness = mp_obj_get_int(brightness_in);

    if (brightness > 255) {
        brightness = 255;
    } else if (brightness < 0) {
        brightness = 0;
    }
    write_spi(self, LCD_CMD_WRDISBV, (uint8_t[]) { brightness & 0xFF }, 1);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(amoled_AMOLED_brightness_obj, amoled_AMOLED_brightness);


STATIC mp_obj_t amoled_AMOLED_width(mp_obj_t self_in)
{
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_int(self->width);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(amoled_AMOLED_width_obj, amoled_AMOLED_width);


STATIC mp_obj_t amoled_AMOLED_height(mp_obj_t self_in)
{
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_int(self->height);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(amoled_AMOLED_height_obj, amoled_AMOLED_height);


STATIC mp_obj_t amoled_AMOLED_rotation(size_t n_args, const mp_obj_t *args_in)
{
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args_in[0]);
    self->rotation = mp_obj_get_int(args_in[1]) % 4;
    if (n_args > 2) {
        mp_obj_tuple_t *rotations_in = MP_OBJ_TO_PTR(args_in[2]);
        for (size_t i = 0; i < rotations_in->len; i++) {
            if (i < 4) {
                mp_obj_tuple_t *item = MP_OBJ_TO_PTR(rotations_in->items[i]);
                self->rotations[i].madctl   = mp_obj_get_int(item->items[0]);
                self->rotations[i].width    = mp_obj_get_int(item->items[1]);
                self->rotations[i].height   = mp_obj_get_int(item->items[2]);
                self->rotations[i].colstart = mp_obj_get_int(item->items[3]);
                self->rotations[i].rowstart = mp_obj_get_int(item->items[4]);
            }
        }
    }
    set_rotation(self, self->rotation);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_rotation_obj, 2, 3, amoled_AMOLED_rotation);


STATIC mp_obj_t amoled_AMOLED_vscroll_area(size_t n_args, const mp_obj_t *args_in)
{
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args_in[0]);
    mp_int_t tfa = mp_obj_get_int(args_in[1]);
    mp_int_t vsa = mp_obj_get_int(args_in[2]);
    mp_int_t bfa = mp_obj_get_int(args_in[3]);

    write_spi(
            self,
            LCD_CMD_VSCRDEF,
            (uint8_t []) {
                (tfa) >> 8,
                (tfa) & 0xFF,
                (vsa) >> 8,
                (vsa) & 0xFF,
                (bfa) >> 8,
                (bfa) & 0xFF
            },
            6
    );
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_vscroll_area_obj, 4, 4, amoled_AMOLED_vscroll_area);


STATIC mp_obj_t amoled_AMOLED_vscroll_start(size_t n_args, const mp_obj_t *args_in)
{
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args_in[0]);
    mp_int_t vssa = mp_obj_get_int(args_in[1]);

    if (n_args > 2) {
        if (mp_obj_is_true(args_in[2])) {
            self->madctl_val |= LCD_CMD_ML_BIT;
        } else {
            self->madctl_val &= ~LCD_CMD_ML_BIT;
        }
    } else {
        self->madctl_val &= ~LCD_CMD_ML_BIT;
    }
    write_spi(
        self,
        LCD_CMD_MADCTL,
        (uint8_t[]) { self->madctl_val, },
        2
    );

    write_spi(
        self,
        LCD_CMD_VSCSAD,
        (uint8_t []) { (vssa) >> 8, (vssa) & 0xFF },
        2
    );

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_vscroll_start_obj, 2, 3, amoled_AMOLED_vscroll_start);


// Mapping to Micropython
STATIC const mp_rom_map_elem_t amoled_AMOLED_locals_dict_table[] = {
    /* { MP_ROM_QSTR(MP_QSTR_custom_init),   MP_ROM_PTR(&amoled_AMOLED_custom_init_obj)   }, */
    { MP_ROM_QSTR(MP_QSTR_version),         MP_ROM_PTR(&amoled_AMOLED_version_obj)          },
    { MP_ROM_QSTR(MP_QSTR_deinit),          MP_ROM_PTR(&amoled_AMOLED_deinit_obj)          },
    { MP_ROM_QSTR(MP_QSTR_reset),           MP_ROM_PTR(&amoled_AMOLED_reset_obj)           },
    { MP_ROM_QSTR(MP_QSTR_init),            MP_ROM_PTR(&amoled_AMOLED_init_obj)            },
    { MP_ROM_QSTR(MP_QSTR_send_cmd),        MP_ROM_PTR(&amoled_AMOLED_send_cmd_obj)        },
    { MP_ROM_QSTR(MP_QSTR_refresh),         MP_ROM_PTR(&amoled_AMOLED_refresh_obj)         },
    { MP_ROM_QSTR(MP_QSTR_pixel),           MP_ROM_PTR(&amoled_AMOLED_pixel_obj)           },
    { MP_ROM_QSTR(MP_QSTR_fill),            MP_ROM_PTR(&amoled_AMOLED_fill_obj)            },
	{ MP_ROM_QSTR(MP_QSTR_line),            MP_ROM_PTR(&amoled_AMOLED_line_obj)            },
    { MP_ROM_QSTR(MP_QSTR_hline),           MP_ROM_PTR(&amoled_AMOLED_hline_obj)           },
    { MP_ROM_QSTR(MP_QSTR_vline),           MP_ROM_PTR(&amoled_AMOLED_vline_obj)           },
    { MP_ROM_QSTR(MP_QSTR_rect),            MP_ROM_PTR(&amoled_AMOLED_rect_obj)            },
    { MP_ROM_QSTR(MP_QSTR_fill_rect),       MP_ROM_PTR(&amoled_AMOLED_fill_rect_obj)       },
    { MP_ROM_QSTR(MP_QSTR_bubble_rect),     MP_ROM_PTR(&amoled_AMOLED_bubble_rect_obj)     },
    { MP_ROM_QSTR(MP_QSTR_fill_bubble_rect),MP_ROM_PTR(&amoled_AMOLED_fill_bubble_rect_obj)},
    { MP_ROM_QSTR(MP_QSTR_circle),          MP_ROM_PTR(&amoled_AMOLED_circle_obj)          },
    { MP_ROM_QSTR(MP_QSTR_fill_circle),     MP_ROM_PTR(&amoled_AMOLED_fill_circle_obj)     },
	{ MP_ROM_QSTR(MP_QSTR_trian),           MP_ROM_PTR(&amoled_AMOLED_trian_obj)           },
	{ MP_ROM_QSTR(MP_QSTR_fill_trian),      MP_ROM_PTR(&amoled_AMOLED_fill_trian_obj)      },	
    { MP_ROM_QSTR(MP_QSTR_polygon),         MP_ROM_PTR(&amoled_AMOLED_polygon_obj)         },
    { MP_ROM_QSTR(MP_QSTR_fill_polygon),    MP_ROM_PTR(&amoled_AMOLED_fill_polygon_obj)    },
    { MP_ROM_QSTR(MP_QSTR_polygon_center),  MP_ROM_PTR(&amoled_AMOLED_polygon_center_obj)  },
    { MP_ROM_QSTR(MP_QSTR_colorRGB),        MP_ROM_PTR(&amoled_AMOLED_colorRGB_obj)        },
    { MP_ROM_QSTR(MP_QSTR_bitmap),          MP_ROM_PTR(&amoled_AMOLED_bitmap_obj)          },
    { MP_ROM_QSTR(MP_QSTR_jpg),             MP_ROM_PTR(&amoled_AMOLED_jpg_obj)             },
    { MP_ROM_QSTR(MP_QSTR_jpg_decode),      MP_ROM_PTR(&amoled_AMOLED_jpg_decode_obj)      },
    { MP_ROM_QSTR(MP_QSTR_text),            MP_ROM_PTR(&amoled_AMOLED_text_obj)            },
    { MP_ROM_QSTR(MP_QSTR_text_len),        MP_ROM_PTR(&amoled_AMOLED_text_len_obj)        },
    { MP_ROM_QSTR(MP_QSTR_write),           MP_ROM_PTR(&amoled_AMOLED_write_obj)           },
    { MP_ROM_QSTR(MP_QSTR_write_len),       MP_ROM_PTR(&amoled_AMOLED_write_len_obj)       },
    { MP_ROM_QSTR(MP_QSTR_draw),            MP_ROM_PTR(&amoled_AMOLED_draw_obj)            },
    { MP_ROM_QSTR(MP_QSTR_draw_len),        MP_ROM_PTR(&amoled_AMOLED_draw_len_obj)        },	
    { MP_ROM_QSTR(MP_QSTR_mirror),          MP_ROM_PTR(&amoled_AMOLED_mirror_obj)          },
    { MP_ROM_QSTR(MP_QSTR_swap_xy),         MP_ROM_PTR(&amoled_AMOLED_swap_xy_obj)         },
    { MP_ROM_QSTR(MP_QSTR_set_gap),         MP_ROM_PTR(&amoled_AMOLED_set_gap_obj)         },
    { MP_ROM_QSTR(MP_QSTR_invert_color),    MP_ROM_PTR(&amoled_AMOLED_invert_color_obj)    },
    { MP_ROM_QSTR(MP_QSTR_disp_off),        MP_ROM_PTR(&amoled_AMOLED_disp_off_obj)        },
    { MP_ROM_QSTR(MP_QSTR_disp_on),         MP_ROM_PTR(&amoled_AMOLED_disp_on_obj)         },
    { MP_ROM_QSTR(MP_QSTR_backlight_on),    MP_ROM_PTR(&amoled_AMOLED_backlight_on_obj)    },
    { MP_ROM_QSTR(MP_QSTR_backlight_off),   MP_ROM_PTR(&amoled_AMOLED_backlight_off_obj)   },
    { MP_ROM_QSTR(MP_QSTR_brightness),      MP_ROM_PTR(&amoled_AMOLED_brightness_obj)      },
    { MP_ROM_QSTR(MP_QSTR_height),          MP_ROM_PTR(&amoled_AMOLED_height_obj)          },
    { MP_ROM_QSTR(MP_QSTR_width),           MP_ROM_PTR(&amoled_AMOLED_width_obj)           },
    { MP_ROM_QSTR(MP_QSTR_rotation),        MP_ROM_PTR(&amoled_AMOLED_rotation_obj)        },
    { MP_ROM_QSTR(MP_QSTR_vscroll_area),    MP_ROM_PTR(&amoled_AMOLED_vscroll_area_obj)    },
    { MP_ROM_QSTR(MP_QSTR_vscroll_start),   MP_ROM_PTR(&amoled_AMOLED_vscroll_start_obj)   },
    { MP_ROM_QSTR(MP_QSTR___del__),         MP_ROM_PTR(&amoled_AMOLED_deinit_obj)          },
    { MP_ROM_QSTR(MP_QSTR_RGB),             MP_ROM_INT(COLOR_SPACE_RGB)                      },
    { MP_ROM_QSTR(MP_QSTR_BGR),             MP_ROM_INT(COLOR_SPACE_BGR)                      },
    { MP_ROM_QSTR(MP_QSTR_MONOCHROME),      MP_ROM_INT(COLOR_SPACE_MONOCHROME)               },
};
STATIC MP_DEFINE_CONST_DICT(amoled_AMOLED_locals_dict, amoled_AMOLED_locals_dict_table);


#ifdef MP_OBJ_TYPE_GET_SLOT
MP_DEFINE_CONST_OBJ_TYPE(
    amoled_AMOLED_type,
    MP_QSTR_AMOLED,
    MP_TYPE_FLAG_NONE,
    print, amoled_AMOLED_print,
    make_new, amoled_AMOLED_make_new,
    locals_dict, (mp_obj_dict_t *)&amoled_AMOLED_locals_dict
);
#else
const mp_obj_type_t amoled_AMOLED_type = {
    { &mp_type_type },
    .name        = MP_QSTR_AMOLED,
    .print       = amoled_AMOLED_print,
    .make_new    = amoled_AMOLED_make_new,
    .locals_dict = (mp_obj_dict_t *)&amoled_AMOLED_locals_dict,
};
#endif


STATIC const mp_map_elem_t mp_module_amoled_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),   MP_OBJ_NEW_QSTR(MP_QSTR_amoled)      },
    { MP_ROM_QSTR(MP_QSTR_AMOLED),    (mp_obj_t)&amoled_AMOLED_type       },
    { MP_ROM_QSTR(MP_QSTR_QSPIPanel),  (mp_obj_t)&amoled_qspi_bus_type      },
    { MP_ROM_QSTR(MP_QSTR_RGB),        MP_ROM_INT(COLOR_SPACE_RGB)           },
    { MP_ROM_QSTR(MP_QSTR_BGR),        MP_ROM_INT(COLOR_SPACE_BGR)           },
    { MP_ROM_QSTR(MP_QSTR_MONOCHROME), MP_ROM_INT(COLOR_SPACE_MONOCHROME)    },
    { MP_ROM_QSTR(MP_QSTR_BLACK),      MP_ROM_INT(BLACK)                     },
    { MP_ROM_QSTR(MP_QSTR_BLUE),       MP_ROM_INT(BLUE)                      },
    { MP_ROM_QSTR(MP_QSTR_RED),        MP_ROM_INT(RED)                       },
    { MP_ROM_QSTR(MP_QSTR_GREEN),      MP_ROM_INT(GREEN)                     },
    { MP_ROM_QSTR(MP_QSTR_CYAN),       MP_ROM_INT(CYAN)                      },
    { MP_ROM_QSTR(MP_QSTR_MAGENTA),    MP_ROM_INT(MAGENTA)                   },
    { MP_ROM_QSTR(MP_QSTR_YELLOW),     MP_ROM_INT(YELLOW)                    },
    { MP_ROM_QSTR(MP_QSTR_WHITE),      MP_ROM_INT(WHITE)                     },
};
STATIC MP_DEFINE_CONST_DICT(mp_module_amoled_globals, mp_module_amoled_globals_table);


const mp_obj_module_t mp_module_amoled = {
    .base    = {&mp_type_module},
    .globals = (mp_obj_dict_t *)&mp_module_amoled_globals,
};


#if MICROPY_VERSION >= 0x011300 // MicroPython 1.19 or later
MP_REGISTER_MODULE(MP_QSTR_amoled, mp_module_amoled);
#else
MP_REGISTER_MODULE(MP_QSTR_amoled, mp_module_amoled, MODULE_AMOLED_ENABLE);
#endif
