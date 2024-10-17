#ifndef ENCRYPT_DECRYPT_H
#define ENCRYPT_DECRYPT_H

#include <iostream>
#include <openssl/aes.h>
#include <openssl/rand.h>
#include <openssl/evp.h>
#include <cstring>
#include <openssl/err.h>
#include <openssl/conf.h>

#include "Base64.h"
#include "drive/log.h"

#define AES_GCM_IV_SIZE 12
#define AES_GCM_TAG_SIZE 16

// TODO: print openssl errors
namespace sirius::wsserver
{

struct EncryptionResult
{
    std::string cipherData;
    std::string iv;
    std::string tag;
};

std::string getErrorMessage();
EncryptionResult encrypt(const std::string& key, const std::string& data);
std::string decrypt(const std::string& key, const std::string& data, const std::string& initializedVector, const std::string& tag);
void printBytes(const std::string& prefix, const std::string& data);

}

#endif // ENCRYPT_DECRYPT_H