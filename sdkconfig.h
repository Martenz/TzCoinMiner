// Custom SDK configuration for ESP32-S3
// Forces hardware SHA-256 acceleration

#ifndef SDKCONFIG_H
#define SDKCONFIG_H

// Enable hardware SHA acceleration
#define CONFIG_MBEDTLS_HARDWARE_SHA 1
#define CONFIG_MBEDTLS_SHA256_C 1

// Disable software fallback
#undef CONFIG_MBEDTLS_SHA256_SMALLER

// Performance optimizations
#define CONFIG_ESP32S3_DEFAULT_CPU_FREQ_240 1
#define CONFIG_FREERTOS_HZ 1000

#endif // SDKCONFIG_H
