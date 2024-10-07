#ifndef __amoled_qspi_bus_H__
#define __amoled_qspi_bus_H__

#include "mphalport.h"
#include "py/obj.h"
#include "esp_lcd_panel_io.h"
#include "driver/spi_master.h"

typedef struct _amoled_panel_p_t {
    void (*tx_param)(mp_obj_base_t *self, int lcd_cmd, const void *param, size_t param_size);
    void (*tx_color)(mp_obj_base_t *self, int lcd_cmd, const void *color, size_t color_size);
    void (*deinit)(mp_obj_base_t *self);
} amoled_panel_p_t;

typedef struct _amoled_qspi_bus_obj_t {
    mp_obj_base_t base;
    mp_obj_base_t *spi_obj;
    uint16_t width;
    uint16_t height;

    mp_hal_pin_obj_t databus_pins[4];
    mp_hal_pin_obj_t dc_pin;
    mp_hal_pin_obj_t cs_pin;

    mp_obj_t databus[4];
    mp_obj_t dc;
    mp_obj_t cs;

    uint32_t pclk;
    int cmd_bits;
    int param_bits;

    // spi_device_handle_t io_handle;
    enum {
        MACHINE_HW_QSPI_STATE_NONE,
        MACHINE_HW_QSPI_STATE_INIT,
        MACHINE_HW_QSPI_STATE_DEINIT
    } state;
} amoled_qspi_bus_obj_t;

extern const mp_obj_type_t amoled_qspi_bus_type;

#endif
