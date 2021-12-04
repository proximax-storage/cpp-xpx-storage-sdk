//#include "drive/BackgroundExecutor.h"
//
//namespace sirius::drive {
//
//    BackgroundExecutor::BackgroundExecutor(std::shared_ptr<Session> session)
//            : m_session(session),
//              m_work(m_context),
//              m_thread(std::thread([this]
//               {
//                   m_context.run();
//               }))
//    {
//    }
//
//    void BackgroundExecutor::run(const std::function<void()>& task, const std::function<void()>& callBack)
//    {
//        m_context.post([=, this]
//        {
//            task();
//            m_session->lt_session().get_context().post(callBack);
//        });
//    }
//}
