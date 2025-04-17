#include "debug_utils.h"

#include <stdio.h>

bool buffer_to_hex_string(const void* buff, uint32_t buff_len, char* str, uint32_t str_len, uint32_t* processed)
{
    const uint32_t byte_str_len = 2; // "xx"

    if ( (buff_len * byte_str_len) + 1 > str_len )
        return false;

    char* string_p = str;

    for ( uint32_t i = 0; i < buff_len; i++ )
    {
        snprintf(string_p, byte_str_len + 1, "%02X", *((uint8_t*)(buff) + i));
        string_p += byte_str_len;
        if ( processed != NULL )
            *processed = i + 1;
    }

    return true;
}
