/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/



#include "MessageReader.h"

namespace sirius::drive::messenger {

void MessageReader::read() {

}

void MessageReader::onRead( const std::optional<OutputMessage>& message ) {
    if (message && m_context->m_serviceIsActive) {
        m_sender.sendMessage(*message);
        read();
    }
}

};

}