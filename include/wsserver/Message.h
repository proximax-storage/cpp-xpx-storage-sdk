#ifndef MESSAGE_H
#define MESSAGE_H

#include <iostream>
#include <openssl/hmac.h>
#include <cstring>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#include "EncryptDecrypt.h"
#include "drive/log.h"
#include "crypto/Signer.h"
#include "utils/HexParser.h"

std::string jsonToString(std::shared_ptr<boost::property_tree::ptree> data)
{
    std::stringstream stream;
    write_json(stream, *data);
    return stream.str();
}

// compute MD5 HMAC of given data using the provided key
std::string getMD5HMAC(const std::string &data, const std::string &key)
{
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hashLength;

    HMAC(EVP_md5(), key.c_str(), key.length(), (const unsigned char *)data.c_str(), data.length(), hash, &hashLength);

    char md5HMACString[EVP_MAX_MD_SIZE * 2 + 1];
    for (unsigned int i = 0; i < hashLength; i++)
	{
        sprintf(&md5HMACString[i * 2], "%02x", (unsigned int)hash[i]);
    }

    return {md5HMACString };
}

/*
    Creates a JSON with the format
    {
        data: ENCRYPTED MESSAGE using AES256
        HMAC: MD5 HMAC of the ENCRYPTED MESSAGE
    }
    as a string to be sent to client
*/

std::string encodeMessage(std::shared_ptr<boost::property_tree::ptree> data, const std::string &key)
{
    const std::string encrypted = aes_encrypt(jsonToString(data), key);

    auto message = std::make_shared<boost::property_tree::ptree>();
    message->put("data", encrypted);
    message->put("hmac", getMD5HMAC(encrypted, key));

    return jsonToString(message);
}

/*
    Decrypts messages sent from client in the format
    {
        data: ENCRYPTED MESSAGE using AES256
        HMAC: MD5 HMAC of the ENCRYPTED MESSAGE
    }
*/
int decodeMessage(const std::string &bufferStr, std::shared_ptr<boost::property_tree::ptree> data, const std::string &key)
{
    try
	{
        boost::property_tree::ptree msg;
        std::stringstream iss(bufferStr);
        read_json(iss, msg);

        if (msg.get<std::string>("hmac") == getMD5HMAC(msg.get<std::string>("data"), key))
		{
            std::stringstream decryptedData(aes_decrypt(msg.get<std::string>("data"), key));
            read_json(decryptedData, *data);
        }

    } catch (const boost::property_tree::json_parser_error& e)
	{
		__LOG_WARN( "DecodeMessage: JSON parsing error: " << e.what() )
        return 0;
    } catch (const std::exception& e)
	{
		__LOG_WARN( "DecodeMessage: An unexpected error occurred: " << e.what() )
        return 0;
    } catch (...)
	{
		__LOG_WARN( "DecodeMessage: An unexpected and unknown error occurred." )
        return 0;
    }

    return 1;
}

void sign(const sirius::crypto::KeyPair& keyPair, const std::string& buffer, sirius::Signature& signature)
{
    std::vector<uint8_t> rawBuffer(buffer.begin(), buffer.end());
    //sirius::utils::ParseHexStringIntoContainer(buffer.c_str(), 64, rawBuffer);
    sirius::crypto::Sign(keyPair, { sirius::utils::RawBuffer{ rawBuffer } }, signature);
}

bool verify(const std::string& publicKey, const std::string& buffer, const sirius::Signature& signature)
{
    sirius::Key key;
    sirius::utils::ParseHexStringIntoContainer(publicKey.c_str(), publicKey.size(), key);

    std::vector<uint8_t> rawBuffer(buffer.begin(), buffer.end());
//    for (int i = 0; i < 64; i++)
//    {
//        rawSignature[i] = static_cast<uint8_t>(clientSignatureRaw[i]);
//    }
    //sirius::utils::ParseHexStringIntoContainer(buffer.c_str(), 64, rawBuffer);

    return sirius::crypto::Verify(key, { sirius::utils::RawBuffer{ rawBuffer } }, signature );
}

#endif // MESSAGE_H