# M5Paper v1.1 Support

Questo documento descrive l'implementazione del supporto per M5Paper v1.1 in TzCoinMiner.

## Hardware

- **Board**: M5Paper v1.1
- **MCU**: ESP32 DOWDQ6 V3
- **Display**: E-ink 960x540 pixels (4.7" greyscale)
- **Pulsanti**: 
  - Button 1 (GPIO38): Pulsante principale - navigazione pagine
  - Button 2 (GPIO37): Pulsante esterno - azioni specifiche per pagina

## Configurazione PlatformIO

L'environment per M5Paper è configurato in `platformio.ini`:

```ini
[env:m5paper]
platform = espressif32@6.6.0
board = esp32dev
framework = arduino
monitor_speed = 115200
upload_speed = 921600
build_flags = 
    -DBOARD_HAS_PSRAM
    -DARDUINO_USB_CDC_ON_BOOT
    -DCORE_DEBUG_LEVEL=0
    -D CONFIG_FREERTOS_UNICORE=0
    -D CONFIG_MBEDTLS_HARDWARE_SHA=1
    -D DISPLAY_TYPE_M5PAPER=1
    -D M5PAPER_V1_1=1
    ; Ottimizzazioni per performance mining
    -O3
    -funroll-loops
    -ffast-math
board_build.partitions = default.csv
board_build.arduino.memory_type = qio_qspi
lib_deps = 
    bblanchon/ArduinoJson@^7.2.0
    m5stack/M5EPD@^0.1.5
```

### Build Flags Specifici

- `DISPLAY_TYPE_M5PAPER=1`: Identifica la board M5Paper e attiva il codice specifico
- `M5PAPER_V1_1=1`: Specifica la versione hardware 1.1

## Struttura del Codice

### File Dedicati al Display M5Paper

- `src/display_m5paper.h`: Header con definizioni e prototipi funzioni
- `src/display_m5paper.cpp`: Implementazione logica display E-ink

### Gestione Condizionale in main.cpp

Il file `main.cpp` utilizza direttive di preprocessore per selezionare automaticamente il codice corretto in base al build flag `DISPLAY_TYPE_M5PAPER`:

```cpp
#ifdef DISPLAY_TYPE_M5PAPER
    #include "display_m5paper.h"
    // Definizioni specifiche M5Paper
#else
    #include "display.h"
    // Definizioni per T-Display AMOLED
#endif
```

## Caratteristiche Display E-ink

### Modalità di Aggiornamento

Il display E-ink supporta diverse modalità di refresh:

- **UPDATE_MODE_INIT**: Refresh completo con inizializzazione
- **UPDATE_MODE_GC16**: Alta qualità, 16 livelli di grigio
- **UPDATE_MODE_GL16**: Aggiornamento veloce, 16 livelli di grigio
- **UPDATE_MODE_DU4**: Aggiornamento più veloce, 4 livelli di grigio

### Ottimizzazioni

Per ridurre il consumo energetico e l'usura del display:

1. **Refresh Selettivo**: Il display viene aggiornato solo quando cambiano i dati
2. **Refresh Completo Periodico**: Ogni 5 minuti per prevenire ghosting
3. **Refresh Parziale Limitato**: Massimo 1 aggiornamento al secondo
4. **Cache Stato**: Traccia i valori precedenti per evitare refresh inutili

## Pagine Disponibili

### 1. PAGE_LOGO_M5
- Logo e informazioni generali
- Nome device e versione ESP32
- Istruzioni base

### 2. PAGE_MINING_M5
- Statistiche mining in tempo reale
- Hashrate (kH/s per Bitcoin, H/s per Duino-Coin)
- Shares trovate
- Best difficulty (solo Bitcoin/BCH)
- Uptime sistema

### 3. PAGE_SETUP_M5
- Configurazione WiFi
- IP address quando connesso
- Configurazione mining
- Modalità auto-start

## Controlli

### Pulsante 1 (GPIO38) - Navigazione
Pressione breve: Cambio pagina (ciclico)

### Pulsante 2 (GPIO37) - Azioni
- **Pagina Logo**: Refresh display
- **Pagina Mining**: Start/Stop mining
- **Pagina Setup**: Attiva/Disattiva AP WiFi

Pressione lunga (1s+):
- **Pagina Mining**: Toggle Solo/Pool mode (solo Bitcoin)

## Build e Upload

### Build per M5Paper
```bash
pio run -e m5paper
```

### Upload su M5Paper
```bash
pio run -e m5paper -t upload
```

### Monitor Seriale
```bash
pio device monitor -e m5paper
```

## Differenze rispetto a T-Display AMOLED

| Caratteristica | T-Display AMOLED | M5Paper E-ink |
|---------------|------------------|---------------|
| Risoluzione | 536x240 | 960x540 |
| Colori | 65K colori | 16 livelli grigio |
| Refresh Rate | 50fps (logo), 1fps (altre) | 1fps max |
| Consumo | Alto (backlight) | Bassissimo |
| Visibilità sole | Scarsa | Eccellente |
| Animazioni | Si | No |
| Ghosting | No | Si (richiede refresh periodici) |

## Note Tecniche

### Consumo Energetico
Il display E-ink consuma energia solo durante l'aggiornamento, rendendo M5Paper ideale per:
- Mining a batteria
- Installazioni remote
- Uso prolungato

### Durata Display
Per massimizzare la durata del display E-ink:
- Minimizzare i refresh completi
- Usare refresh parziali quando possibile
- Implementare timeout per display in sleep

### Memory Usage
Il canvas M5Paper richiede:
- Buffer: 960 x 540 x 1 byte = ~518KB
- Heap disponibile: ~4MB PSRAM
- Stack task monitor: 10KB

## Troubleshooting

### Display non si aggiorna
- Verificare che M5.EPD.Clear() sia stato chiamato all'inizializzazione
- Controllare che canvas.pushCanvas() venga eseguito
- Provare un refresh completo con UPDATE_MODE_GC16

### Ghosting persistente
- Eseguire M5.EPD.Clear(true) per clear completo
- Ridurre intervallo tra refresh completi

### Consumo elevato
- Verificare che il display non si aggiorni troppo frequentemente
- Controllare la logica di change detection nello stato cache
