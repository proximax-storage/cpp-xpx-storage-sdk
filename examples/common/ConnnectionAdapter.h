/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once
#include "connection/NodeConnector.h"
#include "ionet/Node.h"

namespace sirius { namespace sdk { namespace examples {

    std::shared_ptr<connection::NodeConnector> ConnectNode(const ionet::Node& node,
                                                      const connection::NodeConnector::ConnectCallback& callback);

}}}