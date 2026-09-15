#include "../header/functions.h"
#include "tensorflow/lite/model.h"
#include "tensorflow/lite/kernels/register.h"

auto start2 = std::chrono::high_resolution_clock::now(); 
int frame_count2 = 0;

// Function for timing how much frames can Interpreter process in a second (used for debuging)
void count_frames2() {

    frame_count2++;
    auto end2 = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end2 - start2;

    if (diff.count() >= 1.0) {
        std::cout << "FPS: " << frame_count2 << " \n"; 
        frame_count2 = 0;
        start2 = std::chrono::high_resolution_clock::now();
    }

} 

void call_TFLite_Interpreter(int id, std::atomic<bool>& keepRunning) {

    cpu_set_t cpuset; // Bit mask for 4 CPU cores
    CPU_ZERO(&cpuset); // Shutdown all cores
    CPU_SET(id, &cpuset); // Set core with given id
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset); // OS moves core to this thread; pthread_self() .give it to this thread
    
    // Loading of smart model from given resource file that auto delets after exiting the function (unique_ptr). 
    // FlatBufferModel represents special format for rading files withouth the need of unparsing
    std::unique_ptr<tflite::FlatBufferModel> model = tflite::FlatBufferModel::BuildFromFile("Resource_Files/detect.tflite");
    if (!model) {
        std::cerr << "Ne mogu učitati model!" << std::endl;
        return;
    }
    
    // Model that contains librarys that allow sloving mathematical operations 
    tflite::ops::builtin::BuiltinOpResolver resolver;

    // Building interpreter from previous models that will start neuron web
    std::unique_ptr<tflite::Interpreter> interpreter;
    tflite::InterpreterBuilder(*model, resolver)(&interpreter); 

    interpreter->AllocateTensors(); 

    //print_model_info(interpreter);
    
    // How much rame it allocates
    //size_t bytes = interpreter->tensor(0)->bytes;
    //std::cout << "Veličina ulaznog tenzora: " << bytes / 1024.0 << " KB" << std::endl;

    // Get input tensor (place in RAM where AI expects image)
    // 1. INPUT (UInt8 300x300)
    int input_index, num_detections, max_elements, class_id;
    size_t expected_bytes, actual_bytes;
    uint8_t* input = nullptr;
    float* classes = nullptr;
    float* scores = nullptr;
    float* num_detections_ptr = nullptr;

    while (keepRunning.load()) {
        auto t1 = std::chrono::high_resolution_clock::now();

        CameraFrame cf;
        frame_Queue.wait_and_pop(cf);
        //size_t trenutno_u_redu = frame_Queue.size();

        //std::cout << "Trenutno u redu: " << trenutno_u_redu << std::endl;
        // Get pointer to the input tensor
        input = interpreter->typed_input_tensor<uint8_t>(0);
        if (!input) {
            std::cerr << "Greška, ulazni tensor nije UInt8!" << std::endl;
            return;
        }

        // Fast copying of bajts from memory to TFLite (instead copying pixels this function sends whole blocks of data at once)
        // inputImage.data - start of image data; inputImage.total() - number of pixels; inputImage.elemSize() size of pixel (float = 3 channels, each pixel = 3 * 4 bajts) where reslut gives exact number of bajts to copy
        input_index = interpreter->inputs()[0]; // Give me first thing where indexes are input type as oppose to (0) which is give me the first thing
        expected_bytes = interpreter->tensor(input_index)->bytes;
        actual_bytes = cf.frame.total() * cf.frame.elemSize();


        if (actual_bytes == expected_bytes) {
            std::memcpy(input, cf.frame.data, actual_bytes);
        }  else {
            std::cerr << "Veličina frame-a se ne podudara s tensorom!" << std::endl;
        }

        if (interpreter->Invoke() != kTfLiteOk) {
            std::cerr << "Greška pri pokretanju modeal!" << std::endl;
            return;
        }

        // 2. DOHVAĆANJE SVA 4 IZLAZA
        // Tenzor 0: Lokacije [1, 10, 4] -> Float (SSD obično dekvantizira lokacije u float automatski)
        //float* locations = interpreter->typed_output_tensor<float>(0);
        // Tenzor 1: Klase [1, 10]
        classes = interpreter->typed_output_tensor<float>(1);
        // Tenzor 2: Sigurnost [1, 10]
        scores = interpreter->typed_output_tensor<float>(2);
        // Tenzor 3: Broj detekcija [1]
        num_detections_ptr = interpreter->typed_output_tensor<float>(3);
        num_detections = static_cast<int>(num_detections_ptr[0]);
        max_elements = std::min(3, num_detections);

        // 3. OBRADA REZULTATA
        for (int i = 0; i < max_elements; i++) {
            if (scores[i] > 0.5) { // Threshold 50%
                class_id = static_cast<int>(classes[i]);// Displays nubmer of the class with scores[i]; Casting because class gives float

                //std::cout << "Objekt: " << class_id << " [" << scores[i]*100 << "%] " << std::endl;
                
                cameraState[cf.cameraId].detectedType = class_id;
                cameraState[cf.cameraId].motionDetected = true;
            }
        }

        // Check  if camera is ACTIVE and object i detected than increas consecutiveHits
        if (cameraState[cf.cameraId].motionDetected && cameraState[cf.cameraId].mode == State::ACTIVE) {
            cameraState[cf.cameraId].consecutiveHits++;
            if (cameraState[cf.cameraId].consecutiveHits >= 3) {
                cameraState[cf.cameraId].mode = State::MONITORING;
                std::cout << "Počinjem snimati, prelazim u MONITORING " << std::endl;
            }
        } //else if (cameraState[cf.cameraId].consecutiveHits > 0) {
            //cameraState[cf.cameraId].consecutiveHits--;
        //}

        cameraState[cf.cameraId].motionDetected = false;
        

        auto t2 = std::chrono::high_resolution_clock::now();
        auto ms_int = std::chrono::duration_cast<std::chrono::milliseconds>(t2-t1);
        //std::cout << ms_int.count() << "ms\n";
        ///count_frames2();
    }
}