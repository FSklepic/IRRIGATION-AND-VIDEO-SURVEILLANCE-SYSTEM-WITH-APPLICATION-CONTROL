#include "../header/functions.h"
#include <opencv2/imgproc.hpp>

static thread_local float smoothX = 373.5f; // Start position in the middle of the frame (896/2)
static thread_local float smoothY = 213.5f; // Start position in the middle of the frame (512/2);
const float alpha = 0.2f;      // Smoothing factor (0.1 - 0.3 je idealno)
const int TARGET_SIZE = 400;
const int frameW = 747;
const int frameH = 427;

void get_Contours(cv::Mat foreground_mask, cv::Mat frame, int &counter, int id) {
    bool noMovement = true;
    int num_of_Boxes = 0;
    //imshow("Other", frame);
    std::vector<std::vector<cv::Point>> contours; //Outer vector - all detected contours; Inner vector - points belonging to contour
    //std::vector<cv::Vec4i> hierarchy; //Vector/array containing 4 intigers/relationships of contours [next, previous, first_child, parent]

    cv::findContours(foreground_mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE ); //RETR_EXTERNAL vs RETR_TREE tryout; CHAIN_APPROX_SIMPLE vs CHAIN_APPROX_NONE tryout
    //cv::drawContours(frame, contours, -1, cv::Scalar(255, 0, 255), 2);
    cv::Rect finalROI, currentRect;
    cv::Mat finalForAI;

    for (int i = 0; i < contours.size(); i++) {
        //std::cout << "Countour size: " << contours.size() << std::endl;
        double area = cv::contourArea(contours[i]);
        //std::cout << area << std::endl;
        
        //Bounding box
        if (area >= 500.0) {
            if (cameraState[id].mode == State::IDLE) {
                cameraState[id].mode = State::ACTIVE;
                cameraState[id].graceTimer.stop();
                std::cout << "prelazim u ACTIVE " << std::endl;
            }
            
            num_of_Boxes++;
            // Connect all contours into one bounding box
            currentRect = cv::boundingRect(contours[i]); // Find all edge points of contours
            if(num_of_Boxes == 1) {
                finalROI = currentRect;
            } else {
                finalROI |= currentRect;
            }
            noMovement = false;
        
            //cv::drawContours(frame, contours, -1, cv::Scalar(255, 0, 255), 2);
        }

    } 

    if (noMovement) {
        if (!cameraState[id].graceTimer.isRunning()) {
            cameraState[id].graceTimer.start();
        }

        if (cameraState[id].mode != State::IDLE && cameraState[id].graceTimer.getSeconds() >= 5.0f) {
            cameraState[id].mode = State::IDLE;
            cameraState[id].consecutiveHits = 0;
            cameraState[id].motionDetected = false;
            cameraState[id].graceTimer.stop();
            std::cout << "prelazim u IDLE " << std::endl;
        }

        return;
    }

    //std::cout <<  "Counter: " << counter <<  std::endl;
    // CALCULATE CENTER OF FRAME AND APPLY EMA SMOOTHING
    float targetCenterX = finalROI.x + finalROI.width / 2.0f;
    float targetCenterY = finalROI.y + finalROI.height / 2.0f;

    smoothX = (alpha * targetCenterX) + (1.0f - alpha) * smoothX;
    smoothY = (alpha * targetCenterY) + (1.0f - alpha) * smoothY;

    // DECISION: CROP OR USE FULL FRAME
    // If objects are far apart (union of objects is bigger than 400x400 pixels)
    if (num_of_Boxes > 0 && finalROI.width < 400 && finalROI.height < 400) {

        // Prepare borders for roi
        int startX = std::round(smoothX - (TARGET_SIZE / 2));
        int startY = std::round(smoothY - (TARGET_SIZE / 2));

        startX = std::max(0, std::min(startX, frameW - TARGET_SIZE));
        startY = std::max(0, std::min(startY, frameH - TARGET_SIZE));

        // Check edges and "Letterboxing" (Padding)
        cv::Rect roi(startX, startY, TARGET_SIZE, TARGET_SIZE);
        cv::Mat cut = frame(roi);
        cv::resize(cut, finalForAI, cv::Size(300, 300));
        
        // Letterbox with black borders
        /*   
        // If ROI is fully inside the picture
        if (roi.x >= 0 && roi.y >= 0 && roi.x + roi.width <= frame.cols && roi.y + roi.height <= frame.rows) {
            finalForAI = frame(roi).clone();
        } else {
            // If ROI goes out of the border frame, we move it
            int left = std::max(0, roi.x);
            int top = std::max(0, roi.y);
            int right = std::min(frame.cols, roi.x + roi.width);
            int bottom = std::min(frame.rows, roi.y + roi.height);

            cv::Mat cut = frame(cv::Rect(left, top, right - left, bottom - top));
            
            
            int padTop = std::max(0, -roi.y);
            int padBottom = std::max(0, (roi.y + roi.height) - frame.rows);
            int padLeft = std::max(0, -roi.x);
            int padRight = std::max(0, (roi.x + roi.width) - frame.cols);

            cv::copyMakeBorder(cut, finalForAI, padTop, padBottom, padLeft, padRight, cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0));
            
        }
        */

    } else {
        cv::resize(frame, finalForAI, cv::Size(300, 300));
    }

    //cv::rectangle(frame, finalROI.tl(), finalROI.br(), cv::Scalar(0, 255,0), 4);
     // Standart for YOLO
    cv::cvtColor(finalForAI, finalForAI, cv::COLOR_BGR2RGB); // BGR is OpenCV standard and RGB is AI standard
    //resizedFrame.convertTo(resizedFrame, CV_8SC3, 1, -128); // Normalisation from int 0-255 to float 0.0-1.0 (int is too big for AI)
    //imshow("Frame", finalForAI);
    int interval = cameraState[id].getInterval();
    if (interval > 0 && counter % interval == 0) {
        frame_Queue.push({std::move(finalForAI), id});
        counter = 0;
    } 
    
    counter++;
  
}