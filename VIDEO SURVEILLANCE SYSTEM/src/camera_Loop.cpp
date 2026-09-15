#include "../header/functions.h"
#include <opencv2/videoio.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/video.hpp>
#include <thread>
#include <sys/prctl.h>
#include <iostream>


// time and frame founter for calculating FPS
auto start = std::chrono::high_resolution_clock::now();    
int frame_count = 0; 
int counter = 0;
const int TARGET_FRAME_TIME_MS = 100;

void display_resolution(cv::Mat frame) {
    int size = frame.rows;
    int height = frame.cols;
    std::cout << "Size: " << size << "  Height: " << height << "\n";
}

void count_frames() {

    frame_count++;
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end - start;

    if (diff.count() >= 1.0) {
        std::cout << "FPS: " << frame_count << " \n"; 
        frame_count = 0;
        start = std::chrono::high_resolution_clock::now();
    }

}  


void camera_Loop(int id, std::atomic<bool>& keepRunning, std::atomic<bool>& turnOffCamera) {

    cpu_set_t cpuset; // Bit mask for 4 CPU cores
    CPU_ZERO(&cpuset); // Shutdown all cores
    CPU_SET(id, &cpuset); // Set core with given id
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset); // OS moves core to this thread; pthread_self() .give it to this thread
    
    int sleep_time = 0;
    
    switch (id) {
        case 0:
            cameraState[id].subStreamrtsp_url = "rtsp://admin:123NsNd123!@192.168.10.50:554/h264Preview_01_sub";
            cameraState[id].mainStreamrtsp_url = "rtsp://admin:123NsNd123!@192.168.10.50:554/h264Preview_01_main";
            break;
        case 1:
            cameraState[id].subStreamrtsp_url = "rtsp://admin:123NsNd123!@192.168.10.60:554/h264Preview_01_sub";
            cameraState[id].mainStreamrtsp_url = "rtsp://admin:123NsNd123!@192.168.10.60:554/h264Preview_01_main";
            break;
        default:
            cameraState[id].subStreamrtsp_url = "rtsp://admin:123NsNd123!@192.168.0.70:554/h264Preview_01_sub";
            cameraState[id].mainStreamrtsp_url = "rtsp://admin:123NsNd123!@192.168.0.70:554/h264Preview_01_main";
            break;
    }
    

    // relative path from project root (workspace folder)
    std::string file_path = cameraState[id].subStreamrtsp_url;
        
    cv::VideoCapture cap(file_path, cv::CAP_FFMPEG);
    if (!cap.isOpened()) {
        std::cerr << "ERROR: cannot open file: " << file_path << "\n";
        return;
    }

    /* Using smart pointer allows for automatic deletion of data upon overwritte (i.e. when last known data goes out of scope)
    which prevents data leaking and allows shared managment using reference counter*/ 
    /*cv::BackgroundSubratctor is an OpenCv abstract class that allows use of different methods such as apply.
    In this case backround is a smart pointer to a BackgroundSubratctor object that automaticlly handels memory*/
    /*cv::createBackgroundSubtractorMOG2 (int history = 500, double varThreshold = 16, bool	detectShadows = true )
    history -> 500 - 1000 (how fast does model learn backround)
    varThreshold -> 16 - 25 (how different a pixel must be to count as foreground)
    detectShadows -> Shadow pixels are marked as 127 in mask (foreground is 255). Decreases a speed a bit*/
    cv::Ptr<cv::BackgroundSubtractorMOG2> background; 
    background = cv::createBackgroundSubtractorMOG2(500, 20, true);	// Interchangable values

    cv::Mat frame, resizedFrame, grayFrame, foreground_mask, background_image, kernel;

    /*
    cv::namedWindow("Frame", cv::WINDOW_NORMAL);
    cv::namedWindow("Background", cv::WINDOW_NORMAL);
    cv::resizeWindow("Frame", 640, 480);
    cv::resizeWindow("Background", 640, 480);
    */

    while (keepRunning.load()) {
        if (!turnOffCamera.load()){
            auto t1 = std::chrono::high_resolution_clock::now();

            //std::cout << cameraState[id].consecutiveHits << std::endl;
            // If it should reccord and it is not reccording, START reccording
            if (cameraState[id].mode == State::MONITORING && !cameraState[id].isRecording ) {
                cameraState[id].currentPath = get_full_Path(id);

                pid_t pid = fork();
                if (pid == 0) { 
                    prctl(PR_SET_PDEATHSIG, SIGTERM);

                    execlp("ffmpeg", "ffmpeg", "-hide_banner", "-y", "-loglevel", "error", 
                       "-rtsp_transport", "tcp", "-i", cameraState[id].mainStreamrtsp_url.c_str(), 
                       "-vcodec", "copy", "-acodec", "copy", "-map", "0", 
                       "-f", "matroska", cameraState[id].currentPath.c_str(), NULL);
                
                    exit(1);
                } else if (pid > 0) { 
                    cameraState[id].ffmpegPid = pid;
                    cameraState[id].isRecording = true;
                    std::cout << "[CAM " << id << "] SNIMANJE POKRENUTO: " << cameraState[id].currentPath << std::endl;

                    std::string message = "Nedefinirano\n"; 
                    if (cameraState[id].detectedType == 0) {
                        //message = "Osoba\n";
                    }
                    std::cout << message << std::endl;
                    std::thread([message]() {
                        send_pushNotification(message);}).detach();
                    }
            // If it shouldn't reccord and it is reccording, STOP reccording
            } else if (cameraState[id].mode != State::MONITORING && cameraState[id].isRecording) {
                // State changes to IDLE in get_Contours after 5s withouth movement
                if (cameraState[id].ffmpegPid > 0) {
                    kill(cameraState[id].ffmpegPid, SIGINT);
                    int status;
                    waitpid(cameraState[id].ffmpegPid, &status, WNOHANG); // Zombie cleanup
                }

                cameraState[id].isRecording = false;
                cameraState[id].ffmpegPid = -1;
                std::cout << "[CAM " << id << "] SNIMANJE ZAUSTAVLJENO." << std::endl;
            }

            if (!cap.read(frame)) {
                std::cout << "No more frames or cannot read frame\n";
                break;
            }
            if (frame.empty()) break;
            cv::resize(frame, frame, cv::Size(747, 427));

            // function call that displays frame reslolution
            /*
            display_resolution(frame);
            */

            // Lowering complexity of proccesed image data
            cv::cvtColor(frame, grayFrame, cv::COLOR_BGR2GRAY);
            background->apply(grayFrame, foreground_mask); // works continuously, retains history, possible to add learning rate
        
            /*
            background->getBackgroundImage(background_image);
            */

            //Morphology (clean motion mask)
            cv::threshold(foreground_mask, foreground_mask, 0, 255, cv::THRESH_BINARY); //Transform shadows to white pixels (x,y > 0 = 255; else 0)        
            cv::medianBlur(foreground_mask, foreground_mask, 3); //Removes noise 3 ili 5 možda je 5 bolji
        
            kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)); // kernel size 5 - 11; Rect - general purpuse; Ellipse smoother shapes; cross - directional filtering; Size - interchangable
            cv::morphologyEx(foreground_mask, foreground_mask, cv::MORPH_OPEN, kernel, cv::Point(-1, -1), 1); //Erosion (shrinks white regions) -> dilation (expands white regions) => Removes small white dots and thin connections - Uklanja male objekte (šum) koji su preživjeli blur, ali zadržava veličinu onih velikih (npr. provalnika).
            //cv::morphologyEx(foreground_mask, foreground_mask, cv::MORPH_CLOSE, kernel, cv::Point(-1,-1), 1); //Dilation -> erosion => fills small holes or gaps in objects

            //Get contours
            get_Contours(foreground_mask, frame, counter, id);

            /* 
            auto t2 = std::chrono::high_resolution_clock::now();
            auto ms_int = std::chrono::duration_cast<std::chrono::milliseconds>(t2-t1);
            std::cout << ms_int.count() << "ms\n";
            */
            
            /**/
            //Transforming image to correct format
            
            //count_frames();
            /*
            auto t2 = std::chrono::high_resolution_clock::now();
            auto ms_int = std::chrono::duration_cast<std::chrono::milliseconds>(t2-t1);
            if (ms_int.count() < TARGET_FRAME_TIME_MS) {
                sleep_time = TARGET_FRAME_TIME_MS - ms_int.count();
                //std::cout << ms_int.count() << "camera " << id << "ms\n";
                std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time));
            }
            */

            // Function call that displays FPS
            /**/
        
        

            /*
            v::imshow("Frame", frame);
            cv::imshow("Background", resized_frame);
            cv::imshow("Backgrounds", background_image);
            */
       
            if (cv::waitKey(5) >= 0) break;
        
        // Ititiate gracefull shutdown to avoid errors on another bootup
        } else if (cameraState[id].mode == State::MONITORING && cameraState[id].isRecording) {
                if (cameraState[id].ffmpegPid > 0) {
                    kill(cameraState[id].ffmpegPid, SIGINT);
                    int status;
                    waitpid(cameraState[id].ffmpegPid, &status, WNOHANG); // Zombie cleanup
                }

                cameraState[id].mode = State::IDLE;
                cameraState[id].isRecording = false;
                cameraState[id].ffmpegPid = -1;
                std::cout << "[CAM " << id << "] SNIMANJE ZAUSTAVLJENO." << std::endl;
                
        } else {
            std::this_thread::sleep_for(std::chrono::seconds(1)); // Change if needed
        }
    }
    cap.release();
    background.release();

}