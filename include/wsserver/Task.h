#ifndef TASK_H
#define TASK_H

enum Type
{
	// TODO: Use a status(success or failure) as a field of payload, not like type
    HANDSHAKE_REQUEST, // From client to server
    HANDSHAKE_RESPONSE, // From server to client

    CLIENT_UPLOAD_DATA_START_REQUEST, // From client to server
    SERVER_READY_RESPONSE, // From server to client

    CLIENT_UPLOAD_DATA_REQUEST, // From client
    SERVER_ACK, // send to client after handling

    FS_TREE_REQUEST, // From client
    FS_TREE_RESPONSE, // From server

	CLIENT_DOWNLOAD_FILES_REQUEST,
	CLIENT_DOWNLOAD_FILES_RESPONSE,
	CLIENT_DOWNLOAD_PIECE_RESPONSE,
	CLIENT_ACK, // send to server after handling

    CLIENT_DOWNLOAD_DATA_START,
    CLIENT_DOWNLOAD_DATA_INFO,
    CLIENT_DOWNLOAD_DATA,
    CLIENT_DOWNLOAD_DATA_ACK,
    CLIENT_DOWNLOAD_DATA_FAILURE,
    SERVER_DOWNLOAD_DATA_START,
    SERVER_DOWNLOAD_DATA,
    SERVER_DOWNLOAD_DATA_ACK,
    SERVER_DOWNLOAD_DATA_FAILURE,
    CLIENT_DELETE_DATA,
    SERVER_DELETE_DATA_ACK,
    SERVER_DELETE_DATA_FAILURE,
    MESSAGE,
    MESSAGE_ACK,
    CLOSE,
    CLOSE_ACK,
    FAILURE // From server to client. Internal server error. Look on the field 'code'.
};

enum ServerErrorCode
{
    UNEXPECTED_INTERNAL_ERROR,
    JSON_PARSER_ERROR,
    READING_MESSAGE_ERROR,
    RECEIVED_EMPTY_DATA,
    RECEIVED_INVALID_DATA,
    DECRYPTION_ERROR
};

#endif // TASK_H