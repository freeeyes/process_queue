#pragma once

#include "shm_common.hpp"

#if PSS_PLATFORM == PLATFORM_UNIX

#include <sys/ipc.h>
#include <sys/msg.h>

const int MAX_MESSAGE_SIZE = 100;

namespace shm_queue {
    const int MAX_MESSAGE_SIZE = 100;
    const unsigned long MESSAGE_LOGIC = 1;
    const unsigned long MESSAGE_EXIT = 2;

    class Msg_queue_buffer {
    public:
        unsigned long message_type_ = MESSAGE_LOGIC;
        size_t len = 0;
        char text[MAX_MESSAGE_SIZE] = { '\0' };
    };

    class CMessage_Queue_Linux : public CShm_queue_interface
    {
    public:
        CMessage_Queue_Linux()
        {

        }

        ~CMessage_Queue_Linux()
        {

        }

        bool set_proc_message(const char* message_text, size_t len) final
        {
            Msg_queue_buffer msg_queue_data;
            struct msqid_ds msq_id_ds_buf;

            if (message_max_size_ < len)
            {
                std::stringstream ss;
                ss << "[" << __FILE__ << ":" << __LINE__
                    << "] set_proc_message fail [key " << queue_key_
                    << "]error: len is more than message_max_size_" << std::endl;
                error_ = ss.str();
                return false;

            }

            //������г�����ָ�����������Ͳ��ڲ���
            if ((msgctl(msg_queue_id_, IPC_STAT, &msq_id_ds_buf) >= 0) && ((int)msq_id_ds_buf.msg_qnum > message_count_))
            {
                std::stringstream ss;
                ss << "[" << __FILE__ << ":" << __LINE__
                    << "] set_proc_message fail [key " << queue_key_
                    << "]error: msg queue is full" << std::endl;
                error_ = ss.str();
                return false;
            }

            memcpy(msg_queue_data.text, message_text, len);
            msg_queue_data.len = len;

            //������Ϣ����
            size_t message_size = MAX_MESSAGE_SIZE + sizeof(size_t) + sizeof(unsigned long);
            if (0 != msgsnd(msg_queue_id_, (void*)&msg_queue_data, message_size, IPC_NOWAIT))
            {
                int error_no = errno;

                std::stringstream ss;
                ss << "[" << __FILE__ << ":" << __LINE__
                    << "] set_proc_message fail [key " << queue_key_
                    << "]send error: (" << error_no << ")" << strerror(error_no) << std::endl;
                error_ = ss.str();
                return false;
            }
            else
            {
                return true;
            }
        }

        //������Ϣ
        void recv_message(queue_recv_message_func fn_logic) final
        {
            recv_thread_is_run_ = true;
            tt_recv_ = std::thread([this, fn_logic]() {
                Msg_queue_buffer msg_queue_data;
                size_t message_size = MAX_MESSAGE_SIZE + sizeof(size_t) + sizeof(unsigned long);

                //���thread id
                std::thread::id this_id = std::this_thread::get_id();
                std::stringstream sa;
                sa << this_id;
                thread_id_ = std::atoll(sa.str().c_str());

                while (1) {
                    if (msgrcv(msg_queue_id_, (void*)&msg_queue_data, message_size, 0, 0) == -1) {
                        std::stringstream ss;
                        ss << "[" << __FILE__ << ":" << __LINE__
                            << "] recv_message fail [key " << queue_key_
                            << "]recv error: (" << errno << ")" << strerror(errno) << std::endl;
                        error_ = ss.str();

                        if (error_func_)
                        {
                            error_func_(error_);
                        }

                        break;
                    }

                    if (MESSAGE_EXIT == msg_queue_data.message_type_)
                    {
                        //����ǹرգ����������Ϣ�ǲ��Ǳ��߳���Ϣ
                        if ((unsigned long long)msg_queue_data.len == thread_id_)
                        {
                            //�̹߳ر�
                            std::cout << "[recv_message]recv thread is close" << std::endl;
                            break;
                        }
                        else
                        {
                            //���Ǳ��̵߳Ĺرգ����·Ż���Ϣ����
                            std::cout << "[recv_message]recv thread is not local" << std::endl;
                            msgsnd(msg_queue_id_, (void*)&msg_queue_data, message_size, IPC_NOWAIT);
                        }
                    }

                    //������յ�����
                    fn_logic(msg_queue_data.text, msg_queue_data.len);
                }

                //����лص��¼�����ص�
                if (close_func_)
                {
                    close_func_(queue_key_);
                }

                });
        }

        void close() final
        {
            if (recv_thread_is_run_)
            {
                //��Ҫ�رն�Ӧ����Ϣ�����߳�(�����Ӧ���߳�ID)
                Msg_queue_buffer msg_queue_data;

                size_t message_size = MAX_MESSAGE_SIZE + sizeof(size_t) + sizeof(unsigned long);
                msg_queue_data.len = (size_t)thread_id_;
                msg_queue_data.message_type_ = MESSAGE_EXIT;

                msgsnd(msg_queue_id_, (void*)&msg_queue_data, message_size, IPC_NOWAIT);

                tt_recv_.join();
            }
        }

        bool create_instance(shm_key key, size_t message_size, int message_count) final
        {
            queue_key_ = key;
            message_max_size_ = message_size;
            message_count_ = message_count;

            if (message_size > MAX_MESSAGE_SIZE)
            {
                std::stringstream ss;
                ss << "[" << __FILE__ << ":" << __LINE__
                    << "] create instance fail [key " << queue_key_
                    << "]error: len more than MAX_MESSAGE_SIZE, you must reset MAX_MESSAGE_SIZE." << std::endl;
                error_ = ss.str();
                return false;
            }

            //�򿪻��ߴ�����Ϣ����
            msg_queue_id_ = msgget(queue_key_, 0666 | IPC_CREAT);
            if (msg_queue_id_ < 0)
            {
                std::stringstream ss;
                ss << "[" << __FILE__ << ":" << __LINE__
                    << "] create instance fail [key " << queue_key_
                    << "]error: " << strerror(errno) << std::endl;
                error_ = ss.str();
                return false;
            }

            return true;
        }

        void show_message_list() final
        {
            struct msqid_ds msq_id_ds_buf;

            if ((msgctl(msg_queue_id_, IPC_STAT, &msq_id_ds_buf) >= 0))
            {
                std::cout << "[show_message_list]message count:" << msq_id_ds_buf.msg_qnum << std::endl;
            }
        }

        std::string get_error() const final
        {
            return error_;
        }

        void set_error_function(queue_error_func error_func)
        {
            error_func_ = error_func;
        }

        void set_close_function(queue_close_func close_func)
        {
            close_func_ = close_func;
        }

    private:
        shm_key queue_key_;
        size_t message_max_size_ = 0;
        int message_count_ = 0;
        int msg_queue_id_ = 0;
        std::string error_;
        std::thread tt_recv_;
        bool recv_thread_is_run_ = false;
        unsigned long long thread_id_ = 0;
        queue_error_func error_func_ = nullptr;
        queue_close_func close_func_ = nullptr;
    };

};

#endif
