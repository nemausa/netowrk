#include <atomic>
#include <list>
#include <vector>

#include "depends/easy_server_mgr.hpp"
#include "depends/cell_timestamp.hpp"

#include "depends/cell_thread.hpp"
#include "utils/conf.hpp"
// #define GLOG_NO_ABBREVIATED_SEVERITIES 
// #include "glog/logging.h"
using namespace std;

const char *ip = "127.0.0.1";
uint16_t port  = 4567;
int thread_num = 1;
int client_num = 1;

// 客户端每次发送几条消息
int msg_num = 10;
// 写入消息到缓冲区的时间
int send_sleep = 1;
// 工作休眠的时间
int work_sleep = 1;
int send_buffer_size = SEND_BUFF_SIZE;
int recv_buffer_size = RECV_BUFF_SIZE;

class my_client : public easy_client_mgr {
public:
    my_client() {
        is_check_id_ = config::instance().has_key("check_id");
    }

    void on_msg(data_header *header) {
    // msg_count_++;
    switch (header->cmd) {
    case CMD_LOGIN_RESULT: {
        login_result *login = (login_result*)header;
        if (is_check_id_) {
            if (login->msg_id != recv_msg_id_) {
                SPDLOG_LOGGER_INFO(spdlog::get(LOG_NAME), "socket<{}> msg_id<{}> recv_id<{}> {}", pclient_->sockfd(), login->msg_id, recv_msg_id_, login->msg_id - recv_msg_id_);
            }
            ++recv_msg_id_;
        }
        // printf("socket=%d CMD_LOGIN_RESULT, data length=%d\n", pclient_->sockfd(), header->length);
    }
    break;
    case CMD_LOGOUT_RESULT: {
        // printf("socket=%d CMD_LOGOUT_RESULT, data length=%d\n", pclient_->sockfd(), header->length);
    }
    break;
    case CMD_NEW_JOIN: {
        // printf("socket=%d CMD_NEW_JOIN, data length=%d\n", pclient_->sockfd(), header->length);
    }
    break;
    case CMD_ERROR: {
        printf("socket=%d CMD_ERROR, data length=%d\n", pclient_->sockfd(), header->length);
    }
    break;
    default: {
        printf("socket=%d undefined command, data length=%d\n", pclient_->sockfd(), header->length);
    }
    }
    }

    int send_test(login *data) {
        int ret = 0;
        // 如果剩余发送的次数大于0
        if (send_count_ > 0) {
            data->msg_id = send_msg_id_;
            ret = send_data(data);
             if (SOCKET_ERROR != ret) {
                 ++send_msg_id_;
                 --send_count_;
             }
        }
        return ret;
    }

    bool check_send(time_t dt) {
        reset_time_ += dt;
        if (reset_time_ >= send_sleep) {
            reset_time_ -= send_sleep;
            send_count_ = msg_num;
        }
        return send_count_ > 0;
    }
private:
    int recv_msg_id_ = 1;
    int send_msg_id_ = 1;
    time_t reset_time_ = 0;
    int send_count_ = 0;
    bool is_check_id_ = false;
};

std::atomic_int send_count(0);
std::atomic_int ready_count(0);
std::atomic_int connect_count(0); 

void work_thread(cell_thread *pthread, int id) {
    SPDLOG_LOGGER_INFO(spdlog::get(LOG_NAME), "thread<{}> start", id);
    vector<my_client*> clients(client_num);
    int begin = 0;
    int end = client_num;
    for (int n = begin; n < end; n++) {
        if (!pthread->is_run())
            break;
        clients[n] = new my_client();
        cell_thread::sleep(0);
    }
    for (int n = begin; n < end; n++) {
        if (!pthread->is_run()) {
            break;
        }
        if (INVALID_SOCKET == clients[n]->init_socket(send_buffer_size, recv_buffer_size)) {
            break;
        }
        if (SOCKET_ERROR == clients[n]->connect(ip, port)) {
            break;
        }
        connect_count++;
        cell_thread::sleep(0);
    }

    SPDLOG_LOGGER_INFO(spdlog::get(LOG_NAME), "thread<{}> connect<begin={}, end={}, connect_count={}>", id, begin, end, (int)connect_count);
    
    ready_count++;
    // 等待其他线程准备好再发送数据
    while (ready_count < thread_num && pthread->is_run()) {
        cell_thread::sleep(10);
    }

    login lg;
    strcpy(lg.user_name, "nemausa");
    strcpy(lg.password, "nemausa password");
    
    auto t2 = cell_timestamp::now_milliseconds();
    auto t0 = t2;
    auto dt = t0;

    cell_timestamp t_time;
    while (pthread->is_run()) {
        t0 = cell_timestamp::now_milliseconds();
        dt = t0 -t2;
        t2 = t0;
        
        int count = 0;
        for (int m = 0; m < msg_num; m++) {
            for (int n = begin; n < end; n++) {
                if (clients[n]->is_run()) {
                    if (clients[n]->send_test(&lg) > 0) {
                        ++send_count;
                    }
                }
            }
        }

        for (int n = begin; n < end; n++) {
            if (clients[n]->is_run()) {
                if (!clients[n]->on_run(0)) {
                    // 连接断开
                    connect_count--;
                    continue;
                }
                clients[n]->check_send(dt);
            }
        }

        cell_thread::sleep(work_sleep);
    }

    for (int n = begin; n < end; n++) {
        clients[n]->close();
        delete clients[n];
    }
    SPDLOG_LOGGER_INFO(spdlog::get(LOG_NAME), "thread<{}> exit", id);
    --ready_count;
}

#include <memory>
#include "spdlog/sinks/stdout_color_sinks.h"
int main(int argc, char *args[]) {
    config::instance().load("client.conf");
    ip = config::instance().get_string("ip");
    port = config::instance().get_int_default("port", 4567);
    thread_num = config::instance().get_int_default("thread_num", 1);
    client_num = config::instance().get_int_default("client_num", 1000);
    msg_num = config::instance().get_int_default("msg_num", 10);
    send_sleep = config::instance().get_int_default("send_sleep", 100);
    work_sleep = config::instance().get_int_default("work_sleep", 1);
    send_buffer_size = config::instance().get_int_default("send_buffer_size", SEND_BUFF_SIZE);
    recv_buffer_size = config::instance().get_int_default("recv_buffer_size", RECV_BUFF_SIZE);
	spdlog::cfg::load_env_levels();
    std::vector<spdlog::sink_ptr> sinks;
    sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    sinks.push_back(std::make_shared<spdlog::sinks::daily_file_sink_mt>("logs/client.txt", 23, 59));
    auto combined_logger = std::make_shared<spdlog::logger>(LOG_NAME, begin(sinks), end(sinks));
    //register it if you need to access it globally
    combined_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%-6t] [%^%-6l%$] [%-5n] [%!] [%#]  %v"); 
    spdlog::register_logger(combined_logger);
    spdlog::flush_every(std::chrono::seconds(5));
	//启动终端命令线程
	//用于接收运行时用户输入的指令
	cell_thread tCmd;
	tCmd.start(nullptr, [combined_logger](cell_thread* pThread) {
		while (true)
		{
			char cmdBuf[256] = {};
			scanf("%s", cmdBuf);
			if (0 == strcmp(cmdBuf, "exit"))
			{
				//pThread->Exit();
				combined_logger->info("exit ");
                pThread->exit();
				break;
			}
			else {
				combined_logger->info("undefine");
			}
		}
	});

    vector<cell_thread*> threads;
    for (int n = 0; n < thread_num; n++) {
        cell_thread *t = new cell_thread();
        t->start(nullptr, [n](cell_thread *pthread) {
            work_thread(pthread, n);
        });
        threads.push_back(t);
    }

    cell_timestamp ts;
    while (tCmd.is_run()) {
        auto t = ts.second();
        if (t >= 1.0) {
           SPDLOG_LOGGER_INFO(spdlog::get(LOG_NAME), 
                "thread<{}> clients<{}> connect<{}> time<{:02.4f}> send<{}>",
                thread_num, client_num, (int)connect_count, t, (int)send_count);
            send_count = 0;
            ts.update();
        }
        cell_thread::sleep(1);
    }

    for (auto t : threads) {
        t->close();
        delete t;
    }
    combined_logger->info("eixt");
    return 0;
}
