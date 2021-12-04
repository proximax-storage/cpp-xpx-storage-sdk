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
    explicit BackgroundExecutor(std::shared_ptr<Session> session)
            : m_session(session),
              m_work(m_context),
              m_thread(std::thread([this]
               {
                   m_context.run();
               }))
    {
    }

    ~BackgroundExecutor()
    {
        m_context.stop();
    }

    void run(const std::function<void()>& task, const std::function<void()>& callBack)
    {
        m_context.post( [=,this]
        {
            task();
            m_session->lt_session().get_context().post(callBack);
        });
    }

private:
    std::shared_ptr<Session>        m_session;
    boost::asio::io_context         m_context;
    boost::asio::io_context::work   m_work;
    std::thread                     m_thread;
};

}
