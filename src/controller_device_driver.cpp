#include "controller_device_driver.h"
#include "driverlog.h"
#include "vrmath.h"
#include <iostream>
#include <Windows.h>
#include "device_provider.h"
#include <thread>
#include <string>
#include <mutex>

// Define a mutex to protect logging and socket state
std::mutex log_mutex;

// Let's create some variables for strings used in getting settings.
static const char* my_controller_main_settings_section = "driver_simplecontroller";
static const char* my_controller_right_settings_section = "driver_simplecontroller_left_controller";
static const char* my_controller_left_settings_section = "driver_simplecontroller_right_controller";
static const char* my_controller_settings_key_model_number = "mycontroller_model_number";
static const char* my_controller_settings_key_serial_number = "mycontroller_serial_number";

MyControllerDeviceDriver::MyControllerDeviceDriver(vr::ETrackedControllerRole role)
{
    is_active_ = false;
    my_controller_role_ = role;

    char model_number[1024];
    vr::VRSettings()->GetString(my_controller_main_settings_section, my_controller_settings_key_model_number, model_number, sizeof(model_number));
    my_controller_model_number_ = model_number;

    char serial_number[1024];
    vr::VRSettings()->GetString(my_controller_role_ == vr::TrackedControllerRole_LeftHand ? my_controller_left_settings_section : my_controller_right_settings_section,
        my_controller_settings_key_serial_number, serial_number, sizeof(serial_number));
    my_controller_serial_number_ = serial_number;

    vr::VRServerDriverHost()->TrackedDeviceAdded(
        "simplecontroller_right",
        vr::TrackedDeviceClass_Controller,
        this
    );

    DriverLog("My Controller Model Number: %s", my_controller_model_number_.c_str());
    DriverLog("My Controller Serial Number: %s", my_controller_serial_number_.c_str());
}

vr::EVRInitError MyControllerDeviceDriver::Activate(uint32_t unObjectId)
{
    is_active_ = true;
    my_controller_index_ = unObjectId;

    vr::PropertyContainerHandle_t container = vr::VRProperties()->TrackedDeviceToPropertyContainer(my_controller_index_);

    vr::VRProperties()->SetStringProperty(container, vr::Prop_ModelNumber_String, my_controller_model_number_.c_str());
    vr::VRProperties()->SetInt32Property(container, vr::Prop_ControllerRoleHint_Int32, my_controller_role_);
    vr::VRProperties()->SetStringProperty(container, vr::Prop_InputProfilePath_String, "{simplecontroller}/input/mycontroller_profile.json");

    vr::VRDriverInput()->CreateBooleanComponent(container, "/input/a/touch", &input_handles_[MyComponent_a_touch]);
    vr::VRDriverInput()->CreateBooleanComponent(container, "/input/a/click", &input_handles_[MyComponent_a_click]);
    vr::VRDriverInput()->CreateScalarComponent(container, "/input/trigger/value", &input_handles_[MyComponent_trigger_value], vr::VRScalarType_Absolute, vr::VRScalarUnits_NormalizedOneSided);
    vr::VRDriverInput()->CreateBooleanComponent(container, "/input/trigger/click", &input_handles_[MyComponent_trigger_click]);
    vr::VRDriverInput()->CreateHapticComponent(container, "/output/haptic", &input_handles_[MyComponent_haptic]);

    my_pose_update_thread_ = std::thread(&MyControllerDeviceDriver::MyPoseUpdateThread, this);

    return vr::VRInitError_None;
}

void MyControllerDeviceDriver::MyPoseUpdateThread()
{
    // Initialize the Windows socket API
    WSADATA wsaData;
    int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        std::lock_guard<std::mutex> lock(log_mutex);
        std::cerr << "WSAStartup failed: " << iResult << std::endl;
        return;
    }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        std::lock_guard<std::mutex> lock(log_mutex);
        std::cerr << "Error creating socket: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return;
    }

    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(4120); // TCP port 4120
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1"); // Assuming localhost, modify as needed

    if (connect(sock, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        std::lock_guard<std::mutex> lock(log_mutex);
        std::cerr << "Connection failed: " << WSAGetLastError() << std::endl;
        closesocket(sock);
        WSACleanup();
        return;
    }

    char buffer[256];
    while (is_active_) {
        int bytes_received = recv(sock, buffer, sizeof(buffer), 0);
        if (bytes_received > 0) {
            std::string tcpData(buffer, bytes_received);
            std::lock_guard<std::mutex> lock(log_mutex);
            DriverLog("Received data: %s", tcpData.c_str());

            // We expect the format "0,0,0\nrotX,rotY,rotZ"
            // First, let's split the packet into two parts
            size_t newline_pos = tcpData.find('\n');
            if (newline_pos != std::string::npos) {
                std::string first_part = tcpData.substr(0, newline_pos);  // 0,0,0 part
                std::string second_part = tcpData.substr(newline_pos + 1); // rotX,rotY,rotZ part

                // Now let's parse the second part for the rotation data
                float rotX, rotY, rotZ;
                int parsed = sscanf_s(second_part.c_str(), "%f,%f,%f", &rotX, &rotY, &rotZ);

                if (parsed == 3) {
                    vr::DriverPose_t pose = GetPose();
                    pose.qRotation = vr::HmdQuaternion_t{ rotX, rotY, rotZ, 1.0f };
                    pose.vecPosition[0] = 0.0f;
                    pose.vecPosition[1] = 0.0f;
                    pose.vecPosition[2] = 0.0f;

                    vr::VRServerDriverHost()->TrackedDevicePoseUpdated(my_controller_index_, pose, sizeof(vr::DriverPose_t));

                    std::lock_guard<std::mutex> log_lock(log_mutex);
                    DriverLog("Pose updated: Rotation(%f, %f, %f)", rotX, rotY, rotZ);
                }
                else {
                    std::lock_guard<std::mutex> lock(log_mutex);
                    DriverLog("Failed to parse rotation data: %s", second_part.c_str());
                }
            }
        }
        else {
            std::lock_guard<std::mutex> lock(log_mutex);
            DriverLog("Failed to receive data.");
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    closesocket(sock);
    WSACleanup();
}


void MyControllerDeviceDriver::Deactivate()
{
    if (is_active_.exchange(false)) {
        my_pose_update_thread_.join();
    }
    my_controller_index_ = vr::k_unTrackedDeviceIndexInvalid;
}
