#ifndef ENCRYPT_DECRYPT_H
#define ENCRYPT_DECRYPT_H

#include <iostream>
#include <openssl/aes.h>
#include <openssl/rand.h>
#include <openssl/evp.h>
#include <cstring>

#include "Base64.h"

#define AES_BLOCK_SIZE 16

// Function to perform AES encryption
std::string aes_encrypt(const std::string &plaintext, const std::string &key)
{
    if (key.size() != 32)
	{
        std::cerr << "Key length must be 32 bytes for AES256\n";
        return "";
    }

    // Generate IV
    unsigned char iv[AES_BLOCK_SIZE];
    if (RAND_bytes(iv, AES_BLOCK_SIZE) == 0)
	{
        std::cerr << "Error generating IV\n";
        return "";
    }

    // Create and initialize the cipher context
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
	{
        std::cerr << "Error creating context\n";
        return "";
    }

    // Initialize the encryption operation with AES CBC mode and PKCS7 padding
    if (!EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, reinterpret_cast<const unsigned char *>(key.c_str()), iv))
	{
        std::cerr << "Error initializing encryption operation\n";
        EVP_CIPHER_CTX_free(ctx);
        return "";
    }

    // Provide the plaintext to be encrypted, and obtain the ciphertext output
    int ciphertext_len;
    int len;
    unsigned char ciphertext[plaintext.size() + AES_BLOCK_SIZE];
    if (!EVP_EncryptUpdate(ctx, ciphertext, &len, reinterpret_cast<const unsigned char *>(plaintext.c_str()), plaintext.size()))
	{
        std::cerr << "Error encrypting\n";
        EVP_CIPHER_CTX_free(ctx);
        return "";
    }

    ciphertext_len = len;

    // Finalize the encryption
    if (!EVP_EncryptFinal_ex(ctx, ciphertext + len, &len))
	{
        std::cerr << "Error finalizing encryption\n";
        EVP_CIPHER_CTX_free(ctx);
        return "";
    }

    ciphertext_len += len;

    // Cleanup
    EVP_CIPHER_CTX_free(ctx);

    // Return IV concatenated with ciphertext
    return base64_encode(std::string(reinterpret_cast<const char *>(iv), AES_BLOCK_SIZE) + std::string(reinterpret_cast<const char *>(ciphertext), ciphertext_len));
}

// Function to perform AES decryption
std::string aes_decrypt(const std::string &ciphertextB64, const std::string &key)
{
    const std::string ciphertext = base64_decode(ciphertextB64);
    if (key.size() != 32)
	{
        std::cerr << "Key length must be 32 bytes for AES256\n";
        return "";
    }

    // Extract IV from ciphertext
    unsigned char iv[AES_BLOCK_SIZE];
    memcpy(iv, ciphertext.c_str(), AES_BLOCK_SIZE);

    // Create and initialize the cipher context
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
	{
        std::cerr << "Error creating context\n";
        return "";
    }

    // Initialize the decryption operation with AES CBC mode and PKCS7 padding
    if (!EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, reinterpret_cast<const unsigned char *>(key.c_str()), iv))
	{
        std::cerr << "Error initializing decryption operation\n";
        EVP_CIPHER_CTX_free(ctx);
        return "";
    }

    // Provide the ciphertext to be decrypted, and obtain the plaintext output
    int plaintext_len;
    int len;
    unsigned char plaintext[ciphertext.size() - AES_BLOCK_SIZE];
    if (!EVP_DecryptUpdate(ctx, plaintext, &len, reinterpret_cast<const unsigned char *>(ciphertext.c_str() + AES_BLOCK_SIZE), ciphertext.size() - AES_BLOCK_SIZE)) {
        std::cerr << "Error decrypting\n";
        EVP_CIPHER_CTX_free(ctx);
        return "";
    }

    plaintext_len = len;

    // Finalize the decryption
    if (!EVP_DecryptFinal_ex(ctx, plaintext + len, &len)) {
        std::cerr << "Error finalizing decryption\n";
        EVP_CIPHER_CTX_free(ctx);
        return "";
    }

    plaintext_len += len;

    // Cleanup
    EVP_CIPHER_CTX_free(ctx);

    // Return the decrypted plaintext
    return { reinterpret_cast<const char *>(plaintext), static_cast<size_t>(plaintext_len) };
}

#endif // ENCRYPT_DECRYPT_H