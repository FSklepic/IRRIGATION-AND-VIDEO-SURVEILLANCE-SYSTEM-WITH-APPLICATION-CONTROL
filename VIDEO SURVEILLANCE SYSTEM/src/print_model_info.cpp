#include "../header/functions.h"
#include "tensorflow/lite/interpreter.h"

#include <iostream>

// Print data for yolo8n_full_integer_quant.tflite modele
/*
void print_model_info(std::unique_ptr<tflite::Interpreter>& interpreter) {
    
    auto output_dims = interpreter->output_tensor(0)->dims;
    std::cout << "size: "<< output_dims->size << " Output shape: " << output_dims->data[0] << " x " << output_dims->data[1] << " x " << output_dims->data[2] << std::endl;
    

    // Dohvati indeks ulaznog tenzora (obično je 0)
    
    int input = interpreter->inputs()[0];
    TfLiteIntArray* dims = interpreter->tensor(input)->dims;

    // Dims struktura: [Batch, Visina, Širina, Kanali]
    int batch    = dims->data[0];
    int height   = dims->data[1];
    int width    = dims->data[2];
    int channels = dims->data[3];

    std::cout << "--- MODEL INFO ---" << std::endl;
    std::cout << "Potrebna širina:  " << width << std::endl;
    std::cout << "Potrebna visina:  " << height << std::endl;
    std::cout << "Broj kanala (3=RGB): " << channels << std::endl;
    std::cout << "-------------------" << std::endl;

    TfLiteType type = interpreter->tensor(input)->type;
    if (type == kTfLiteUInt8) {
        std:: cout << "Model očekuje Int8 (0-255)" << std::endl;
    } else if (type == kTfLiteFloat32) {
        std:: cout << "Model očekuje Float32 (Normalizacija 0.0 - 1.0)" << std::endl;
    } else if (type == kTfLiteInt8) {
        std:: cout << "Model očekuje UInt8 (-128-127)" << std::endl;
    }
} 
*/

// Print data for MobilleNet.tflite modele
void print_model_info(std::unique_ptr<tflite::Interpreter>& interpreter) {
    // 1. INPUT INFO
    int input = interpreter->inputs()[0];
    TfLiteIntArray* dims = interpreter->tensor(input)->dims;

    int height   = dims->data[1];
    int width    = dims->data[2];
    int channels = dims->data[3];

    std::cout << "\n--- INPUT INFO ---" << std::endl;
    std::cout << "Rezolucija: " << width << "x" << height << std::endl;
    std::cout << "Kanali: " << channels << std::endl;

    TfLiteType type = interpreter->tensor(input)->type;
    if (type == kTfLiteUInt8) std::cout << "Tip: UInt8 (Asymmetric Quantized)" << std::endl;
    else if (type == kTfLiteInt8) std::cout << "Tip: Int8 (Symmetric Quantized)" << std::endl;
    else if (type == kTfLiteFloat32) std::cout << "Tip: Float32" << std::endl;

    // 2. OUTPUT INFO (MobileNet usually has [1, 1001])
    int output = interpreter->outputs()[0];
    TfLiteIntArray* out_dims = interpreter->tensor(output)->dims;
    
    std::cout << "--- OUTPUT INFO ---" << std::endl;
    std::cout << "Output shape: ";
    for(int i=0; i < out_dims->size; i++) std::cout << out_dims->data[i] << " ";
    std::cout << "\n-------------------\n" << std::endl;
}