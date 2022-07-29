/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include <memory>

namespace sirius::drive {

class Timer {

private:

    class TimerCallback : public std::enable_shared_from_this<TimerCallback>
    {

    private:

        boost::asio::high_resolution_timer m_timer;
        std::function<void()> m_callback;

    public:

        template<class ExecutionContext>
        TimerCallback( ExecutionContext& executionContext )
        : m_timer( executionContext )
        {}

        void run( int milliseconds,
                  std::function<void()>&& callback )
        {
            m_callback = std::move( callback );
            m_timer.expires_after( std::chrono::milliseconds( milliseconds ));
            m_timer.async_wait( [pThisWeak = weak_from_this()]( boost::system::error_code const& ec ) {
                if ( auto pThis = pThisWeak.lock(); pThis ) {
                    pThis->onTimeout( ec );
                }
            } );
        }

    private:

        void onTimeout( const boost::system::error_code& ec )
        {
            if ( ec )
            {
                return;
            }

            m_callback();
        }

    };

  std::shared_ptr<TimerCallback> m_callback;

public:

    Timer( const Timer& ) = delete;
    Timer& operator= (const Timer&) = delete;
    Timer( Timer&& ) = default;
    Timer& operator= ( Timer&& ) = default;

    template<class ExecutionContext>
    Timer( ExecutionContext& executionContext,
           int milliseconds,
           std::function<void()>&& callback )
           : m_callback( std::make_shared<TimerCallback>( executionContext ) )
    {
           m_callback->run( milliseconds, std::move( callback ));
    }

   Timer() = default;

   operator bool() const
   {
       return m_callback.operator bool();
   }

    void cancel() {
        m_callback.reset();
    }
};

}