#ifndef STRATUM_CLIENT_H
#define STRATUM_CLIENT_H

#include <Arduino.h>
#include <vector>

// Struttura per un job di mining Stratum
struct stratum_job_t {
    String job_id;
    String prev_hash;
    String coinb1;
    String coinb2;
    std::vector<String> merkle_branch;
    String version;
    String nbits;
    String ntime;
    bool clean_jobs;
    String extranonce1;
    int extranonce2_size;
};

// Callback quando arriva un nuovo job
typedef void (*stratum_job_callback_t)(stratum_job_t* job);

// Inizializza il client Stratum
void stratum_init(const char* pool_url, uint16_t port, const char* wallet_address, 
                  const char* worker_name = nullptr, const char* password = nullptr);

// Connetti al pool
bool stratum_connect();

// Disconnetti dal pool
void stratum_disconnect();

// Controlla se connesso
bool stratum_is_connected();

// Loop principale - chiamare regolarmente per gestire messaggi
void stratum_loop();

// Invia una share al pool
bool stratum_submit_share(const char* job_id, const char* extranonce2, 
                         const char* ntime, const char* nonce);

// Imposta callback per nuovi job
void stratum_set_job_callback(stratum_job_callback_t callback);

// Ottieni difficoltà corrente
uint32_t stratum_get_difficulty();

// Ottieni job corrente
stratum_job_t stratum_get_current_job();

#endif // STRATUM_CLIENT_H
