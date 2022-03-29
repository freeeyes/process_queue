#pragma once

#include "shm_common.hpp"

#if PSS_PLATFORM == PLATFORM_WIN
#include <vector>
#include <tchar.h>
#include <windows.h>

#define NAME_SIZE      200   //�¼����Ƴ���

#define Shm_id HANDLE

namespace shm_queue {
    enum class Shm_memory_state : unsigned char
    {
        SHM_INIT,     //��һ�����빲���ڴ棬��ʼ��
        SHM_RESUME,   //�����ڴ��Ѵ��ڣ��ָ�����ӳ�乲���ڴ�����
    };

    enum class Shm_message_type : unsigned char
    {
        MESSAGE_IS_FULL,     //��Ϣ����δ������Ϣ
        MESSAGE_IS_EMPTY,    //��Ϣ��û��δ������Ϣ
    };

    //�����ڴ�ͷ
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

            //������ס���������ö�ȡ�Ȳ�������
            ::WaitForSingleObject(process_mutext_, INFINITE);

            //Ѱ��һ������Ĺ����ڴ濨�ۣ������ݲ����ȥ
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

            //������ɣ��ſ�������
            ::ReleaseMutex(process_mutext_);

            //����һ���¼�
            HANDLE send_event = ::CreateEvent(NULL, FALSE, FALSE, event_name_);
            if (send_event == nullptr)
            {
                //����������Ϣ
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

        //������Ϣ
        void recv_message(queue_recv_message_func fn_logic) final
        {
            //����һ���̣߳����ý���λ��
            recv_thread_is_run_ = true;
            tt_recv_ = std::thread([this, fn_logic]() {
                HANDLE recv_event = ::CreateEvent(NULL, FALSE, FALSE, event_name_);
                if (recv_event == nullptr)
                {
                    //����������Ϣ
                    std::stringstream ss;
                    ss << "[" << __FILE__ << ":" << __LINE__
                        << "] recv_message failed error:" << GetLastError();
                    error_ = ss.str();

                    if (error_func_)
                    {
                        error_func_(error_);
                    }
                }

                while (true)
                {
                    ::WaitForSingleObject(recv_event, INFINITE);  //�յ��źţ��Զ�����

                    //������ס���������ö�ȡ�Ȳ�������
                    ::WaitForSingleObject(process_mutext_, INFINITE);

                    //Ѱ��һ������Ĺ����ڴ濨�ۣ������ݲ����ȥ
                    for (auto& message : message_list_)
                    {
                        if (message->shm_message_ == Shm_message_type::MESSAGE_IS_FULL)
                        {
                            fn_logic(message->message_begin_, message->message_curr_size_);
                            message->shm_message_ = Shm_message_type::MESSAGE_IS_EMPTY;
                        }
                    }

                    //������ɣ��ſ�������
                    ::ReleaseMutex(process_mutext_);

                    //����ǹر��¼������˳���ǰ�߳�
                    if (recv_thread_is_close_)
                    {
                        break;
                    }
                }

                //�������ݽ����߳�
                ::CloseHandle(recv_event);

                //����лص��¼�����ص�
                if (close_func_)
                {
                    close_func_(shm_key_);
                }
                });
        }

        //�رյ�ǰ�����߳�
        void close() final
        {
            if (recv_thread_is_run_)
            {
                //���ͽ�����Ϣ
                recv_thread_is_close_ = true;

                HANDLE send_event = ::CreateEvent(NULL, FALSE, FALSE, event_name_);

                ::SetEvent(send_event);
                ::CloseHandle(send_event);

                tt_recv_.join();
            }
        }

        //����һ����Ϣ����ʵ��
        bool create_instance(shm_key key, size_t message_size, int message_count) final
        {
            auto queue_size = (sizeof(CShm_head) + message_size)* message_count;
            message_size_ = message_size;

            std::cout << "[create_instance]queue_size=" << queue_size << std::endl;

            //��mmap����
            char* shm_ptr = create_share_memory(key, queue_size);
            if (nullptr == shm_ptr)
            {
                return false;
            }
            else
            {
                //������Ϣ����
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

        char* create_share_memory(shm_key shm_key, size_t shm_size)
        {
            if (shm_key < 0) {
                std::stringstream ss;
                ss << "[" << __FILE__ << ":" << __LINE__ 
                    << "] CreateShareMem failed [key " << shm_key
                    << "]error: shm_key is more than 0";
                error_ = ss.str();
                return nullptr;
            }

            //��shm_keyת��Ϊ�Լ����ļ���(����·��)
            std::string shm_file_name = std::to_string(shm_key);
            size_t shm_file_name_size = shm_file_name.length();
            wchar_t* shm_file_name_buffer = new wchar_t[shm_file_name_size + 1];
            ::MultiByteToWideChar(CP_ACP, 0, shm_file_name.c_str(), (int)shm_file_name_size, shm_file_name_buffer, (int)shm_file_name_size * sizeof(wchar_t));
            shm_file_name_buffer[shm_file_name_size] = 0; 

            shm_id_ = ::OpenFileMapping(FILE_MAP_ALL_ACCESS, 0, shm_file_name_buffer);
            if (NULL == shm_id_)
            {   
                shm_memory_state_ = Shm_memory_state::SHM_INIT;
                // ��ʧ�ܣ�����֮
                shm_id_ = ::CreateFileMapping(INVALID_HANDLE_VALUE,
                    NULL,
                    PAGE_READWRITE,
                    0,
                    (DWORD)shm_size,
                    shm_file_name_buffer);

                if (NULL == shm_id_)
                {
                    //�����ļ�ʧ��
                    std::stringstream ss;
                    ss << "[" << __FILE__ << ":" << __LINE__
                        << "] CreateShareMem failed [key " << shm_key
                        << "] size:" << shm_size << ", error:" << GetLastError();
                    error_ = ss.str();
                    delete[] shm_file_name_buffer;
                    return nullptr;
                }

                //std::cout << "[create_share_memory]Shm_memory_state::SHM_INIT" << std::endl;
            }
            else
            {
                shm_memory_state_ = Shm_memory_state::SHM_RESUME;
                //std::cout << "[create_share_memory]Shm_memory_state::SHM_RESUME" << std::endl;
            }

            // �򿪳ɹ���ӳ������һ����ͼ���õ�ָ�����ڴ��ָ�룬��ʾ�����������
            shm_ptr_ = (char*)::MapViewOfFile(shm_id_, FILE_MAP_ALL_ACCESS, 0, 0, 0);
            delete[] shm_file_name_buffer;
            
            //����һ��event����
            auto event_name = "Global\\" + shm_file_name;
            size_t event_name_size = 0;
            mbstowcs_s(&event_name_size, event_name_, event_name.c_str(), event_name.length());

            //��ȡ���ߴ�����ǰ�����ڴ�һ�����̼�Ļ�����
            //���process_mutext�������򴴽�������ֱ�Ӷ�ȡ
            process_mutext_ = CreateMutex(NULL, false, _T("process_mutext"));
            queue_size_     = shm_size;
            shm_key_        = shm_key;
            return shm_ptr_;
        }

        void set_error_function(queue_error_func error_func)
        {
            error_func_ = error_func;
        }

        void set_close_function(queue_close_func close_func)
        {
            close_func_ = close_func;
        }

        std::string error_;
        Shm_memory_state shm_memory_state_ = Shm_memory_state::SHM_INIT;
        shm_key shm_key_ = 0;
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
        queue_error_func error_func_ = nullptr;
        queue_close_func close_func_ = nullptr;
    };
}

#endif
