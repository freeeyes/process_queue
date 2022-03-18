Table of Contents
=================

 - [Overview](#overview)
 - [Build and Install](#build-and-install)
 - [Documentation](#documentation)

Overview
========
This is an inter-process message queue, limited to native communication, supports cross-platform(windows && linux), head only.  

Build and Install
=================
head only.  
you can include shm_queue.hpp

Documentation
=============
create_instance(key_id, message_size, message_count)  
key_id:you queue id.  
message_size:slot size
message_count:slot count

producer:  
```c++
#include "shm_queue/shm_queue.hpp"

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

    char buffer[10] = { '\0' };
#if PSS_PLATFORM == PLATFORM_WIN
    sprintf_s(buffer, 10, "freeeyes");
#else
    sprintf(buffer, "freeeyes");
#endif

    if (!message_queue->set_proc_message(buffer, strlen(buffer)))
    {
        std::cout << "[error]" << message_queue->get_error() << std::endl;
    }	

    getchar();
}
```  

consumer:  
```c++
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

    message_queue->recv_message(do_logic_message);

    getchar();
}
```  