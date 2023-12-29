/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include "drive/Session.h"

#include <boost/asio/io_context.hpp>

namespace sirius::drive {

class BackgroundExecutor
{
    boost::asio::io_context m_context;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> m_work;
    std::thread m_thread;

public:
    BackgroundExecutor()
      :
        m_context(),
        m_work(boost::asio::make_work_guard(m_context)),
        m_thread( std::thread( [this]
                              {
                                __LOG( "BackgroundExecutor started: ")
                                m_context.run();
                                __LOG( "BackgroundExecutor ended: ")
                                }))
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

    void execute( const std::function<void()>& task )
    {
        boost::asio::post(m_context, [=] {
            task();
        });
    }
};

}
