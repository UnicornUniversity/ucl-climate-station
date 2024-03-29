#include <usb_talk.h>
#include <bc_scheduler.h>
#include <bc_usb_cdc.h>
#include <base64.h>
#include <application.h>

#define USB_TALK_TOKEN_ARRAY         0
#define USB_TALK_TOKEN_TOPIC         1
#define USB_TALK_TOKEN_PAYLOAD       2
#define USB_TALK_TOKEN_PAYLOAD_KEY   3
#define USB_TALK_TOKEN_PAYLOAD_VALUE 4

#define USB_TALK_SUBSCRIBES 16

static struct
{
    char tx_buffer[256];
    char rx_buffer[1024];
    size_t rx_length;
    bool rx_error;

    struct {
        const char *topic;
        usb_talk_sub_callback_t callback;
        void *param;

    } subscribes[USB_TALK_SUBSCRIBES];
    size_t subscribes_length;

} _usb_talk;

static void _usb_talk_task(void *param);
static void _usb_talk_process_character(char character);
static void _usb_talk_process_message(char *message, size_t length);
static bool _usb_talk_token_get_int(const char *buffer, jsmntok_t *token, int *value);

void usb_talk_init(void)
{
    memset(&_usb_talk, 0, sizeof(_usb_talk));

    bc_usb_cdc_init();

    bc_scheduler_register(_usb_talk_task, NULL, 0);
}

void usb_talk_start(void)
{
    bc_usb_cdc_start();
}

void usb_talk_sub(const char *topic, usb_talk_sub_callback_t callback, void *param)
{
    if (_usb_talk.subscribes_length >= USB_TALK_SUBSCRIBES){
        return;
    }
    _usb_talk.subscribes[_usb_talk.subscribes_length].topic = topic;
    _usb_talk.subscribes[_usb_talk.subscribes_length].callback = callback;
    _usb_talk.subscribes[_usb_talk.subscribes_length].param = param;
    _usb_talk.subscribes_length++;
}

void usb_talk_send_string(const char *buffer)
{
    bc_usb_cdc_write(buffer, strlen(buffer));
}

void usb_talk_publish_led(const char *prefix, bool *state)
{
    snprintf(_usb_talk.tx_buffer, sizeof(_usb_talk.tx_buffer),
                "[\"%s/led/-/state\", %s]\n",
                prefix, *state ? "true" : "false");

    usb_talk_send_string((const char *) _usb_talk.tx_buffer);
}

void usb_talk_publish_push_button(const char *prefix, uint16_t *event_count)
{
    snprintf(_usb_talk.tx_buffer, sizeof(_usb_talk.tx_buffer),
             "[\"%s/push-button/-/event-count\", %" PRIu16 "]\n",
             prefix, *event_count);

    usb_talk_send_string((const char *) _usb_talk.tx_buffer);
}

void usb_talk_publish_thermometer(const char *prefix, uint8_t *i2c, float *temperature)
{

    uint8_t number = (*i2c & ~0x80) == BC_TAG_TEMPERATURE_I2C_ADDRESS_DEFAULT ? 0 : 1;

    snprintf(_usb_talk.tx_buffer, sizeof(_usb_talk.tx_buffer),
                "[\"%s/thermometer/%d:%d/temperature\", %0.2f]\n",
                prefix, ((*i2c & 0x80) >> 7), number, *temperature);

    usb_talk_send_string((const char *) _usb_talk.tx_buffer);
}

void usb_talk_publish_humidity_sensor(const char *prefix, uint8_t *i2c, float *relative_humidity)
{

    uint8_t number;

    switch((*i2c & ~0x80))
    {
        case 0x5f:
            number = 0;
            break;
        case 0x40:
            number = 2;
            break;
        case 0x41:
            number = 3;
            break;
        default:
            number = 0;
    }

    snprintf(_usb_talk.tx_buffer, sizeof(_usb_talk.tx_buffer),
                "[\"%s/hygrometer/%d:%d/relative-humidity\", %0.1f]\n",
                prefix, ((*i2c & 0x80) >> 7), number, *relative_humidity);

    usb_talk_send_string((const char *) _usb_talk.tx_buffer);
}

void usb_talk_publish_lux_meter(const char *prefix, uint8_t *i2c, float *illuminance)
{

    uint8_t number = (*i2c & ~0x80) == BC_TAG_LUX_METER_I2C_ADDRESS_DEFAULT ? 0 : 1;

    snprintf(_usb_talk.tx_buffer, sizeof(_usb_talk.tx_buffer),
                "[\"%s/lux-meter/%d:%d/illuminance\", %0.1f]\n",
                prefix, ((*i2c & 0x80) >> 7), number, *illuminance);

    usb_talk_send_string((const char *) _usb_talk.tx_buffer);
}

void usb_talk_publish_barometer(const char *prefix, uint8_t *i2c, float *pressure, float *altitude)
{
    snprintf(_usb_talk.tx_buffer, sizeof(_usb_talk.tx_buffer),
                "[\"%s/barometer/%d:0/pressure\", %0.2f]\n",
                prefix, ((*i2c & 0x80) >> 7), *pressure);

    usb_talk_send_string((const char *) _usb_talk.tx_buffer);

    snprintf(_usb_talk.tx_buffer, sizeof(_usb_talk.tx_buffer),
                "[\"%s/barometer/%d:0/altitude\", %0.2f]\n",
                prefix, ((*i2c & 0x80) >> 7), *altitude);

    usb_talk_send_string((const char *) _usb_talk.tx_buffer);
}

void usb_talk_publish_co2_concentation(const char *prefix, float *concentration)
{
    snprintf(_usb_talk.tx_buffer, sizeof(_usb_talk.tx_buffer),
                "[\"%s/co2-meter/-/concentration\", %0.2f]\n",
                prefix, *concentration);

    usb_talk_send_string((const char *) _usb_talk.tx_buffer);
}

void usb_talk_publish_light(const char *prefix, bool *state)
{
    snprintf(_usb_talk.tx_buffer, sizeof(_usb_talk.tx_buffer),
                "[\"%s/light/-/state\", %s]\n",
                prefix, *state ? "true" : "false");

    usb_talk_send_string((const char *) _usb_talk.tx_buffer);
}

void usb_talk_publish_relay(const char *prefix, bool *state)
{
    snprintf(_usb_talk.tx_buffer, sizeof(_usb_talk.tx_buffer),
                "[\"%s/relay/-/state\", %s]\n",
                prefix, *state ? "true" : "false");

    usb_talk_send_string((const char *) _usb_talk.tx_buffer);
}

void usb_talk_publish_module_relay(const char *prefix, uint8_t *number, bc_module_relay_state_t *state)
{
    if (*state == BC_MODULE_RELAY_STATE_UNKNOWN)
    {
        snprintf(_usb_talk.tx_buffer, sizeof(_usb_talk.tx_buffer),
                    "[\"%s/relay/0:%d/state\", null]\n",
                    prefix, *number);
    }
    else
    {
        snprintf(_usb_talk.tx_buffer, sizeof(_usb_talk.tx_buffer),
                    "[\"%s/relay/0:%d/state\", %s]\n",
                    prefix, *number, *state == BC_MODULE_RELAY_STATE_TRUE ? "true" : "false");
    }

    usb_talk_send_string((const char *) _usb_talk.tx_buffer);
}

void usb_talk_publish_led_strip_config(const char *prefix, const char *mode, int *count)
{
    snprintf(_usb_talk.tx_buffer, sizeof(_usb_talk.tx_buffer),
                "[\"%s/led-strip/-/config\", {\"mode\": \"%s\", \"count\": %d}]\n",
                prefix, mode, *count );

    usb_talk_send_string((const char *) _usb_talk.tx_buffer);
}

static void _usb_talk_task(void *param)
{
    (void) param;

    while (true)
    {
        static uint8_t buffer[16];

        size_t length = bc_usb_cdc_read(buffer, sizeof(buffer));

        if (length == 0)
        {
            break;
        }

        for (size_t i = 0; i < length; i++)
        {
            _usb_talk_process_character((char) buffer[i]);
        }
    }

    bc_scheduler_plan_current_now();
}

static void _usb_talk_process_character(char character)
{
    if (character == '\n')
    {
        if (!_usb_talk.rx_error && _usb_talk.rx_length > 0)
        {
            _usb_talk_process_message(_usb_talk.rx_buffer, _usb_talk.rx_length);
        }

        _usb_talk.rx_length = 0;
        _usb_talk.rx_error = false;

        return;
    }

    if (_usb_talk.rx_length == sizeof(_usb_talk.rx_buffer))
    {
        _usb_talk.rx_error = true;
    }
    else
    {
        if (!_usb_talk.rx_error)
        {
            _usb_talk.rx_buffer[_usb_talk.rx_length++] = character;
        }
    }
}

static void _usb_talk_process_message(char *message, size_t length)
{
    static jsmn_parser parser;
    static jsmntok_t tokens[16];

    jsmn_init(&parser);

    int token_count = jsmn_parse(&parser, (const char *) message, length, tokens, sizeof(tokens));

    if (token_count < 3)
    {
        return;
    }

    if (tokens[USB_TALK_TOKEN_ARRAY].type != JSMN_ARRAY || tokens[USB_TALK_TOKEN_ARRAY].size != 2)
    {
        return;
    }

    if (tokens[USB_TALK_TOKEN_TOPIC].type != JSMN_STRING || tokens[USB_TALK_TOKEN_TOPIC].size != 0)
    {
        return;
    }

    for (size_t i = 0; i < _usb_talk.subscribes_length; i++)
    {
        if (usb_talk_is_string_token_equal(message, &tokens[USB_TALK_TOKEN_TOPIC], _usb_talk.subscribes[i].topic))
        {
            usb_talk_payload_t payload = {
                    message,
                    token_count - USB_TALK_TOKEN_PAYLOAD,
                    tokens + USB_TALK_TOKEN_PAYLOAD
            };
            _usb_talk.subscribes[i].callback(&payload, _usb_talk.subscribes[i].param);
        }
    }
}

bool usb_talk_payload_get_bool(usb_talk_payload_t *payload, bool *value)
{
    if (usb_talk_is_string_token_equal(payload->buffer, &payload->tokens[0], "true"))
    {
        *value = true;
        return true;
    }
    else if (usb_talk_is_string_token_equal(payload->buffer, &payload->tokens[0], "false"))
    {
        *value = false;
        return true;
    }
    return false;
}

bool usb_talk_payload_get_key_bool(usb_talk_payload_t *payload, const char *key, bool *value)
{
    if (payload->tokens[0].type != JSMN_OBJECT)
    {
        return false;
    }

    for (int i = 1; i + 1 < payload->token_count; i += 2)
    {
        if (usb_talk_is_string_token_equal(payload->buffer, &payload->tokens[i], key))
        {
            if (usb_talk_is_string_token_equal(payload->buffer, &payload->tokens[i + 1], "true"))
            {
                *value = true;
                return true;
            }
            else if (usb_talk_is_string_token_equal(payload->buffer, &payload->tokens[i + 1], "false"))
            {
                *value = false;
                return true;
            }
            else
            {
                return false;
            }
        }
    }
    return false;
}

bool usb_talk_payload_get_data(usb_talk_payload_t *payload, uint8_t *buffer, size_t *length)
{
    if (payload->tokens[0].type != JSMN_STRING)
    {
        return false;
    }

    uint32_t input_length = payload->tokens[0].end - payload->tokens[0].start;

    size_t data_length = base64_calculate_decode_length(&payload->buffer[payload->tokens[0].start], input_length);

    if (data_length > *length)
    {
        return false;
    }

    return base64_decode(&payload->buffer[payload->tokens[0].start], input_length, buffer, (uint32_t *)length);
}

bool usb_talk_payload_get_key_data(usb_talk_payload_t *payload, const char *key, uint8_t *buffer, size_t *length)
{
    if (payload->tokens[0].type != JSMN_OBJECT)
    {
        return false;
    }

    for (int i = 1; i + 1 < payload->token_count; i += 2)
    {
        if (usb_talk_is_string_token_equal(payload->buffer, &payload->tokens[i], key))
        {
            if (payload->tokens[i + 1].type != JSMN_STRING)
            {
                return false;
            }

            uint32_t input_length = payload->tokens[i + 1].end - payload->tokens[i + 1].start;

            size_t data_length = base64_calculate_decode_length(&payload->buffer[payload->tokens[i + 1].start], input_length);

            if (data_length > *length)
            {
                return false;
            }

            return base64_decode(&payload->buffer[payload->tokens[i + 1].start], input_length, buffer, (uint32_t *)length);
        }
    }
    return false;
}

bool usb_talk_payload_get_enum(usb_talk_payload_t *payload, int *value, ...)
{
    char temp[11];
    char *str;
    int j = 0;

    jsmntok_t *token = &payload->tokens[0];

    if (token->type != JSMN_STRING)
    {
        return false;
    }

    size_t length = token->end - token->start;

    if (length > (sizeof(temp) - 1))
    {
        return false;
    }

    memset(temp, 0x00, sizeof(temp));

    strncpy(temp, payload->buffer + token->start, length);

    va_list vl;
    va_start(vl, value);
    str = va_arg(vl, char*);
    while (str != NULL)
    {
        if (strcmp(str, temp) == 0)
        {
            *value = j;
            return true;
        }
        str = va_arg(vl, char*);
        j++;
    }
    va_end(vl);

    return false;
}

bool usb_talk_payload_get_key_enum(usb_talk_payload_t *payload, const char *key, int *value, ...)
{
    char temp[11];
    char *str;
    int j = 0;

    if (payload->tokens[0].type != JSMN_OBJECT)
    {
        return false;
    }

    for (int i = 1; i + 1 < payload->token_count; i += 2)
    {
        if (usb_talk_is_string_token_equal(payload->buffer, &payload->tokens[i], key))
        {

            jsmntok_t *token = &payload->tokens[i + 1];
            size_t length = token->end - token->start;

            if (length > (sizeof(temp) - 1))
            {
                return false;
            }

            memset(temp, 0x00, sizeof(temp));

            strncpy(temp, payload->buffer + token->start, length);

            va_list vl;
            va_start(vl, value);
            str = va_arg(vl, char*);
            while (str != NULL)
            {
                if (strcmp(str, temp) == 0)
                {
                    *value = j;
                    return true;
                }
                str = va_arg(vl, char*);
                j++;
            }
            va_end(vl);

            return false;
        }
    }
    return false;
}

bool usb_talk_payload_get_int(usb_talk_payload_t *payload, int *value)
{
    return _usb_talk_token_get_int(payload->buffer, &payload->tokens[0], value);
}

bool usb_talk_payload_get_key_int(usb_talk_payload_t *payload, const char *key, int *value)
{
    for (int i = 1; i + 1 < payload->token_count; i += 2)
    {
        if (usb_talk_is_string_token_equal(payload->buffer, &payload->tokens[i], key))
        {
            return _usb_talk_token_get_int(payload->buffer, &payload->tokens[i + 1], value);
        }
    }
    return false;
}

bool usb_talk_payload_get_string(usb_talk_payload_t *payload, char *buffer, size_t *length)
{
    if (payload->tokens[0].type != JSMN_STRING)
    {
        return false;
    }
    uint32_t token_length = payload->tokens[0].end - payload->tokens[0].start;
    if (token_length > *length)
    {
        return false;
    }
    strncpy(buffer, &payload->buffer[payload->tokens[0].start], token_length);
    *length = token_length;
    return true;
}

bool usb_talk_payload_get_key_string(usb_talk_payload_t *payload, const char *key, char *buffer, size_t *length)
{
    for (int i = 1; i + 1 < payload->token_count; i += 2)
    {
        if (usb_talk_is_string_token_equal(payload->buffer, &payload->tokens[i], key))
        {
            if (payload->tokens[i + 1].type != JSMN_STRING)
            {
                return false;
            }
            uint32_t token_length = payload->tokens[i + 1].end - payload->tokens[i + 1].start;
            if (token_length > *length)
            {
                return false;
            }
            strncpy(buffer, &payload->buffer[payload->tokens[i + 1].start], token_length);
            *length = token_length;
            return true;
        }
    }
    return false;
}

bool usb_talk_is_string_token_equal(const char *buffer, jsmntok_t *token, const char *string)
{
    size_t token_length;

    token_length = (size_t) (token->end - token->start);

    if (strlen(string) != token_length)
    {
        return false;
    }

    if (strncmp(string, &buffer[token->start], token_length) != 0)
    {
        return false;
    }

    return true;
}

static bool _usb_talk_token_get_int(const char *buffer, jsmntok_t *token, int *value)
{
    if (token->type != JSMN_PRIMITIVE)
    {
        return false;
    }

    size_t length = (size_t) (token->end - token->start);

    char str[10 + 1];

    if (length > sizeof(str))
    {
        return false;
    }

    memset(str, 0, sizeof(str));

    strncpy(str, buffer + token->start, length);

    if (strncmp(str, "null", sizeof(str)) == 0)
    {
        return USB_TALK_INT_VALUE_NULL;
    }

    if (strchr(str, 'e'))
    {
        *value = (int) strtof(str, NULL);
    }
    else
    {
        *value = (int) strtol(str, NULL, 10);
    }

    return true;
}