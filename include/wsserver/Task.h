/*
    Task codes used in each json when sending & receiving message or data via websocket server
*/
enum Task {
    DOWNLOAD_START,
    DOWNLOAD_INFO,
    DOWNLOAD_DATA,
    DOWNLOAD_ACK,
    DOWNLOAD_FAILURE,
    UPLOAD_START,
    UPLOAD_DATA,
    UPLOAD_ACK,
    UPLOAD_FAILURE,
    DELETE_DATA,
    DELETE_DATA_ACK,
    DELETE_DATA_FAILURE,
    MESSAGE,
    MESSAGE_ACK,
    CLOSE,
    CLOSE_ACK
};