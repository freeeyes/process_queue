// shm_queue_test.cpp : 实现共享内存队列
//

#include <iostream>
#include "shm_queue/shm_queue.hpp"

void do_logic_message(const char* message, size_t len)
{
    std::cout << "[do_logic_message]message=" << message << ",len(" << len << ")" << std::endl;
}

int main()
{
    auto message_queue =std::make_shared<shm_queue::CShm_message_queue>();
    if (false == message_queue->create_instance(11111, 100, 10))
    {
        std::cout << message_queue->get_error() << std::endl;
    }
    else
    {
        std::cout << "share memory success" << std::endl;
    }

    message_queue->set_close_function([](key_t key_id) {
        std::cout << "[queue close]" << key_id << std::endl;
        });

    message_queue->show_message_list();

    char buffer[10] = { '\0' };
#if PSS_PLATFORM == PLATFORM_WIN
    sprintf_s(buffer, 10, "freeeyes");
#else
    sprintf(buffer, "freeeyes");
#endif

    message_queue->recv_message(do_logic_message);

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    if (!message_queue->set_proc_message(buffer, strlen(buffer)))
    {
        std::cout << "[error]" << message_queue->get_error() << std::endl;
    }

    message_queue->close();

    std::cout << "close is ok" << std::endl;

    getchar();

    return 0;
}
