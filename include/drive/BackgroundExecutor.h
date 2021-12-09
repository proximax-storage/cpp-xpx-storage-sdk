/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include "drive/Session.h"

namespace sirius::drive {

class BackgroundExecutor
{
public:
    BackgroundExecutor()
      :
        m_work(m_context),
        m_thread( std::thread( [this] { m_context.run(); } ))
    {
    }

    ~BackgroundExecutor()
    {
        stop();
    }
    
    void stop()
    {
        m_context.stop();
        if ( m_thread.joinable() )
        {
            m_thread.join();
        }
    }

    void run( const std::function<void()>& task )
    {
        m_context.post( [=] { task(); });
    }

private:
    boost::asio::io_context         m_context;
    boost::asio::io_context::work   m_work;
    std::thread                     m_thread;
};

}
