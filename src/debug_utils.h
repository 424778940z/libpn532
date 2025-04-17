#ifndef _DEBUG_UTILS_H_
#define _DEBUG_UTILS_H_

#include <stdint.h>
#include <stdbool.h>

#define buffer_to_hex_string_len_calc(byte_size) (byte_size * 3 + 1)

bool buffer_to_hex_string(const void* buff, uint32_t buff_len, char* str, uint32_t str_len, uint32_t* processed);

#endif // _DEBUG_UTILS_H_