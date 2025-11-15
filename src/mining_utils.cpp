// Mining utility functions (from NerdMiner)
#include "mining_utils.h"
#include <Arduino.h>

// Bitcoin difficulty 1 target  
// Use the EXACT method from NerdMiner - just copy their constant directly
// The issue was compiler optimization, so we use volatile to prevent optimization
static double get_truediffone() {
    // Use the exact same constant as NerdMiner but with explicit double precision
    // This value is 0x00000000FFFF0000000000000000000000000000000000000000000000000000
    volatile double result = 2.695953529101130949315647634472399133601089873857416408613777309696e76;
    return result;
}

/* Converts a little endian 256 bit value to a double */
double le256todouble(const void *target) 
{
	const uint64_t *data64;
	double dcut64;

	data64 = (const uint64_t *)((const uint8_t*)target + 24);
	dcut64 = *data64 * 6277101735386680763835789423207666416102355444464034512896.0;

	data64 = (const uint64_t *)((const uint8_t*)target + 16);
	dcut64 += *data64 * 340282366920938463463374607431768211456.0;

	data64 = (const uint64_t *)((const uint8_t*)target + 8);
	dcut64 += *data64 * 18446744073709551616.0;

	data64 = (const uint64_t *)(target);
	dcut64 += *data64;

    return dcut64;
}

double diff_from_target(const void *target)
{
	double d64, dcut64;

	d64 = get_truediffone();
	dcut64 = le256todouble(target);
	if (dcut64 == 0.0)
		dcut64 = 1.0;
	
	return d64 / dcut64;
}

bool isSha256Valid(const void* sha256)
{
    for(uint8_t i=0; i < 8; ++i)
    {
        if ( ((const uint32_t*)sha256)[i] != 0 ) 
            return true;
    }
    return false;
}

uint32_t swab32(uint32_t v) {
    return ((v << 24) & 0xff000000) | 
           ((v << 8) & 0xff0000) | 
           ((v >> 8) & 0xff00) | 
           ((v >> 24) & 0xff);
}
