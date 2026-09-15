#include "../header/functions.h"

#include <csignal>
#include <thread>
#include <functional>
#include <iostream>


std::atomic<bool>* stop_ptr = nullptr;
ThreadSafeQueue<CameraFrame> frame_Queue;
std::vector<CameraState> cameraState(3);


// Function that allows user to stop and end program
void signalHandeler(int sig) {
    std::cout << "Interrupt handle " << sig << std::endl;

    if(stop_ptr) {

        *stop_ptr = false;
    }
    
}


int main() {
   
    // Seting max thread count to avoid OpenCV using multible cores and to avoid context switching
    cv::setNumThreads(1);

    // Atomic vriable that informs threads to stop and initiate gracefull shutdown
    std::atomic<bool> keepRunning(true);
    std::atomic<bool> turnOffCamera(false);
    stop_ptr = &keepRunning;

    signal(SIGINT, signalHandeler);

    //std::thread camera1(camera_Loop, 0, std::ref(keepRunning), std::ref(turnOffCamera));
    std::thread camera2(camera_Loop, 1, std::ref(keepRunning), std::ref(turnOffCamera));
    //std::thread camera3(camera_Loop, 2, std::ref(keepRunning), std::ref(turnOffCamera));
    std::thread ai(call_TFLite_Interpreter, 3, std::ref(keepRunning));
    std::thread communication(handle_communication, 3, std::ref(keepRunning), std::ref(turnOffCamera));

    //camera_Loop(std::ref(keepRunning));

    std::cout << "System is running. Pres Ctr+C to end it." << std::endl;
    std::cout << "Listening for commands on port 8895..." << std::endl;


    while(keepRunning) {

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // Šaljemo prazan frame da "probudimo" AI dretvu iz wait_and_pop
    frame_Queue.push(CameraFrame());
    
    std::cout << "System shutdown..." << std::endl;

    if(communication.joinable()) communication.join();
    //if(camera1.joinable()) camera1.join();
    if(camera2.joinable()) camera2.join();
    //if(camera3.joinable()) camera3.join();
    if(ai.joinable()) ai.join();

    return 0;
}