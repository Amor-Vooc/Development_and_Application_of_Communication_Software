#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <random>
#include <atomic>
#include <vector>

// 模拟参数配置
const int TOTAL_FRAMES = 10;     // A总共发送的数据帧数量
const int MAX_RETRIES = 3;       // 最大重传次数
const int DATA_DURATION = 100;   // 模拟数据传输耗时(ms)
const int ACK_DURATION = 50;     // 模拟ACK传输耗时(ms)
const int DIFS_TIME = 20;        // 分布式帧间间隔(ms)

// 信道状态枚举
enum ChannelState {
    IDLE,       // 空闲
    BUSY_A,     // A正在发送数据
    BUSY_B,     // B正在发送ACK
    NOISE,      // 噪音/冲突/其他主机占用
    COLLISION   // 发生了冲突
};

// 全局共享资源
ChannelState channel = IDLE;
std::mutex mtx;
std::condition_variable cv;
std::atomic<bool> simulation_running(true); // 控制模拟结束

// 随机数生成器
std::random_device rd;
std::mt19937 gen(rd());

// 辅助函数：生成随机退避时间
int get_backoff_time(int retries) {
    // 简单的指数退避模拟：重传次数越多，等待范围越大
    int max_wait = 100 * (1 << retries);
    std::uniform_int_distribution<> dis(50, max_wait);
    return dis(gen);
}

// 主机A：发送者
void hostA() {
    int success_count = 0;

    for (int i = 1; i <= TOTAL_FRAMES; ++i) {
        int retries = 0;
        bool frame_success = false;

        std::cout << "[Host A] 准备发送第 " << i << " 帧数据..." << std::endl;

        while (retries <= MAX_RETRIES) {
            // 1. CSMA: 载波监听 (Carrier Sense)
            {
                std::unique_lock<std::mutex> lk(mtx);
                // 模拟DIFS等待
                if (channel != IDLE) {
                    std::cout << "[Host A] 信道忙，正在等待..." << std::endl;
                    cv.wait(lk, [] { return channel == IDLE; });
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(DIFS_TIME));

            // 再次检查信道是否在DIFS期间被占用
            {
                std::unique_lock<std::mutex> lk(mtx);
                if (channel != IDLE) {
                    int backoff = get_backoff_time(retries);
                    std::cout << "[Host A] DIFS期间信道变忙，退避 " << backoff << "ms" << std::endl;
                    lk.unlock();
                    std::this_thread::sleep_for(std::chrono::milliseconds(backoff));
                    continue; // 重新开始监听
                }

                // 2. 发送数据
                channel = BUSY_A;
                std::cout << "[Host A] -> 发送数据帧 (尝试次数: " << retries + 1 << ")" << std::endl;
            }

            cv.notify_all(); // 通知B和干扰线程

            // 模拟传输时间，这里不持有锁，允许干扰发生
            std::this_thread::sleep_for(std::chrono::milliseconds(DATA_DURATION));

            // 3. 等待ACK
            // 我们需要等待B把信道变成 BUSY_B
            // 如果超时，或者信道变成了 COLLISION/NOISE，则失败
            {
                std::unique_lock<std::mutex> lk(mtx);
                // 等待 B 发送 ACK，超时设为 200ms
                bool received_ack = cv.wait_for(lk, std::chrono::milliseconds(200), [] {
                    return channel == BUSY_B;
                    });

                if (received_ack) {
                    std::cout << "[Host A] <=== 收到 ACK，第 " << i << " 帧发送成功。" << std::endl;

                    // 等待ACK传输结束，释放信道
                    lk.unlock();
                    std::this_thread::sleep_for(std::chrono::milliseconds(ACK_DURATION));

                    lk.lock();
                    channel = IDLE;
                    cv.notify_all();

                    frame_success = true;
                    success_count++;
                    break; // 跳出重传循环，发送下一帧
                }
                else {
                    // 超时或未收到ACK
                    std::cout << "[Host A] 未收到ACK (超时或冲突)。" << std::endl;
                    channel = IDLE; // 重置信道
                    cv.notify_all();

                    retries++;
                    if (retries <= MAX_RETRIES) {
                        int backoff = get_backoff_time(retries);
                        std::cout << "[Host A] 准备第 " << retries << " 次重传，随机退避 " << backoff << "ms..." << std::endl;
                        lk.unlock(); // 释放锁进行休眠
                        std::this_thread::sleep_for(std::chrono::milliseconds(backoff));
                    }
                }
            }
        }

        if (!frame_success) {
            std::cout << "[Host A] !!! 第 " << i << " 帧重传次数超限，丢弃该帧 !!!" << std::endl;
        }

        // 帧间稍微间隔一下
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    std::cout << "\n[Host A] 任务结束。成功发送: " << success_count << "/" << TOTAL_FRAMES << std::endl;
    simulation_running = false; // 通知其他线程退出
    cv.notify_all();
}

// 主机B：接收者
void hostB() {
    while (simulation_running) {
        std::unique_lock<std::mutex> lk(mtx);

        // 等待信道变为 BUSY_A
        cv.wait(lk, [] { return channel == BUSY_A || !simulation_running; });

        if (!simulation_running) break;

        // 开始接收
        // std::cout << "[Host B] 检测到载波，正在接收..." << std::endl;

        // 在接收过程中释放锁，看看会不会发生冲突（被干扰线程修改）
        lk.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(DATA_DURATION - 20)); // 稍小于A的发送时间
        lk.lock();

        // 检查接收完后信道状态
        if (channel == BUSY_A) {
            // 接收成功，准备发送ACK
            // std::cout << "[Host B] 数据帧接收完整，准备发送ACK。" << std::endl;
            channel = BUSY_B;
            cv.notify_all(); // 通知A收到ACK了
        }
        else {
            // 信道状态变了（被干扰置为 NOISE/COLLISION）
            std::cout << "[Host B] 数据帧损坏 (检测到冲突)，不发送ACK。" << std::endl;
            // B什么都不做，A会超时
        }
    }
}

// 主线程逻辑：干扰生成器
void interference_generator() {
    std::uniform_int_distribution<> dis(300, 800); // 随机间隔产生干扰

    while (simulation_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(dis(gen)));

        if (!simulation_running) break;

        std::unique_lock<std::mutex> lk(mtx);
        if (channel == BUSY_A) {
            std::cout << "\n[Environment] *** 突发干扰！制造冲突！ ***\n" << std::endl;
            channel = COLLISION; // 修改状态，导致B接收失败
            cv.notify_all();
        }
        else if (channel == IDLE) {
            // 占用一下信道，模拟其他主机通信
            // std::cout << "[Environment] 其他主机正在使用信道..." << std::endl;
            channel = NOISE;
            lk.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            lk.lock();
            if (channel == NOISE) channel = IDLE;
            cv.notify_all();
        }
    }
}

int main() {
    std::cout << "=== CSMA/CA 协议模拟程序启动 ===" << std::endl;
    std::cout << "配置: 发送" << TOTAL_FRAMES << "帧, 最大重传" << MAX_RETRIES << "次" << std::endl;
    std::cout << "----------------------------------" << std::endl;

    // 创建线程
    std::thread thread_b(hostB);
    std::thread thread_noise(interference_generator);
    std::thread thread_a(hostA); // A 开始运行

    // 等待A完成任务
    thread_a.join();

    // A完成后，B和Noise线程会因为 simulation_running 变为 false 而退出
    thread_b.join();
    thread_noise.join();

    std::cout << "=== 模拟结束 ===" << std::endl;
    return 0;
}