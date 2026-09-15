#include "../header/functions.h"
#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

std::string get_full_Path(int id) {
    std::string type = "Ostalo";
    if (cameraState[id].detectedType == 0) type = "Osoba"; // YOLO/COCO standard
    else if (cameraState[id].detectedType  == 18) type = "Koza"; 

    auto now = std::chrono::system_clock::now();
    auto t_c = std::chrono::system_clock::to_time_t(now);
    std::tm* now_tm = std::localtime(&t_c);

    std::stringstream ss_date, ss_filename;
    ss_date << std::put_time(now_tm, "%Y-%m-%d");
    ss_filename << std::put_time(now_tm, "%H-%M-%S") << "_" << type << ".mkv";

    std::string folder = "/mnt/ssd/Camera" + std::to_string(id+1) + "/" + type + "/" + ss_date.str();
    fs::create_directories(folder);
    return folder + "/" + ss_filename.str();
}