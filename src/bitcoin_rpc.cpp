#include "bitcoin_rpc.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <base64.h>

// Configurazione del nodo Bitcoin
static BitcoinNodeConfig nodeConfig = {0};
static bool isInitialized = false;

// Nodi pubblici Bitcoin (mainnet e testnet)
// NOTA: Per mining reale serve un nodo locale completo!
// Questi sono solo per demo/test
const char* PUBLIC_NODES[] = {
    "https://bitcoin.publicnode.com",           // Mainnet
    "https://testnet.bitcoin.publicnode.com"    // Testnet
};

// Inizializza la connessione al nodo Bitcoin
bool bitcoin_rpc_init(const char* host, uint16_t port, const char* user, const char* pass)
{
    if(strlen(host) >= sizeof(nodeConfig.host)) {
        Serial.println("❌ Host troppo lungo!");
        return false;
    }
    
    strcpy(nodeConfig.host, host);
    nodeConfig.port = port;
    
    if(user) strcpy(nodeConfig.username, user);
    if(pass) strcpy(nodeConfig.password, pass);
    
    isInitialized = true;
    
    Serial.println("╔════════════════════════════════════════════════════════╗");
    Serial.println("║        BITCOIN RPC CLIENT INITIALIZED                 ║");
    Serial.println("╚════════════════════════════════════════════════════════╝");
    Serial.printf("🌐 Nodo: %s:%d\n", nodeConfig.host, nodeConfig.port);
    Serial.println();
    
    return true;
}

// Esegue una chiamata RPC al nodo Bitcoin
bool bitcoin_rpc_call(const char* method, const char* params, JsonDocument& response)
{
    if(!isInitialized) {
        Serial.println("❌ RPC non inizializzato!");
        return false;
    }
    
    if(WiFi.status() != WL_CONNECTED) {
        Serial.println("❌ WiFi non connesso!");
        return false;
    }
    
    HTTPClient http;
    
    // Costruisci URL
    char url[256];
    if(nodeConfig.port == 443 || strstr(nodeConfig.host, "https://")) {
        snprintf(url, sizeof(url), "%s", nodeConfig.host);
    } else {
        snprintf(url, sizeof(url), "http://%s:%d", nodeConfig.host, nodeConfig.port);
    }
    
    Serial.printf("📡 Chiamata RPC: %s\n", method);
    
    http.begin(url);
    http.setTimeout(15000); // 15 secondi timeout
    
    // Header per autenticazione Basic
    if(strlen(nodeConfig.username) > 0) {
        String auth = String(nodeConfig.username) + ":" + String(nodeConfig.password);
        String authEncoded = base64::encode(auth);
        http.addHeader("Authorization", "Basic " + authEncoded);
    }
    
    http.addHeader("Content-Type", "application/json");
    
    // Costruisci payload JSON-RPC
    JsonDocument requestDoc;
    requestDoc["jsonrpc"] = "1.0";
    requestDoc["id"] = "esp32";
    requestDoc["method"] = method;
    
    // Aggiungi parametri se presenti (come array vuoto se non ci sono parametri)
    JsonArray paramsArray = requestDoc["params"].to<JsonArray>();
    
    String requestBody;
    serializeJson(requestDoc, requestBody);
    
    // Esegui richiesta POST
    int httpCode = http.POST(requestBody);
    
    if(httpCode > 0) {
        if(httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            
            // Parse risposta
            DeserializationError error = deserializeJson(response, payload);
            
            if(error) {
                Serial.printf("❌ Errore parsing JSON: %s\n", error.c_str());
                http.end();
                return false;
            }
            
            // Verifica errori RPC
            if(!response["error"].isNull()) {
                Serial.println("❌ Errore RPC:");
                serializeJsonPretty(response["error"], Serial);
                Serial.println();
                http.end();
                return false;
            }
            
            Serial.println("✅ Risposta ricevuta");
            http.end();
            return true;
            
        } else {
            Serial.printf("❌ HTTP error: %d\n", httpCode);
        }
    } else {
        Serial.printf("❌ Errore connessione: %s\n", http.errorToString(httpCode).c_str());
    }
    
    http.end();
    return false;
}

// Ottiene il block template dal nodo Bitcoin
bool bitcoin_rpc_get_block_template(BitcoinBlockTemplate* block_template)
{
    if(!block_template) return false;
    
    Serial.println();
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    Serial.println("📦 Recupero Block Template dalla blockchain...");
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    
    JsonDocument response;
    
    // Parametri per getblocktemplate
    const char* params = "[{\"rules\": [\"segwit\"]}]";
    
    if(!bitcoin_rpc_call("getblocktemplate", params, response)) {
        Serial.println("❌ Impossibile ottenere block template!");
        Serial.println("💡 Suggerimenti:");
        Serial.println("   - Verifica che il nodo sia sincronizzato");
        Serial.println("   - Controlla credenziali RPC");
        Serial.println("   - Assicurati che il nodo accetti getblocktemplate");
        return false;
    }
    
    JsonObject result = response["result"];
    
    // Estrai dati dal template
    block_template->version = result["version"] | 0;
    
    const char* prevhash = result["previousblockhash"] | "";
    strncpy(block_template->previousblockhash, prevhash, 64);
    block_template->previousblockhash[64] = '\0';
    
    block_template->curtime = result["curtime"] | 0;
    block_template->height = result["height"] | 0;
    
    // Bits (difficulty target in compact format)
    const char* bitsStr = result["bits"] | "1d00ffff";
    block_template->bits = strtoul(bitsStr, NULL, 16);
    
    // Conta transazioni
    JsonArray transactions = result["transactions"];
    block_template->transactions_count = transactions.size();
    
    // Per ora usiamo un merkle root semplificato
    // In un vero miner dovresti calcolare il merkle tree da tutte le transazioni
    snprintf(block_template->merkleroot, 65, "0000000000000000000000000000000000000000000000000000000000000000");
    
    block_template->valid = true;
    
    // Stampa info
    Serial.println();
    Serial.println("╔════════════════════════════════════════════════════════╗");
    Serial.println("║           📦 BLOCK TEMPLATE RICEVUTO                  ║");
    Serial.println("╚════════════════════════════════════════════════════════╝");
    Serial.printf("🔢 Altezza blocco: %u\n", block_template->height);
    Serial.printf("📅 Timestamp: %u\n", block_template->curtime);
    Serial.printf("🎯 Difficulty bits: 0x%08x\n", block_template->bits);
    Serial.printf("📝 Transazioni: %d\n", block_template->transactions_count);
    Serial.printf("🔗 Hash precedente:\n   %s\n", block_template->previousblockhash);
    Serial.println();
    Serial.println("⚠️  NOTA IMPORTANTE:");
    Serial.println("   La difficoltà REALE di Bitcoin è ENORME!");
    Serial.println("   Mining con ESP32 è solo EDUCATIVO.");
    Serial.println("   Probabilità di trovare blocco: ~0%");
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    Serial.println();
    
    return true;
}

// Ottiene info sulla blockchain
bool bitcoin_rpc_get_blockchain_info(uint32_t* block_height, char* chain)
{
    Serial.println("📊 Recupero info blockchain...");
    
    JsonDocument response;
    
    if(!bitcoin_rpc_call("getblockchaininfo", NULL, response)) {
        return false;
    }
    
    JsonObject result = response["result"];
    
    if(block_height) {
        *block_height = result["blocks"] | 0;
    }
    
    if(chain) {
        const char* chainName = result["chain"] | "unknown";
        strcpy(chain, chainName);
    }
    
    Serial.println("✅ Info blockchain ricevute");
    Serial.printf("   Chain: %s\n", chain ? chain : "N/A");
    Serial.printf("   Blocks: %u\n", block_height ? *block_height : 0);
    Serial.println();
    
    return true;
}

// Sottomette un blocco trovato
bool bitcoin_rpc_submit_block(const char* block_hex)
{
    if(!block_hex) return false;
    
    Serial.println();
    Serial.println("╔════════════════════════════════════════════════════════╗");
    Serial.println("║           🚀 SOTTOMISSIONE BLOCCO                     ║");
    Serial.println("╚════════════════════════════════════════════════════════╝");
    Serial.println("📤 Inviando blocco al nodo...");
    
    JsonDocument response;
    
    char params[512];
    snprintf(params, sizeof(params), "[\"%s\"]", block_hex);
    
    if(!bitcoin_rpc_call("submitblock", params, response)) {
        Serial.println("❌ Errore sottomissione blocco!");
        return false;
    }
    
    // Se result è null, il blocco è stato accettato!
    if(response["result"].isNull()) {
        Serial.println();
        Serial.println("╔════════════════════════════════════════════════════════╗");
        Serial.println("║           🎉 BLOCCO ACCETTATO! 🎉                     ║");
        Serial.println("╚════════════════════════════════════════════════════════╝");
        Serial.println("🏆 Congratulazioni! Hai minato un blocco Bitcoin!");
        Serial.println("   (Se sei in testnet o con difficoltà ridotta)");
        Serial.println();
        return true;
    } else {
        Serial.println("❌ Blocco rifiutato:");
        serializeJsonPretty(response["result"], Serial);
        Serial.println();
        return false;
    }
}

// Test connessione al nodo
bool bitcoin_rpc_test_connection(void)
{
    Serial.println();
    Serial.println("🔍 Test connessione al nodo Bitcoin...");
    
    char chain[32] = {0};
    uint32_t height = 0;
    
    if(bitcoin_rpc_get_blockchain_info(&height, chain)) {
        Serial.println("✅ Connessione al nodo Bitcoin riuscita!");
        Serial.printf("   Network: %s\n", chain);
        Serial.printf("   Altezza blockchain: %u\n", height);
        Serial.println();
        return true;
    } else {
        Serial.println("❌ Impossibile connettersi al nodo!");
        Serial.println();
        Serial.println("💡 Per mining SOLO hai bisogno di:");
        Serial.println("   1. Un nodo Bitcoin completo locale (Bitcoin Core)");
        Serial.println("   2. RPC abilitato in bitcoin.conf:");
        Serial.println("      server=1");
        Serial.println("      rpcuser=tuouser");
        Serial.println("      rpcpassword=tuapassword");
        Serial.println("      rpcallowip=192.168.x.x/24");
        Serial.println();
        Serial.println("📚 Alternative per testing:");
        Serial.println("   - Bitcoin Testnet (monete gratuite)");
        Serial.println("   - Bitcoin Regtest (rete locale)");
        Serial.println("   - Mining pool (più realistico)");
        Serial.println();
        return false;
    }
}
