cmake_minimum_required(VERSION 3.9)

find_package(OpenSSL REQUIRED)

storage_sdk_library_target(messenger-server)

foreach (_service messengerServer)

    # Proto file
    get_filename_component(${_service}_proto "../protobuf/messenger/${_service}.proto" ABSOLUTE)
    get_filename_component(${_service}_proto_path "${${_service}_proto}" PATH)

    # Generated sources
    set(${_service}_proto_srcs "${CMAKE_CURRENT_BINARY_DIR}/${_service}.pb.cc")
    set(${_service}_proto_hdrs "${CMAKE_CURRENT_BINARY_DIR}/${_service}.pb.h")
    set(${_service}_grpc_srcs "${CMAKE_CURRENT_BINARY_DIR}/${_service}.grpc.pb.cc")
    set(${_service}_grpc_hdrs "${CMAKE_CURRENT_BINARY_DIR}/${_service}.grpc.pb.h")
    add_custom_command(
            OUTPUT "${${_service}_proto_srcs}" "${${_service}_proto_hdrs}" "${${_service}_grpc_srcs}" "${${_service}_grpc_hdrs}"
            COMMAND ${_PROTOBUF_PROTOC}
            ARGS --grpc_out "${CMAKE_CURRENT_BINARY_DIR}"
            --cpp_out "${CMAKE_CURRENT_BINARY_DIR}"
            -I "${${_service}_proto_path}"
            --plugin=protoc-gen-grpc="${_GRPC_CPP_PLUGIN_EXECUTABLE}"
            "${${_service}_proto}"
            DEPENDS "${${_service}_proto}")

    # Include generated *.pb.h files
    target_include_directories(messenger-server PUBLIC "${CMAKE_CURRENT_BINARY_DIR}")

    # vm_client_grpc_proto
    add_library(${_service}_grpc_proto
            ${${_service}_grpc_srcs}
            ${${_service}_grpc_hdrs}
            ${${_service}_proto_srcs}
            ${${_service}_proto_hdrs})
    target_link_libraries(${_service}_grpc_proto
            ${_REFLECTION}
            ${_GRPC_GRPCPP}
            ${_PROTOBUF_LIBPROTOBUF})

    target_link_libraries(messenger-server
            ${_service}_grpc_proto
            ${_REFLECTION}
            ${_GRPC_GRPCPP}
            ${_PROTOBUF_LIBPROTOBUF})
endforeach ()

target_include_directories(messenger-server PUBLIC ../include ../../include)
target_include_directories(messenger-server PUBLIC SYSTEM ${Boost_INCLUDE_DIR})

target_link_libraries(messenger-server utils OpenSSL::SSL)
