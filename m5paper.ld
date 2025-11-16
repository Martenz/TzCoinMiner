/* ESP32 linker script with increased IRAM for M5Paper */

MEMORY
{
  /* IRAM for CPU - Increased from default */
  iram0_0_seg (RX) :   org = 0x40080000, len = 0x20000  /* 128KB instead of default */
  
  /* IRAM for PRO CPU cache */
  iram0_2_seg (RX) :   org = 0x400D0018, len = 0x330000

  /* DROM */
  drom0_0_seg (R) :    org = 0x3F400018, len = 0x400000
  
  /* DRAM */
  dram0_0_seg (RW) :   org = 0x3FFB0000, len = 0x50000
  
  /* RTC fast memory */
  rtc_iram_seg(RWX) :  org = 0x400C0000, len = 0x2000
  
  /* RTC slow memory */
  rtc_slow_seg(RW) :   org = 0x50000000, len = 0x1000

  /* external flash */
  irom0_0_seg (RX) :   org = 0x400D0018, len = 0x330000
}

/* Include the rest from default linker script */
INCLUDE "esp32.project.ld"
