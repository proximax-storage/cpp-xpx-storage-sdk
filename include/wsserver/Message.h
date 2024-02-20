#ifndef MESSAGE_H
#define MESSAGE_H

#include <iostream>
#include <openssl/hmac.h>
#include <cstring>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#include "EncryptDecrypt.h"

namespace pt = boost::property_tree;    // from <boost/property_tree>

// compute MD5 HMAC of given data using the provided key
std::string getMD5HMAC(const std::string &data, const std::string &key) {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hashLength;

    HMAC(EVP_md5(), key.c_str(), key.length(), (const unsigned char *)data.c_str(), data.length(), hash, &hashLength);

    char md5HMACString[EVP_MAX_MD_SIZE * 2 + 1];
    for (unsigned int i = 0; i < hashLength; i++) {
        sprintf(&md5HMACString[i * 2], "%02x", (unsigned int)hash[i]);
    }

    return std::string(md5HMACString);
}

/*
    Creates a JSON with the format
    {
        data: ENCRYPTED MESSAGE using AES256
        HMAC: MD5 HMAC of the ENCRYPTED MESSAGE
    }
    as a string to be sent to client
*/

std::string encodeMessage(pt::ptree *data, const std::string &key) {
    std::ostringstream oss;
    write_json(oss, *data, false);

    std::string encrypted = aes_encrypt(oss.str(), key);

    pt::ptree msg;
    msg.put("data", encrypted);
    msg.put("HMAC", getMD5HMAC(encrypted, key));

    std::ostringstream oss2;
    write_json(oss2, msg, false);

    return oss2.str();
}

/*
    Decrypts messages sent from client in the format
    {
        data: ENCRYPTED MESSAGE using AES256
        HMAC: MD5 HMAC of the ENCRYPTED MESSAGE
    }
*/
int decodeMessage(const std::string &bufferStr, pt::ptree *data, const std::string &key) {
    try {
        pt::ptree msg;
        std::istringstream iss(bufferStr);
        read_json(iss, msg);

        if (msg.get<std::string>("HMAC") == getMD5HMAC(msg.get<std::string>("data"), key)) {
            std::istringstream iss(aes_decrypt(msg.get<std::string>("data"), key));
            read_json(iss, *data);
        }

    } catch (const boost::property_tree::json_parser_error& e) {
        std::cerr << "JSON parsing error: " << e.what() << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "An unexpected error occurred: " << e.what() << std::endl;
        return 0;
    } catch (...) {
        std::cerr << "An unexpected and unknown error occurred." << std::endl;
        return 0;
    }

    return 1;
}

#endif // MESSAGE_H