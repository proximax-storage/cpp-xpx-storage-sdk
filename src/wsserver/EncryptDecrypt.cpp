#include "wsserver/EncryptDecrypt.h"

namespace sirius::wsserver
{

std::string getErrorMessage()
{
    char buffer[256];
    unsigned long errorCode;
    std::string result;

    while ((errorCode = ERR_get_error()) != 0)
    {
        ERR_error_string_n(errorCode, buffer, sizeof(buffer));
        if (!result.empty())
        {
            result += "\n";
        }

        result += buffer;
    }

    return result;
}

EncryptionResult encrypt(const std::string& key, const std::string& data)
{
    if (key.size() != 32)
    {
        __LOG_WARN( "encrypt: Key length must be 32 bytes for AES256: " << key )
        return {};
    }

    auto ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
    {
        __LOG_WARN( "encrypt: Error creating context" )
        return {};
    }

    if (1 != EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr))
    {
        __LOG_WARN( "encrypt: init aes 256 gcm error: " << getErrorMessage() )
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    unsigned char iv[AES_GCM_IV_SIZE];
    if (RAND_bytes(iv, AES_GCM_IV_SIZE) == 0)
    {
        __LOG_WARN( "encrypt: Error generating IV" )
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    auto keyRaw = reinterpret_cast<const unsigned char *>(key.c_str());
    if (1 != EVP_EncryptInit_ex(ctx, nullptr, nullptr, keyRaw, iv))
    {
        __LOG_WARN( "encrypt: init ex iv error: " << getErrorMessage() )
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    int length;
    int cipherDataLength;

    std::vector<uint8_t> cipherData(data.size() + AES_GCM_TAG_SIZE, 0);
    if (1 != EVP_EncryptUpdate(ctx, cipherData.data(), &length, reinterpret_cast<const unsigned char *>(data.c_str()), static_cast<int>(data.size())))
    {
        __LOG_WARN( "encrypt: update cipher data error: " << getErrorMessage() )
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    cipherDataLength = length;

    if (1 != EVP_EncryptFinal_ex(ctx, cipherData.data() + length, &length))
    {
        __LOG_WARN( "encrypt: final ex error: " << getErrorMessage() )
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    cipherDataLength += length;

    unsigned char tag[AES_GCM_TAG_SIZE];
    if (1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, AES_GCM_TAG_SIZE, &tag))
    {
        __LOG_WARN( "encrypt: ctrl error: " << getErrorMessage() )
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    EVP_CIPHER_CTX_free(ctx);

    EncryptionResult result;
    result.cipherData = std::string(reinterpret_cast<const char *>(cipherData.data()), cipherDataLength);
    result.iv = std::string(reinterpret_cast<const char *>(iv), AES_GCM_IV_SIZE);
    result.tag = std::string(reinterpret_cast<const char *>(tag), AES_GCM_TAG_SIZE);

    return result;
}

std::string decrypt(const std::string& key, const std::string& data, const std::string& initializedVector, const std::string& tag)
{
    if (key.size() != 32)
    {
        __LOG_WARN( "decrypt: Key length must be 32 bytes: " << base64_encode(key) )
        return "";
    }

    if (initializedVector.size() != AES_GCM_IV_SIZE)
    {
        __LOG_WARN( "decrypt: IV length must be " << AES_GCM_IV_SIZE << " bytes: " << base64_encode(initializedVector) )
        return "";
    }

    if (tag.size() != AES_GCM_TAG_SIZE)
    {
        __LOG_WARN( "decrypt: TAG length must be " << AES_GCM_TAG_SIZE << " bytes: " << base64_encode(initializedVector) )
        return "";
    }

    auto ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
    {
        __LOG_WARN( "decrypt: Error creating context" )
        return "";
    }

    if (!EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr))
    {
        __LOG_WARN( "decrypt: init aes 256 gcm error: " << getErrorMessage() )
        EVP_CIPHER_CTX_free(ctx);
        return "";
    }

    auto keyRaw = reinterpret_cast<const unsigned char *>(key.c_str());
    const auto* iv = reinterpret_cast<const unsigned char*>(initializedVector.c_str());
    if (!EVP_DecryptInit_ex(ctx, nullptr, nullptr, keyRaw, iv))
    {
        __LOG_WARN( "decrypt: init aes 256 gcm iv error: " << getErrorMessage() )
        EVP_CIPHER_CTX_free(ctx);
        return "";
    }

    int length = 0;
    std::vector<uint8_t> decryptedData(data.size(), 0);
    if (!EVP_DecryptUpdate(ctx, decryptedData.data(), &length, reinterpret_cast<const unsigned char *>(data.c_str()), static_cast<int>(data.size())))
    {
        __LOG_WARN( "decrypt: update data cipher data error: " << getErrorMessage() )
        EVP_CIPHER_CTX_free(ctx);
        return "";
    }

    if (!EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, AES_GCM_TAG_SIZE, (uint8_t*)tag.c_str()))
    {
        __LOG_WARN( "decrypt: ctrl error: " << getErrorMessage() )
        EVP_CIPHER_CTX_free(ctx);
        return "";
    }

    int retCode = EVP_DecryptFinal_ex(ctx, decryptedData.data() + length, &length);
    if (retCode <= 0)
    {
        __LOG_WARN( "decrypt: final ex error: " << getErrorMessage() )
        EVP_CIPHER_CTX_free(ctx);
        return "";
    }

    EVP_CIPHER_CTX_free(ctx);

    return { decryptedData.begin(), decryptedData.begin() + static_cast<int>(data.size()) + length };
}

void printBytes(const std::string& prefix, const std::string& data)
{
    std::cout << prefix;
    for (int i = 0; i < static_cast<int>(data.size()); i++)
    {
        if (i == static_cast<int>(data.size()) -1 )
        {
            std::cout << static_cast<int>(static_cast<uint8_t>(data[i])) << std::endl;
        }
        else
        {
            std::cout << static_cast<int>(static_cast<uint8_t>(data[i])) << ",";
        }
    }
}

}