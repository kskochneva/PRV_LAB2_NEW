#include <iostream>
#include <thread>
#include <queue>
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>
#include <random>
#include <string>
#include <climits>
#include <memory>

using namespace std;
using namespace chrono;

// ==================== CAR STRUCTURE ====================
struct Car {
    int id;
    int priority;       // 1 - emergency, 2 - public transport, 3 - regular
    string direction;

    Car(int _id, int _priority, string _dir)
        : id(_id), priority(_priority), direction(_dir) {
    }

    // Добавляем конструктор копирования для безопасности
    Car(const Car& other)
        : id(other.id), priority(other.priority), direction(other.direction) {
    }

    // Оператор присваивания
    Car& operator=(const Car& other) {
        if (this != &other) {
            id = other.id;
            priority = other.priority;
            direction = other.direction;
        }
        return *this;
    }
};

// ==================== TRAFFIC LIGHT ====================
class TrafficLight {
private:
    mutex mtx;
    int green_duration;
    bool emergency_mode;

public:
    int id;

    TrafficLight(int _id) : id(_id), green_duration(30), emergency_mode(false) {}

    void adapt_green_time(int car_count) {
        lock_guard<mutex> lock(mtx);
        if (car_count > 20) {
            green_duration = 45;
        }
        else if (car_count > 10) {
            green_duration = 35;
        }
        else {
            green_duration = 25;
        }
    }

    int get_green_duration() {
        lock_guard<mutex> lock(mtx);
        return green_duration;
    }

    void activate_emergency() {
        lock_guard<mutex> lock(mtx);
        emergency_mode = true;
        green_duration = 10;
        cout << "[ALERT] Emergency mode activated at intersection #" << id << "!" << endl;
    }

    void deactivate_emergency() {
        lock_guard<mutex> lock(mtx);
        emergency_mode = false;
        green_duration = 30;
        cout << "[INFO] Emergency mode deactivated at intersection #" << id << endl;
    }

    bool is_emergency() {
        lock_guard<mutex> lock(mtx);
        return emergency_mode;
    }
};

// ==================== INTERSECTION ====================
class Intersection {
private:
    mutex mtx;
    queue<Car> car_queue;
    atomic<int> cars_passed;

public:
    int id;
    TrafficLight light;

    Intersection(int _id) : id(_id), light(_id), cars_passed(0) {}

    // Запрещаем копирование для безопасности
    Intersection(const Intersection&) = delete;
    Intersection& operator=(const Intersection&) = delete;

    // Разрешаем перемещение
    Intersection(Intersection&& other) noexcept
        : id(other.id), light(other.id), cars_passed(other.cars_passed.load()) {
        lock_guard<mutex> lock(other.mtx);
        car_queue = move(other.car_queue);
    }

    void add_car(const Car& car) {
        lock_guard<mutex> lock(mtx);
        car_queue.push(car);
    }

    size_t get_queue_size() {
        lock_guard<mutex> lock(mtx);
        return car_queue.size();
    }

    int process_cars() {
        lock_guard<mutex> lock(mtx);

        // Separate cars by priority
        vector<Car> emergency_cars;
        vector<Car> regular_cars;

        while (!car_queue.empty()) {
            Car car = car_queue.front();
            car_queue.pop();

            if (car.priority == 1) {
                emergency_cars.push_back(car);
            }
            else {
                regular_cars.push_back(car);
            }
        }

        // Emergency cars go first
        vector<Car> all_cars;
        all_cars.reserve(emergency_cars.size() + regular_cars.size());

        for (auto& car : emergency_cars) {
            all_cars.push_back(car);
        }
        for (auto& car : regular_cars) {
            all_cars.push_back(car);
        }

        // Calculate how many cars can pass
        int green_time = light.get_green_duration();
        size_t capacity = static_cast<size_t>(green_time / 2);  // ~2 seconds per car

        size_t passed = 0;
        for (size_t i = 0; i < all_cars.size() && i < capacity; ++i) {
            passed++;
            cars_passed++;

            if (all_cars[i].priority == 1) {
                cout << "  >> Emergency car #" << all_cars[i].id
                    << " passed intersection #" << id << endl;
            }
        }

        // Put remaining cars back in queue
        for (size_t i = capacity; i < all_cars.size(); ++i) {
            car_queue.push(all_cars[i]);
        }

        return static_cast<int>(passed);
    }

    int get_cars_passed() const {
        return cars_passed.load();
    }

    bool is_congested() {
        lock_guard<mutex> lock(mtx);
        return car_queue.size() > 30;
    }
};

// ==================== TRAFFIC CONTROL SYSTEM ====================
class TrafficControlSystem {
private:
    vector<Intersection> intersections;
    mutex cout_mtx;
    atomic<bool> running;
    random_device rd;
    mt19937 gen;

public:
    TrafficControlSystem() : running(true), gen(rd()) {
        // Create 10 intersections
        intersections.reserve(10);
        for (int i = 1; i <= 10; ++i) {
            intersections.emplace_back(i);
        }
    }

    // Запрещаем копирование
    TrafficControlSystem(const TrafficControlSystem&) = delete;
    TrafficControlSystem& operator=(const TrafficControlSystem&) = delete;

    // Generate random cars at intersections
    void generate_traffic() {
        uniform_int_distribution<> cars_per_cycle(0, 12);
        uniform_int_distribution<> priority_dist(1, 100);
        vector<string> directions = { "North", "South", "East", "West" };

        int car_id = 0;

        while (running.load()) {
            this_thread::sleep_for(seconds(2));

            for (auto& intersection : intersections) {
                int num_cars = cars_per_cycle(gen);

                for (int i = 0; i < num_cars; ++i) {
                    ++car_id;

                    // Determine priority
                    int priority;
                    int rand_val = priority_dist(gen);

                    if (rand_val <= 5) {          // 5% emergency
                        priority = 1;
                    }
                    else if (rand_val <= 20) {   // 15% public transport
                        priority = 2;
                    }
                    else {                        // 80% regular
                        priority = 3;
                    }

                    string direction = directions[rand() % 4];
                    Car car(car_id, priority, direction);
                    intersection.add_car(car);

                    // Emergency vehicle handling
                    if (priority == 1) {
                        {
                            lock_guard<mutex> lock(cout_mtx);
                            cout << "\n[!!!] EMERGENCY VEHICLE #" << car_id
                                << " approaching intersection #" << intersection.id << "!" << endl;
                        }
                        intersection.light.activate_emergency();

                        // Используем shared_ptr для безопасной работы с потоками
                        // Создаем копию id, так как intersection может измениться
                        int intersection_id = intersection.id;

                        thread emergency_thread([this, intersection_id]() {
                            this_thread::sleep_for(seconds(5));
                            // Находим нужный перекресток по id
                            for (auto& inter : intersections) {
                                if (inter.id == intersection_id) {
                                    inter.light.deactivate_emergency();
                                    break;
                                }
                            }
                            });
                        emergency_thread.detach();
                    }
                }
            }
        }
    }

    // Control traffic lights based on traffic
    void control_traffic() {
        while (running.load()) {
            for (auto& intersection : intersections) {
                size_t queue_size = intersection.get_queue_size();

                // Adapt green light duration
                intersection.light.adapt_green_time(static_cast<int>(queue_size));

                // Check for congestion
                if (intersection.is_congested()) {
                    {
                        lock_guard<mutex> lock(cout_mtx);
                        cout << "\n[CONGESTION] Intersection #" << intersection.id
                            << " has " << queue_size << " cars waiting!" << endl;
                        cout << "[ACTION] Increasing green light duration to 45 seconds" << endl;
                    }
                }

                // Process cars at this intersection
                int passed = intersection.process_cars();

                if (passed > 0) {
                    lock_guard<mutex> lock(cout_mtx);
                    cout << "[INTERSECTION #" << intersection.id << "] "
                        << passed << " cars passed | Queue: " << intersection.get_queue_size()
                        << " | Green: " << intersection.light.get_green_duration() << "s";
                    if (intersection.light.is_emergency()) {
                        cout << " | EMERGENCY MODE";
                    }
                    cout << endl;
                }
            }

            this_thread::sleep_for(seconds(3));
        }
    }

    // Monitor system status
    void monitor() {
        while (running.load()) {
            this_thread::sleep_for(seconds(10));

            lock_guard<mutex> lock(cout_mtx);
            cout << "\n========== SYSTEM STATUS REPORT ==========" << endl;

            size_t total_cars = 0;
            int total_passed = 0;
            int congested = 0;

            for (auto& intersection : intersections) {
                size_t queue = intersection.get_queue_size();
                int passed = intersection.get_cars_passed();
                total_cars += queue;
                total_passed += passed;
                if (queue > 20) ++congested;
            }

            cout << "Total cars waiting: " << total_cars << endl;
            cout << "Total cars processed: " << total_passed << endl;
            cout << "Congested intersections: " << congested << endl;
            cout << "===========================================" << endl;
        }
    }

    void run(int duration_seconds = 40) {
        cout << "\n========== TRAFFIC SYSTEM STARTED ==========" << endl;
        cout << "Simulating 10 intersections with adaptive traffic lights" << endl;
        cout << "=============================================\n" << endl;

        // Start threads
        thread traffic_thread(&TrafficControlSystem::generate_traffic, this);
        thread control_thread(&TrafficControlSystem::control_traffic, this);
        thread monitor_thread(&TrafficControlSystem::monitor, this);

        // Run for specified duration
        this_thread::sleep_for(seconds(duration_seconds));

        // Stop system
        running.store(false);

        // Wait for threads to finish
        traffic_thread.join();
        control_thread.join();
        monitor_thread.join();

        // Print final statistics
        cout << "\n========== FINAL STATISTICS ==========" << endl;
        int total_passed = 0;
        for (auto& intersection : intersections) {
            int passed = intersection.get_cars_passed();
            total_passed += passed;
            cout << "Intersection #" << intersection.id << ": " << passed << " cars" << endl;
        }
        cout << "----------------------------------------" << endl;
        cout << "TOTAL CARS PROCESSED: " << total_passed << endl;
        cout << "========================================" << endl;
    }
};

// ==================== MAIN FUNCTION ====================
int main() {
    try {
        TrafficControlSystem traffic;
        traffic.run(45);  // Run simulation for 45 seconds
        cout << "\nSimulation completed successfully!" << endl;
    }
    catch (const exception& e) {
        cerr << "Error: " << e.what() << endl;
        return 1;
    }

    return 0;
}