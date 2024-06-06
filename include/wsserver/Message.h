#ifndef MESSAGE_H
#define MESSAGE_H

#include <iostream>
#include <openssl/hmac.h>
#include <cstring>

#include <boost/exception/all.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#include "EncryptDecrypt.h"
#include "drive/log.h"
#include "crypto/Signer.h"
#include "utils/HexParser.h"

namespace sirius::wsserver {

std::string jsonToString(std::shared_ptr<boost::property_tree::ptree> data)
{
    std::stringstream stream;

    try
    {
        write_json(stream, *data);
    } catch (const boost::property_tree::json_parser_error& e)
    {
        __LOG_WARN( "jsonToString: JSON parsing error: " << e.what() )
        return "";
    }

    return stream.str();
}

std::shared_ptr<boost::property_tree::ptree> stringToJson(const std::string& json)
{
    std::stringstream stream(json);
    auto pTree = std::make_shared<boost::property_tree::ptree>();

    try
    {
        boost::property_tree::read_json(stream, *pTree);
    } catch (const boost::property_tree::json_parser_error& e)
    {
        __LOG_WARN( "stringToJson: JSON parsing error: " << e.what() )
    }

    return pTree;
}

//std::string encryptMessage(std::shared_ptr<boost::property_tree::ptree> data, const std::string &key)
//{
//    try
//    {
//        auto task = data->get_optional<int>("payload.task");
//        if (!task.has_value())
//        {
//            __LOG_WARN("encryptMessage: invalid task")
//            return "";
//        }
//
//        if (task.value() == Task::KEY_EX)
//        {
//            const auto sessionId = data->get_optional<std::string>("sessionId");
//            if (!sessionId.has_value() || sessionId->empty())
//            {
//                __LOG_WARN("encryptMessage: sessionId encryption error")
//                return "";
//            }
//
//            const EncryptionResult encryptedSessionId = encrypt(key, sessionId.value());
//            if (encryptedSessionId.cipherData.empty())
//            {
//                __LOG_WARN("encryptMessage: encryptedSessionId error")
//                return "";
//            }
//            else
//            {
//                data->put("payload.sessionId", encryptedSessionId.cipherData);
//            }
//
//            data->put("metadata.iv", encryptedSessionId.iv);
//            data->put("metadata.tag", encryptedSessionId.tag);
//        }
//        else
//        {
//            __LOG_WARN("encryptMessage: not implemented")
//            return "";
//        }
//    } catch (const boost::property_tree::ptree_bad_path& e)
//    {
//        __LOG_WARN("encryptMessage: json path not found: " << e.what())
//        return "";
//    } catch (const boost::property_tree::ptree_bad_data& e)
//    {
//        __LOG_WARN("encryptMessage: json bad data: " << e.what())
//        return "";
//    } catch (const boost::property_tree::ptree_error& e)
//    {
//        __LOG_WARN("encryptMessage: json ptree error: " << e.what())
//        return "";
//    }
//
//    return jsonToString(data);
//}

//int decryptMessage(const std::string& buffer, std::shared_ptr<boost::property_tree::ptree> data, const std::string &key)
//{
//    try
//    {
//        auto pTree = stringToJson(buffer);
//
//        const auto task = pTree.get_optional<int>("payload.task");
//        if (task.has_value() && task.value() == Task::KEY_EX)
//        {
//
//        }
//        else
//        {
//            __LOG_WARN("decryptMessage: not implemented")
//        }
//
//        if (msg.get<std::string>("hmac") == getMD5HMAC(msg.get<std::string>("data"), key))
//        {
//            std::stringstream decryptedData(decrypt(msg.get<std::string>("data"), key));
//            read_json(decryptedData, *data);
//        }
//
//    } catch (const boost::property_tree::json_parser_error &e) {
//        __LOG_WARN("DecodeMessage: JSON parsing error: " << e.what())
//        return 1;
//    } catch (const std::exception &e) {
//        __LOG_WARN("DecodeMessage: An unexpected error occurred: " << e.what())
//        return 1;
//    } catch (...) {
//        __LOG_WARN("DecodeMessage: An unexpected and unknown error occurred.")
//        return 1;
//    }
//
//    return 0;
//}

void sign(const sirius::crypto::KeyPair &keyPair, const std::string &buffer, sirius::Signature &signature)
{
    std::vector<uint8_t> rawBuffer(buffer.begin(), buffer.end() );
    sirius::crypto::Sign(keyPair, { sirius::utils::RawBuffer{rawBuffer} }, signature);
}

bool verify(const std::string &publicKey, const std::string &buffer, const std::string &signature)
{
    bool result = false;
    try
    {
        sirius::Key key;
        sirius::utils::ParseHexStringIntoContainer(publicKey.c_str(), publicKey.size(), key);

        sirius::Signature s;
        sirius::utils::ParseHexStringIntoContainer(signature.c_str(), signature.size(), s);

        std::vector<uint8_t> rawBuffer(buffer.begin(), buffer.end());
        result = sirius::crypto::Verify(key, { sirius::utils::RawBuffer{rawBuffer } }, s);
    }
    catch (const boost::exception& e)
    {
        __LOG_WARN("verify exception: " << boost::diagnostic_information(e))
    }

    return result;
}
}

#endif // MESSAGE_H