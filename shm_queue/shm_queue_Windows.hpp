#pragma once

#include "shm_common.hpp"

#if PSS_PLATFORM == PLATFORM_WIN
#include <vector>
#include <tchar.h>
#include <windows.h>

#define NAME_SIZE      200   //事件名称长度

#define Shm_id HANDLE

namespace shm_queue {
    enum class Shm_memory_state : unsigned char
    {
        SHM_INIT,     //第一次申请共享内存，初始化
        SHM_RESUME,   //共享内存已存在，恢复重新映射共享内存数据
    };

    enum class Shm_message_type : unsigned char
    {
        MESSAGE_IS_FULL,     //消息槽有未处理消息
        MESSAGE_IS_EMPTY,    //消息槽没有未处理消息
    };

    //共享内存头
    class CShm_head
    {
    public:
        Shm_message_type shm_message_ = Shm_message_type::MESSAGE_IS_EMPTY;
        char* message_begin_ = nullptr;
        char* message_end_ = nullptr;
        size_t message_max_size_ = 0;
        size_t message_curr_size_ = 0;
    };

    class CMessage_Queue_Windows : public CShm_queue_interface
    {
    public:
        ~CMessage_Queue_Windows()
        {
            destroy_share_memory();
        }

        bool set_proc_message(const char* message_text, size_t len) final
        {
            bool ret = false;

            if (message_size_ < len)
            {
                return ret;
            }

            //首先锁住进程所，让读取先不做操作
            ::WaitForSingleObject(process_mutext_, INFINITE);

            //寻找一个空余的共享内存卡槽，将数据插入进去
            for (auto& message : message_list_)
            {
                if (message->shm_message_ == Shm_message_type::MESSAGE_IS_EMPTY)
                {
                    ::memcpy_s(message->message_begin_, len, message_text, len);
                    message->message_curr_size_ = len;
                    message->shm_message_ = Shm_message_type::MESSAGE_IS_FULL;
                    ret = true;
                    break;
                }
            }

            //任务完成，放开进程锁
            ::ReleaseMutex(process_mutext_);

            //创建一个事件
            HANDLE send_event = ::CreateEvent(NULL, FALSE, FALSE, event_name_);
            if (send_event == nullptr)
            {
                //创建错误信息
                std::stringstream ss;
                ss << "[" << __FILE__ << ":" << __LINE__
                    << "] set_proc_message failed error:" << GetLastError();
                error_ = ss.str();
                return false;
            }
            ::SetEvent(send_event);
            ::CloseHandle(send_event);
            return ret;
        }

        //接收消息
        void recv_message(queue_recv_message_func fn_logic) final
        {
            //启动一个线程，设置接收位置
            recv_thread_is_run_ = true;
            tt_recv_ = std::thread([this, fn_logic]() {
                HANDLE recv_event = ::CreateEvent(NULL, FALSE, FALSE, event_name_);
                if (recv_event == nullptr)
                {
                    //创建错误信息
                    std::stringstream ss;
                    ss << "[" << __FILE__ << ":" << __LINE__
                        << "] recv_message failed error:" << GetLastError();
                    error_ = ss.str();
                }

                while (true)
                {
                    ::WaitForSingleObject(recv_event, INFINITE);  //收到信号，自动重置

                    //首先锁住进程所，让读取先不做操作
                    ::WaitForSingleObject(process_mutext_, INFINITE);

                    //寻找一个空余的共享内存卡槽，将数据插入进去
                    for (auto& message : message_list_)
                    {
                        if (message->shm_message_ == Shm_message_type::MESSAGE_IS_FULL)
                        {
                            fn_logic(message->message_begin_, message->message_curr_size_);
                            message->shm_message_ = Shm_message_type::MESSAGE_IS_EMPTY;
                        }
                    }

                    //任务完成，放开进程锁
                    ::ReleaseMutex(process_mutext_);

                    //如果是关闭事件，就退出当前线程
                    if (recv_thread_is_close_)
                    {
                        break;
                    }
                }

                //接收数据结束线程
                ::CloseHandle(recv_event);
                });
        }

        //关闭当前接收线程
        void close() final
        {
            if (recv_thread_is_run_)
            {
                //发送结束消息
                recv_thread_is_close_ = true;

                HANDLE send_event = ::CreateEvent(NULL, FALSE, FALSE, event_name_);

                ::SetEvent(send_event);
                ::CloseHandle(send_event);
            }
        }

        //创建一个消息队列实例
        bool create_instance(key_t key, size_t message_size, int message_count) final
        {
            auto queue_size = (sizeof(CShm_head) + message_size)* message_count;
            message_size_ = message_size;

            std::cout << "[create_instance]queue_size=" << queue_size << std::endl;

            //打开mmap对象
            char* shm_ptr = create_share_memory(key, queue_size);
            if (nullptr == shm_ptr)
            {
                return false;
            }
            else
            {
                //创建消息队列
                Resume_message_list(message_size, message_count);

                return true;
            }
        }

        void show_message_list() final
        {
            for (const auto& message_head : message_list_)
            {
                if (message_head->shm_message_ == Shm_message_type::MESSAGE_IS_FULL)
                {
                    std::cout << "message state: FULL,";
                }
                else
                {
                    std::cout << "message state: EMPTY,";
                }
                std::cout << "message size:" << message_head->message_max_size_ << ",";
                std::cout << "message curr len:" << message_head->message_curr_size_ << "," << std::endl;
            }
        }

        std::string get_error() const final
        {
            return error_;
        }

    private:
        void Resume_message_list(size_t message_size, int message_count)
        {
            for (int i = 0; i < message_count; i++)
            {
                CShm_head* shm_head = (CShm_head*)&shm_ptr_[i * (sizeof(CShm_head) + message_size)];
                shm_head->message_begin_ = (char*)&shm_ptr_[i * (sizeof(CShm_head) + message_size) + sizeof(CShm_head)];
                shm_head->message_end_ = (char*)&shm_ptr_[i * (sizeof(CShm_head) + message_size) + sizeof(CShm_head) + message_count];
                if (shm_memory_state_ == Shm_memory_state::SHM_INIT)                
                {
                    shm_head->message_max_size_ = message_size;
                    shm_head->shm_message_ = Shm_message_type::MESSAGE_IS_EMPTY;
                    shm_head->message_curr_size_ = 0;
                }

                message_list_.emplace_back(shm_head);
            }
        }

        void destroy_share_memory()
        {
            ::UnmapViewOfFile((void* )shm_ptr_);
            ::CloseHandle(shm_id_);
        }

        char* create_share_memory(key_t shm_key, size_t shm_size)
        {
            if (shm_key < 0) {
                std::stringstream ss;
                ss << "[" << __FILE__ << ":" << __LINE__ 
                    << "] CreateShareMem failed [key " << shm_key
                    << "]error: shm_key is more than 0";
                error_ = ss.str();
                return nullptr;
            }

            //将shm_key转换为自己的文件名(包括路径)
            std::string shm_file_name = std::to_string(shm_key);
            size_t shm_file_name_size = shm_file_name.length();
            wchar_t* shm_file_name_buffer = new wchar_t[shm_file_name_size + 1];
            ::MultiByteToWideChar(CP_ACP, 0, shm_file_name.c_str(), (int)shm_file_name_size, shm_file_name_buffer, (int)shm_file_name_size * sizeof(wchar_t));
            shm_file_name_buffer[shm_file_name_size] = 0; 

            shm_id_ = ::OpenFileMapping(FILE_MAP_ALL_ACCESS, 0, shm_file_name_buffer);
            if (NULL == shm_id_)
            {   
                shm_memory_state_ = Shm_memory_state::SHM_INIT;
                // 打开失败，创建之
                shm_id_ = ::CreateFileMapping(INVALID_HANDLE_VALUE,
                    NULL,
                    PAGE_READWRITE,
                    0,
                    (DWORD)shm_size,
                    shm_file_name_buffer);

                if (NULL == shm_id_)
                {
                    //创建文件失败
                    std::stringstream ss;
                    ss << "[" << __FILE__ << ":" << __LINE__
                        << "] CreateShareMem failed [key " << shm_key
                        << "] size:" << shm_size << ", error:" << GetLastError();
                    error_ = ss.str();
                    delete[] shm_file_name_buffer;
                    return nullptr;
                }

                std::cout << "[create_share_memory]Shm_memory_state::SHM_INIT" << std::endl;
            }
            else
            {
                shm_memory_state_ = Shm_memory_state::SHM_RESUME;
                std::cout << "[create_share_memory]Shm_memory_state::SHM_RESUME" << std::endl;
            }

            // 打开成功，映射对象的一个视图，得到指向共享内存的指针，显示出里面的数据
            shm_ptr_ = (char*)::MapViewOfFile(shm_id_, FILE_MAP_ALL_ACCESS, 0, 0, 0);
            delete[] shm_file_name_buffer;
            
            //设置一个event名字
            auto event_name = "Global\\" + shm_file_name;
            size_t event_name_size = 0;
            mbstowcs_s(&event_name_size, event_name_, event_name.c_str(), event_name.length());

            //获取或者创建当前共享内存一个进程间的互斥量
            //如果process_mutext不存在则创建，有则直接读取
            process_mutext_ = CreateMutex(NULL, false, _T("process_mutext"));
            queue_size_        = shm_size;
            return shm_ptr_;
        }

        std::string error_;
        Shm_memory_state shm_memory_state_ = Shm_memory_state::SHM_INIT;
        Shm_id shm_id_;
        char* shm_ptr_ = nullptr;
        size_t queue_size_ = 0;
        size_t message_size_ = 0;

        std::thread tt_recv_;
        std::vector<CShm_head*> message_list_;
        wchar_t event_name_[NAME_SIZE] = {'\0'};
        HANDLE process_mutext_;
        bool recv_thread_is_run_ = false;
        bool recv_thread_is_close_ = false;
    };
}

#endif
