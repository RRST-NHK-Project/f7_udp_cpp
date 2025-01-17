#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/joy.hpp"
#include "std_msgs/msg/int32_multi_array.hpp"
#include "geometry_msgs/msg/twist.hpp"
#include "std_msgs/msg/string.hpp"
#include <chrono>
#include <thread>
#include <sstream>
#include <iostream>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <vector>

std::vector<int> data = {0, 0, 0, 0, 0, 0, -1, -1, -1};

int duty_max = 90;
double sp_yaw = 0.1;
double deadzone = 0.3;

int roller_speed_dribble_ab = 30;
int roller_speed_dribble_cd = 30;
int roller_speed_shoot_ab = 50;
int roller_speed_shoot_cd = 50;
int roller_speed_reload = 10;

class UDP {
public:
    UDP() {
        try {
            udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
            if (udp_socket < 0) {
                throw std::runtime_error("Failed to create socket.");
            }

            memset(&dst_addr, 0, sizeof(dst_addr));
            dst_addr.sin_family = AF_INET;
            dst_addr.sin_port = htons(5000);
            if (inet_pton(AF_INET, "192.168.8.216", &dst_addr.sin_addr) <= 0) {
                throw std::runtime_error("Invalid address.");
            }

            memset(&src_addr, 0, sizeof(src_addr));
            src_addr.sin_family = AF_INET;
            src_addr.sin_port = 0;
        } catch (const std::exception& e) {
            std::cerr << "UDP initialization error: " << e.what() << std::endl;
        }
    }

    void send() {
        for (auto& value : data) {
            if (value > duty_max) {
                value = duty_max;
            } else if (value < -duty_max) {
                value = -duty_max;
            }
        }

        std::ostringstream oss;
        for (size_t i = 1; i < data.size(); ++i) {
            oss << data[i];
            if (i != data.size() - 1) {
                oss << ",";
            }
        }

        std::string str_data = oss.str();
        if (sendto(udp_socket, str_data.c_str(), str_data.length(), 0, (struct sockaddr*)&dst_addr, sizeof(dst_addr)) < 0) {
            std::cerr << "Failed to send data." << std::endl;
        }
    }

private:
    int udp_socket;
    struct sockaddr_in src_addr, dst_addr;
};

class Action {
public:
    static bool ready_for_shoot;

    static void ready_for_shoot_action(UDP& udp) {
        std::cout << "Ready for Shooting..." << std::endl;
        data[6] = 1;
        data[8] = 1;
        data[1] = roller_speed_reload;
        data[2] = roller_speed_reload;
        data[3] = -roller_speed_reload;
        data[4] = -roller_speed_reload;
        udp.send();
        std::this_thread::sleep_for(std::chrono::seconds(1));
        data[1] = 0;
        data[2] = 0;
        data[3] = 0;
        data[4] = 0;
        udp.send();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        data[1] = -roller_speed_shoot_ab;
        data[2] = -roller_speed_shoot_ab;
        data[3] = roller_speed_shoot_cd;
        data[4] = roller_speed_shoot_cd;
        udp.send();
        ready_for_shoot = true;
        std::cout << "Done." << std::endl;
    }

    static void shoot_action(UDP& udp) {
        std::cout << "Shooting..." << std::endl;
        data[7] = 1;
        udp.send();
        ready_for_shoot = false;
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::cout << "Ready for Retraction..." << std::endl;
        data[7] = -1;
        udp.send();
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::cout << "Retracting..." << std::endl;
        data[6] = -1;
        data[8] = -1;
        data[1] = 0;
        data[2] = 0;
        data[3] = 0;
        data[4] = 0;
        udp.send();
        std::cout << "Done." << std::endl;
    }

    static void dribble_action(UDP& udp) {
        std::cout << "Dribbling..." << std::endl;
        data[8] = 1;
        data[1] = roller_speed_dribble_ab;
        data[2] = roller_speed_dribble_ab;
        data[3] = -roller_speed_dribble_cd;
        data[4] = -roller_speed_dribble_cd;
        udp.send();
        std::this_thread::sleep_for(std::chrono::seconds(6));
        data[8] = -1;
        data[1] = 0;
        data[2] = 0;
        data[3] = 0;
        data[4] = 0;
        udp.send();
        std::cout << "Done." << std::endl;
    }
};

bool Action::ready_for_shoot = false;

class Listener : public rclcpp::Node {
public:
    Listener() : Node("nhk25_mr"), udp_() {
        subscription_ = this->create_subscription<sensor_msgs::msg::Joy>(
            "joy", 10, std::bind(&Listener::listener_callback, this, std::placeholders::_1));
        RCLCPP_INFO(this->get_logger(), "NHK2025 MR");
    }

private:
    void listener_callback(const sensor_msgs::msg::Joy::SharedPtr msg) {
        bool CIRCLE = msg->buttons[1];
        bool TRIANGLE = msg->buttons[2];

        if (CIRCLE) {
            Action::ready_for_shoot_action(udp_);
            std::this_thread::sleep_for(std::chrono::seconds(3));
        }

        if (Action::ready_for_shoot) {
            Action::shoot_action(udp_);
        }

        if (TRIANGLE && !Action::ready_for_shoot) {
            Action::dribble_action(udp_);
        }

        udp_.send();
    }

    rclcpp::Subscription<sensor_msgs::msg::Joy>::SharedPtr subscription_;
    UDP udp_;
};

int main(int argc, char **argv) {
    rclcpp::init(argc, argv);

    rclcpp::executors::SingleThreadedExecutor exec;
    auto listener = std::make_shared<Listener>();
    exec.add_node(listener);

    exec.spin();

    rclcpp::shutdown();
    return 0;
}
