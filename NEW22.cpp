#include <iostream>
#include <thread>
#include <queue>
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>
#include <random>
#include <string>
#include <memory>
//система управления трафиком 
//ногопоточность генерация машин управление светофором и мониторинг

using namespace std;
using namespace chrono;

// машина структура 
struct Car {
    int id;
    int priority;       //будет триварианта
    string direction;

    Car(int _id, int _priority, string _dir)
        : id(_id), priority(_priority), direction(_dir) {
    }

    //перегружаем оператор для приоритетной очереди
    bool operator<(const Car& other) const {
        if (priority != other.priority)
            return priority > other.priority;  
        return id > other.id;  
    }
};

// имитируем светофор
class TrafficLight {
private:
    mutex mtx;//защита от гонки данных
    int green_duration;
    bool emergency_mode;//режи экстренной машины
    int light_id;

public:
    TrafficLight(int _id) : light_id(_id), green_duration(30), emergency_mode(false) {}

    int getId() const { return light_id; }

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
        cout << "[ALERT] Emergency mode activated at intersection #" << light_id << "!" << endl;
    }

    void deactivate_emergency() {
        lock_guard<mutex> lock(mtx);
        emergency_mode = false;
        green_duration = 30;
        cout << "[INFO] Emergency mode deactivated at intersection #" << light_id << endl;
    }

    bool is_emergency() {
        lock_guard<mutex> lock(mtx);
        return emergency_mode;
    }
};

// имитируем перекресток
class Intersection {
private:
    mutex mtx;
    priority_queue<Car> car_queue;  // USES operator< for sorting!
    atomic<int> cars_passed;//атомарный счечик машин
    int intersection_id;
    TrafficLight traffic_light;

public:
    Intersection(int _id) : intersection_id(_id), traffic_light(_id), cars_passed(0) {}

   
    int getId() const { return intersection_id; }
    TrafficLight& getLight() { return traffic_light; }
    //запрещаем копирование
    Intersection(const Intersection&) = delete;
    Intersection& operator=(const Intersection&) = delete;

    void add_car(const Car& car) {
        lock_guard<mutex> lock(mtx);
        car_queue.push(car);  // operator< is called HERE internally

        // запись добавленной машины
        if (!car_queue.empty() && car_queue.top().id == car.id) {
            cout << "[PRIORITY] Car #" << car.id << " (priority " << car.priority
                << ") is NOW at the front of intersection #" << intersection_id << " queue!" << endl;
        }
    }

    size_t get_queue_size() {
        lock_guard<mutex> lock(mtx);
        return car_queue.size();
    }

    int process_cars() {
        lock_guard<mutex> lock(mtx);

        if (car_queue.empty()) return 0;
        //сколько проедет на зеленый?????
        int green_time = traffic_light.get_green_duration();
        size_t capacity = static_cast<size_t>(green_time / 2);

        int passed = 0;

        // пропускаем в очереди по приоритету
        for (size_t i = 0; i < capacity && !car_queue.empty(); ++i) {
            Car car = car_queue.top();
            car_queue.pop();
            passed++;
            cars_passed++;

            // запись экстернного случая 
            if (car.priority == 1) {
                cout << "  [EMERGENCY] Car #" << car.id << " passed intersection #" << intersection_id
                    << " (preempting normal traffic)" << endl;
            }
        }

        // если пробка собирается
        if (!car_queue.empty() && car_queue.size() > 15) {
            cout << "[CONFLICT] Intersection #" << intersection_id << " still has " << car_queue.size()
                << " cars waiting after green phase!" << endl;
        }

        return passed;
    }

    int get_cars_passed() const {
        return cars_passed.load();
    }

    bool is_congested() {//void control trsffic
        lock_guard<mutex> lock(mtx);
        return car_queue.size() > 30;
    }

    void show_queue_status() {
        lock_guard<mutex> lock(mtx);
        if (car_queue.empty()) return;

        
        Car next = car_queue.top();
        cout << "  [QUEUE] Intersection #" << intersection_id << " next car: #" << next.id
            << " (priority " << next.priority << ")" << endl;
    }
};

     
class TrafficControlSystem {
private:
    static unique_ptr<TrafficControlSystem> instance;
    static mutex instance_mutex;

    vector<unique_ptr<Intersection>> intersections;
    mutex cout_mtx;
    atomic<bool> running;
    random_device rd;
    mt19937 gen;

    // Private constructor (Singleton)
    TrafficControlSystem() : running(true), gen(rd()) {
        for (int i = 1; i <= 10; ++i) {
            //создаёт умный указатель, который сам удалит объект
            intersections.push_back(make_unique<Intersection>(i));
        }
    }

public:
    TrafficControlSystem(const TrafficControlSystem&) = delete;
    TrafficControlSystem& operator=(const TrafficControlSystem&) = delete;

    static TrafficControlSystem* getInstance() {
        lock_guard<mutex> lock(instance_mutex);
        if (!instance) {
            instance = unique_ptr<TrafficControlSystem>(new TrafficControlSystem());
        }
        return instance.get();
    }

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
                    int priority;
                    int rand_val = priority_dist(gen);

                    if (rand_val <= 5) {
                        priority = 1;  // emergency
                    }
                    else if (rand_val <= 20) {
                        priority = 2;  // public transport
                    }
                    else {
                        priority = 3;  // regular
                    }

                    string direction = directions[rand() % 4];
                    Car car(car_id, priority, direction);
                    intersection->add_car(car);

                    
                    if (priority == 1) {
                        {
                            lock_guard<mutex> lock(cout_mtx);
                            cout << "\n[!!!] EMERGENCY VEHICLE #" << car_id
                                << " approaching intersection #" << intersection->getId() << "!" << endl;
                            cout << "[CONFLICT] Preempting normal traffic at intersection #"
                                << intersection->getId() << endl;
                        }
                        intersection->getLight().activate_emergency();

                        int intersection_id = intersection->getId();
                        thread emergency_thread([this, intersection_id]() {
                            this_thread::sleep_for(seconds(5));
                            for (auto& inter : intersections) {
                                if (inter->getId() == intersection_id) {
                                    inter->getLight().deactivate_emergency();
                                    break;
                                }
                            }
                            });
                        emergency_thread.detach();//работа потока в фоне 
                    }
                }
            }
        }
    }

    void control_traffic() {
        while (running.load()) {
            for (auto& intersection : intersections) {
                size_t queue_size = intersection->get_queue_size();
                intersection->getLight().adapt_green_time(static_cast<int>(queue_size));

                // запись пробок
                if (intersection->is_congested()) {
                    lock_guard<mutex> lock(cout_mtx);
                    cout << "\n[CONFLICT] Intersection #" << intersection->getId()
                        << " CONGESTED with " << queue_size << " cars waiting!" << endl;
                    cout << "[ACTION] Green light extended to 45 seconds" << endl;
                }

                int passed = intersection->process_cars();

                if (passed > 0 || intersection->get_queue_size() > 0) {
                    lock_guard<mutex> lock(cout_mtx);
                    cout << "[INTERSECTION #" << intersection->getId() << "] "
                        << passed << " cars passed | Queue: " << intersection->get_queue_size()
                        << " | Green: " << intersection->getLight().get_green_duration() << "s";
                    if (intersection->getLight().is_emergency()) {
                        cout << " | EMERGENCY MODE";
                    }
                    cout << endl;

                    // Show next car in queue (demonstrates operator<)
                    intersection->show_queue_status();
                }
            }
            this_thread::sleep_for(seconds(3));
        }
    }

    void monitor() {
        while (running.load()) {
            this_thread::sleep_for(seconds(10));

            lock_guard<mutex> lock(cout_mtx);
            cout << "\n========== SYSTEM STATUS REPORT ==========" << endl;

            size_t total_cars = 0;
            int total_passed = 0;
            int congested = 0;

            for (auto& intersection : intersections) {
                size_t queue = intersection->get_queue_size();
                int passed = intersection->get_cars_passed();
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

    void run(int duration_seconds = 45) {
        cout << "\n========== TRAFFIC SYSTEM STARTED ==========" << endl;
        cout << "Simulating 10 intersections with adaptive traffic lights" << endl;
        cout << "operator< is used by priority_queue for car sorting" << endl;
        cout << "=============================================\n" << endl;

        thread traffic_thread(&TrafficControlSystem::generate_traffic, this);
        thread control_thread(&TrafficControlSystem::control_traffic, this);
        thread monitor_thread(&TrafficControlSystem::monitor, this);

        this_thread::sleep_for(seconds(duration_seconds));
        running.store(false);

        traffic_thread.join();
        control_thread.join();
        monitor_thread.join();

        cout << "\n========== FINAL STATISTICS ==========" << endl;
        int total_passed = 0;
        for (auto& intersection : intersections) {
            int passed = intersection->get_cars_passed();
            total_passed += passed;
            cout << "Intersection #" << intersection->getId() << ": " << passed << " cars" << endl;
        }
        cout << "----------------------------------------" << endl;
        cout << "TOTAL CARS PROCESSED: " << total_passed << endl;
        cout << "========================================" << endl;
    }
};

// один на всю лабу
unique_ptr<TrafficControlSystem> TrafficControlSystem::instance = nullptr;
mutex TrafficControlSystem::instance_mutex;


int main() {
    try {
        TrafficControlSystem* traffic = TrafficControlSystem::getInstance();
        traffic->run(45);
        cout << "\nSimulation completed successfully!" << endl;
    }
    catch (const exception& e) {
        cerr << "Error: " << e.what() << endl;
        return 1;
    }
    return 0;
}
