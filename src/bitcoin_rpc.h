#ifndef BITCOIN_RPC_H
#define BITCOIN_RPC_H

#include <Arduino.h>

// Struttura per il block template ricevuto dal nodo Bitcoin
struct BitcoinBlockTemplate {
    uint32_t version;
    char previousblockhash[65];
    char merkleroot[65];
    uint32_t curtime;
    uint32_t bits;
    uint32_t height;
    int transactions_count;
    bool valid;
};

// Configurazione nodo Bitcoin
struct BitcoinNodeConfig {
    char host[128];
    uint16_t port;
    char username[64];
    char password[64];
};

// Funzioni per comunicare con nodo Bitcoin
bool bitcoin_rpc_init(const char* host, uint16_t port, const char* user, const char* pass);
bool bitcoin_rpc_get_block_template(BitcoinBlockTemplate* block_template);
bool bitcoin_rpc_get_blockchain_info(uint32_t* block_height, char* chain);
bool bitcoin_rpc_submit_block(const char* block_hex);

// Test connessione
bool bitcoin_rpc_test_connection(void);

#endif // BITCOIN_RPC_H
