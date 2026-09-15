#include "../header/functions.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <signal.h>
#include <thread>

// Handles incoming TCP commands to control camera states and live streams.
void handle_communication(int id, std::atomic<bool>& keepRunning, std::atomic<bool>& turnOffCamera) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(id, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

    int serverSocket;
    struct sockaddr_in serverAddress;
    int opt = 1;
    
    // Create a TCP stream socket
    if ((serverSocket = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket failed");
        return;
    }

    // Forcefully attaching socket to the port 8895 to prevent "Address already in use" errors
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));

    // Set the socket to non-blocking mode so the loop doesn't hang on accept()
    fcntl(serverSocket, F_SETFL, O_NONBLOCK);

    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(8895);
    serverAddress.sin_addr.s_addr = INADDR_ANY;

    // Bind the socket to the network interface and port
    if (bind(serverSocket, (struct sockaddr *)&serverAddress, sizeof(serverAddress)) < 0) {
        perror("Bind failed");
        return;
    }

    // Start listening for incoming connections (queue limit of 5)
    if (listen(serverSocket, 5) < 0) {
        perror("Listen failed");
        return;
    }

    std::cout << "[Server] Čekam naredbe na portu 8895..." << std::endl;

    char buffer[1024] = {0};

    while (keepRunning) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(serverSocket, &read_fds);

        // Define timeout for select()
        struct timeval timeout;
        timeout.tv_sec = 0; 
        timeout.tv_usec = 500000; // 0.5s timeout for select

        // Monitor the socket for activity
        int activity = select(serverSocket + 1, &read_fds, NULL, NULL, &timeout);

        if (activity > 0 && FD_ISSET(serverSocket, &read_fds)) {
            struct sockaddr_in client_addr;
            socklen_t addrlen = sizeof(client_addr);
            int clientSocket = accept(serverSocket, (struct sockaddr *)&client_addr, &addrlen);
            
            // Set a receive timeout on the client socket to handle slow clients
            if (clientSocket >= 0) {
                struct timeval timeout_timer;
                timeout_timer.tv_sec = 0; 
                timeout_timer.tv_usec = 500000;
                setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, &timeout_timer, sizeof(timeout_timer));

                memset(buffer, 0, sizeof(buffer));
                int valread = recv(clientSocket, buffer, sizeof(buffer), 0);
            
                if (valread > 0) {
                    std::string msg(buffer, valread);
                    
                    // --- COMMAND PROCESSING ---
                    // TURN_OFF: Globally disable camera logic and stop active streams
                    if (msg.find("TURN_OFF") != std::string::npos) {
                        std::cout << "[Sustav] Zaustavljam sve procese prije spavanja..." << std::endl;
                        turnOffCamera = true;
                        std::cout << "[Sustav] ISKLJUČUJEM RAD KAMERA." << std::endl;
                        // Gasimo streamove odmah ako rade
                        stop_live_stream(0); 
                        stop_live_stream(1); 
                        stop_live_stream(2); 
                    } 
                    // TURN_ON: Re-enable camera logic
                    else if (msg.find("TURN_ON") != std::string::npos) {
                        std::cout << "[Sustav] UKLJUČUJEM RAD KAMERA." << std::endl;
                        turnOffCamera = false;
                    }
                    // Only process START/STOP if the system isn't globally TURNED_OFF
                    else if (!turnOffCamera.load()) { 
                        if (msg.find("START") != std::string::npos) {
                            std::cout << "[Server] Primljeno: START. Palim feed." << std::endl;
                            start_live_stream(0);
                            start_live_stream(1);
                            start_live_stream(2);
                        } else if (msg.find("STOP") != std::string::npos) {
                            std::cout << "[Server] Primljeno: STOP. Gasim feed." << std::endl;
                            stop_live_stream(0);
                            stop_live_stream(1); 
                            stop_live_stream(2); 
                        }
                    } else {
                        std::cout << "[Server] Sustav je ugašen. Ignoriram START/STOP." << std::endl;
                    }
                }
                close(clientSocket);
            }
        }
        
        // If system is disabled, sleep slightly longer to save power/CPU
        if (turnOffCamera.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
    close(serverSocket);
}

// --- Other functions ---

void start_live_stream(int id) {
    if (cameraState[id].isStreaming) return;
    pid_t pid = fork(); // Create child process
    if (pid == 0) {
        // CHILD PROCESS: Execute the FFmpeg command
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        std::string streamUrl = "rtsp://localhost:8554/live" + std::to_string(id);
        execlp("ffmpeg", "ffmpeg", "-hide_banner", "-loglevel", "error",
               "-rtsp_transport", "tcp", 
               "-i", cameraState[id].subStreamrtsp_url.c_str(),
               "-vcodec", "copy", "-acodec", "copy", 
               "-f", "rtsp", streamUrl.c_str(), NULL);
        exit(1);
    } else if (pid > 0) { // PARENT PROCESS: Store the PID to manage it later
        cameraState[id].liveStreamPid = pid;
        cameraState[id].isStreaming = true;
        std::cout << "[CAM " << id << "] LIVE STREAM POKRENUT" << std::endl;
    }
}

void stop_live_stream(int id) {
    if (cameraState[id].isStreaming && cameraState[id].liveStreamPid > 0) {
        kill(cameraState[id].liveStreamPid, SIGTERM);
        waitpid(cameraState[id].liveStreamPid, NULL, 0);
        cameraState[id].isStreaming = false;
        cameraState[id].liveStreamPid = -1;
        std::cout << "[CAM " << id << "] LIVE STREAM UGAŠEN" << std::endl;
    }
}

void send_pushNotification(std::string message) {
    std::vector<std::string> device = {"100.101.253.100"};
    for (const auto& ip : device) {
        //std::cout << message << "početak"<< std::endl;
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) continue;
        struct sockaddr_in serv_addr;
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(9999);
        inet_pton(AF_INET, ip.c_str(), &serv_addr.sin_addr);
        //std::cout << message << "sredina"<< std::endl;
        struct timeval tv;
        tv.tv_sec = 5;
        tv.tv_usec = 0; 
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof tv);
        //std::cout << message << "kraj sredine"<< std::endl;
        if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == 0) {
            //std::cout << message << "kraj"<< std::endl;
            send(sock, message.c_str(), message.length(), 0);
        } else {
            std::cout << "Spajanje propalo: " << strerror(errno) << std::endl;
        }
        close(sock);
    }
}