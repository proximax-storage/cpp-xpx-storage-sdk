/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include <memory>

#include "drive/Timer.h"

namespace sirius::drive {

class DefaultTimer :
        public Timer,
        public std::enable_shared_from_this<DefaultTimer> {

private:

    boost::asio::high_resolution_timer  m_timer;
    std::function<void()>               m_callback;
    bool                                m_cancelled = false;

public:

    template<class ExecutionContext>
    DefaultTimer( ExecutionContext& executionContext,
           int milliseconds,
           std::function<void()> callback )
            : m_timer( executionContext )
            , m_callback( std::move( callback ) )
    {
        m_timer.expires_after( std::chrono::milliseconds( milliseconds ) );
        m_timer.async_wait( [pThisWeak = weak_from_this()]( boost::system::error_code const& ec ) {
            if ( auto pThis = pThisWeak.lock(); pThis ) {
                pThis->onTimeout( ec );
            }
        } );
    }

    void cancel() override {
        m_cancelled = true;
    }

private:

    void onTimeout( const boost::system::error_code& ec ) {
        if ( m_cancelled ) {
            return;
        }

        if ( ec ) {
            return;
        }

    }

};

}