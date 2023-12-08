#include <messenger-server/MessengerServerBuilder.h>

#include <thread>

using namespace sirius::drive::messenger;
using namespace sirius::drive;

class MessengerMock
        : public Messenger, public IOContextProvider
{

private:

    boost::asio::io_context m_context;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> m_work;
    std::thread m_thread;

    std::map<std::string, std::shared_ptr<MessageSubscriber>> m_subscribers;

public:

    MessengerMock()
            : m_context()
            , m_work( boost::asio::make_work_guard( m_context ))
            , m_thread( std::thread( [this] { m_context.run(); } ))
    {}

    ~MessengerMock() {
        m_work.reset();
        m_context.stop();
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

public:
    boost::asio::io_context& getContext() override
    {
        return m_context;
    }

    void sendMessage( const OutputMessage& message ) override
    {
        auto it = m_subscribers.find( message.m_tag );
        if ( it != m_subscribers.end())
        {
            it->second->onMessageReceived( {message.m_tag, message.m_data} );
        }
    }

    void subscribe( const std::string& tag, std::shared_ptr<MessageSubscriber> subscriber ) override
    {
        m_subscribers[tag] = std::move(subscriber);
    }
};

int main()
{

    auto messenger = std::make_shared<MessengerMock>();

    auto service = MessengerServerBuilder().build( messenger );

    grpc::ServerBuilder builder;

    const std::string address = "127.0.0.1:5052";

    builder.AddListeningPort(address, grpc::InsecureServerCredentials());

    std::cout << "Listening on " << address << std::endl;

    service->registerService(builder);

    auto server = builder.BuildAndStart();

    service->run(messenger);

    std::cout << "Press Enter To Stop" << std::endl;
    std::cin.get();

    server->Shutdown();
    service.reset();

    return 0;
}