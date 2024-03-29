// http://www.mouser.com/ds/2/365/LS013B7DH03%20SPEC_SMA-224806.pdf
// https://www.embeddedartists.com/sites/default/files/support/datasheet/Memory_LCD_Programming.pdf
// https://www.silabs.com/documents/public/application-notes/AN0048.pdf

#include <bc_module_lcd.h>
#include <stm32l0xx.h>
#include <bc_spi.h>
#include <bc_tca9534a.h>
#include <bc_scheduler.h>

#define _BC_MODULE_LCD_DISP_ON   0x04
#define _BC_MODULE_LCD_LED_GREEN 0x10
#define _BC_MODULE_LCD_LED_RED   0x20
#define _BC_MODULE_LCD_LED_BLUE  0x40
#define _BC_MODULE_LCD_DISP_CS   0x80
#define _BC_MODULE_LCD_VCOM_PERIOD 15000

typedef struct bc_module_lcd_t
{
    void (*event_handler)(bc_module_lcd_event_t, void *);
    void *event_param;
    bc_tca9534a_t tca9534a;
    uint8_t *framebuffer;
    const bc_font_t *font;
    uint8_t gpio;
    bc_module_lcd_rotation_t rotation;
    uint8_t vcom;
    bc_scheduler_task_id_t task_id;

} bc_module_lcd_t;

bc_module_lcd_t _bc_module_lcd;

uint8_t reverse2(uint32_t b) {

	return __RBIT(b) >> 24;
}

static void _bc_module_lcd_spi_transfer(uint8_t *buffer, size_t length);
static void _bc_module_lcd_task(void *param);

void bc_module_lcd_init(bc_module_lcd_framebuffer_t *framebuffer)
{
    bc_module_lcd_t *self = &_bc_module_lcd;

    bc_tca9534a_init(&_bc_module_lcd.tca9534a, BC_I2C_I2C0, 0x39);
    bc_tca9534a_set_port_direction(&_bc_module_lcd.tca9534a, 0x00);
    _bc_module_lcd.gpio = _BC_MODULE_LCD_DISP_CS | _BC_MODULE_LCD_DISP_ON | _BC_MODULE_LCD_LED_GREEN | _BC_MODULE_LCD_LED_RED | _BC_MODULE_LCD_LED_BLUE;
    bc_tca9534a_write_port(&_bc_module_lcd.tca9534a, _bc_module_lcd.gpio);

    bc_spi_init(BC_SPI_SPEED_2_MHZ, BC_SPI_MODE_0);

    self->framebuffer = framebuffer->framebuffer;

    // Address lines
    uint8_t line;
    uint32_t offs;
    for (line = 0x01, offs = 1; line <= 128; line++, offs += 18) {
        // Fill the gate line addresses on the exact place in the buffer
        self->framebuffer[offs] = reverse2(line);
    }

    // Prepare buffer so the background is "white" reflective
    bc_module_lcd_clear();

    _bc_module_lcd.task_id = bc_scheduler_register(_bc_module_lcd_task, NULL, _BC_MODULE_LCD_VCOM_PERIOD);
}

void bc_module_lcd_on(void)
{
    _bc_module_lcd.gpio |= _BC_MODULE_LCD_DISP_ON;
    bc_tca9534a_write_port(&_bc_module_lcd.tca9534a, _bc_module_lcd.gpio);
}

void bc_module_lcd_off(void)
{
    _bc_module_lcd.gpio &= ~_BC_MODULE_LCD_DISP_ON;
    bc_tca9534a_write_port(&_bc_module_lcd.tca9534a, _bc_module_lcd.gpio);
}

void bc_module_lcd_clear(void)
{
    uint32_t x;
    uint32_t y;

    for (y = 0; y < 128; y++)
    {
        for (x = 0; x < 128; x++)
        {
            bc_module_lcd_draw_pixel(x, y, false);
        }
    }
}

void bc_module_lcd_draw_pixel(int x, int y, bool value)
{
    if(x > 127 || y > 127 || x < 0 || y < 0)
    {
        return;
    }

    bc_module_lcd_t *self = &_bc_module_lcd;

    int tmp;

    switch(self->rotation)
    {
        case BC_MODULE_LCD_ROTATION_90 :
            tmp = x;
            x = 127 - y;
            y = tmp;
            break;
        case BC_MODULE_LCD_ROTATION_180:
            x = 127 - x;
            y = 127 - y;
            break;
        case BC_MODULE_LCD_ROTATION_270:
            tmp = y;
            y = 127 - x;
            x = tmp;
            break;
        case BC_MODULE_LCD_ROTATION_0:
            break;
        default:
            break;
    }

    // Skip mode byte + addr byte
    uint32_t byteIndex = 2;
    // Skip lines
    byteIndex += y * 18;
    // Select column byte
    byteIndex += x / 8;

    uint8_t bitMask = 1 << (7 - (x % 8));

    if(!value)
    {
        self->framebuffer[byteIndex] |= bitMask;
    }
    else
    {
        self->framebuffer[byteIndex] &= ~bitMask;
    }
}

int bc_module_lcd_draw_char(int left, int top, uint8_t ch)
{
    bc_module_lcd_t *self = &_bc_module_lcd;

    const bc_font_t *font = self->font;

    int w = 0;
    uint8_t h = 0;
    uint16_t i;
    uint16_t x;
    uint16_t y;
    uint8_t bytes;

    for (i = 0; i < font->length; i++)
    {
        if (font->chars[i].code == ch)
        {
            w = font->chars[i].image->width;
            h = font->chars[i].image->heigth;

            bytes = (w + 7) / 8;

            for (y = 0; y < h; y++)
            {
                for (x = 0; x < w; x++)
                {
                    uint32_t byteIndex = x / 8;
                    byteIndex += y * bytes;

                    uint8_t bitMask = 1 << (7 - (x % 8));

                    if (font->chars[i].image->image[byteIndex] & bitMask)
                    {
                        bc_module_lcd_draw_pixel(left + x, top + y, false);
                    }
                    else
                    {
                        bc_module_lcd_draw_pixel(left + x, top + y, true);
                    }
                }
            }
        }
    }

    return w;
}

int bc_module_lcd_draw_string(int left, int top, char *str)
{
    while(*str)
    {
        left += bc_module_lcd_draw_char(left, top, *str);
        str++;
    }
    return left;
}


void bc_module_lcd_draw(const uint8_t *frame, uint8_t width, uint8_t height) // In pixels
{
    (void)frame;
    (void)width;
    (void)height;
}

void bc_module_lcd_printf(uint8_t line, /*uint8_t size, font, */const uint8_t *string/*, ...*/)
{
    (void) line;
    (void) string;
}
/*

Framebuffer format for updating multiple lines, ideal for later DMA TX:

||    Set MODE      ||------18B for line---||--next 18B 2nd line--| ...
||        1B        ||   1B |  16B |  1B   ||   1B |  16B |  1B   |
||  M0 M1 M2  DUMMY || ADDR | DATA | DUMMY || ADDR | DATA | DUMMY |

*/
void bc_module_lcd_update(void)
{
    bc_module_lcd_t *self = &_bc_module_lcd;
    self->framebuffer[0] = 0x80 | self->vcom;

    _bc_module_lcd_spi_transfer(self->framebuffer, BC_LCD_FRAMEBUFFER_SIZE);

    self->vcom ^= 0x40;

    bc_scheduler_plan_relative(self->task_id, _BC_MODULE_LCD_VCOM_PERIOD);
}

void bc_module_lcd_clear_memory_command(void)
{
    uint8_t spi_data[2] = {0x20, 0x00};

    _bc_module_lcd_spi_transfer(spi_data, sizeof(spi_data));
}

void bc_module_lcd_set_font(const bc_font_t *font)
{
    _bc_module_lcd.font = font;
}

void bc_module_lcd_set_rotation(bc_module_lcd_rotation_t rotation)
{
    _bc_module_lcd.rotation = rotation;
}

bc_module_lcd_rotation_t bc_module_lcd_get_rotation(void)
{
    return _bc_module_lcd.rotation;
}

static void _bc_module_lcd_spi_transfer(uint8_t *buffer, size_t length)
{
    _bc_module_lcd.gpio &= ~_BC_MODULE_LCD_DISP_CS;
    bc_tca9534a_write_port(&_bc_module_lcd.tca9534a, _bc_module_lcd.gpio);

    bc_spi_transfer(buffer, NULL, length);

    _bc_module_lcd.gpio |= _BC_MODULE_LCD_DISP_CS;
    bc_tca9534a_write_port(&_bc_module_lcd.tca9534a, _bc_module_lcd.gpio);
}

static void _bc_module_lcd_task(void *param)
{
    (void) param;

    uint8_t spi_data[2] = {_bc_module_lcd.vcom, 0x00};

    _bc_module_lcd_spi_transfer(spi_data, sizeof(spi_data));

    _bc_module_lcd.vcom ^= 0x40;

    bc_scheduler_plan_current_relative(_BC_MODULE_LCD_VCOM_PERIOD);
}
