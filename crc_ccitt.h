#ifndef __CRC_CCITT_H__
#define __CRC_CCITT_H__

#define CRC_16_L_SEED           0xFFFF

uint16_t crc_ccitt(uint16_t crc, uint8_t const *buffer, size_t len);
uint16_t crc_ccitt_byte(uint16_t crc, const uint8_t c);

#endif
