#pragma once   

#include <opencv2/opencv.hpp>
#include "tensorflow/lite/interpreter.h"
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <chrono>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

//Function declaration
void get_Contours(cv::Mat foreground_mask, cv::Mat frame, int &counter, int id);
void call_TFLite_Interpreter(int id, std::atomic<bool>& keepRunning);
void print_model_info(std::unique_ptr<tflite::Interpreter>& interpreter);
void camera_Loop(int id, std::atomic<bool>& keepRunning, std::atomic<bool>& turnOffCamera);
std::string get_full_Path(int id);
void send_pushNotification(std::string message);

void stop_live_stream(int id);
void start_live_stream(int id);
void handle_communication(int id, std::atomic<bool>& keepRunning, std::atomic<bool>& turnOffCamera);

// Camer thread safe queue for managing frames and paralel reading and writing into queue
struct CameraFrame {
    cv::Mat frame;
    int cameraId;
};

template<typename T>
class ThreadSafeQueue {

    private:
        std::mutex mut;
        std::queue<T> data_queue;
        std::condition_variable data_condition;
    public:
        void push(T new_value) {
            std::lock_guard<std::mutex> lock(mut);
            data_queue.push(new_value);
            data_condition.notify_one();
        }

        void wait_and_pop(T& value) {
            std::unique_lock<std::mutex> lock(mut);
            data_condition.wait(lock, [this]{return !data_queue.empty();}); // Wake up only if queue is not empty
            value = data_queue.front();
            data_queue.pop();
        }

        bool empty() const {
            std::lock_guard<std::mutex> lock(mut);
            return data_queue.empty();
        }

        size_t size() {
            std::lock_guard<std::mutex> lock(mut);
            return data_queue.size();
        }
};

extern ThreadSafeQueue<CameraFrame> frame_Queue;

// Used in camera State machine model to manage how long is camera in each of its states
class Stopwatch {
    private:
        using Clock = std::chrono::steady_clock;
        std::chrono::time_point<Clock> startPoint;
        bool running = false;

    public:
        void start() {
            if (!running) {
                startPoint = Clock::now();
                running = true;
            }
        }

        void stop() {
            running = false;
        }

        double getSeconds() const {
            if (!running) return 0.0;

            auto now = Clock::now();
            std::chrono::duration<double> elapsed = now - startPoint;
            return elapsed.count();
        }

        bool isRunning() const {
            return running;
        }
};


// State machine managing struct
enum class State { IDLE, ACTIVE, MONITORING };

struct CameraState {
    std::atomic<State> mode{State::IDLE};
    std::atomic<bool> motionDetected{false};
    std::atomic<int> consecutiveHits{0};
    std::atomic<int> detectedType{0};
    Stopwatch graceTimer;

    pid_t ffmpegPid = -1;
    std::atomic<bool> isRecording{false};
    std::string currentPath = "";
    std::string subStreamrtsp_url = "";
    std::string mainStreamrtsp_url = "";

    std::atomic<bool> isStreaming{false};
    pid_t liveStreamPid = -1;

    int getInterval() const {
        if (mode == State::MONITORING) return 15; // Every 15 FPS
        if (mode == State::ACTIVE) return 3; // Evry 3 FPS
        return 0; // Don't send anything
    }


};

// Global vector for 3 cameras
extern std::vector<CameraState> cameraState;