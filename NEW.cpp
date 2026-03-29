#include <iostream>
#include <thread>
#include <queue>
#include <random>
#include <semaphore>
#include <chrono>
#include <vector>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <memory>      // для unique_ptr, make_unique
#include <cstddef>     // для size_t

using namespace std;
using namespace chrono;

// ==================== СТРУКТУРА ЗАДАЧИ ====================
struct ClusterTask {
    int id;                 // ID задачи
    int priority;           // 1 - высокая, 2 - средняя, 3 - низкая
    int duration;           // время выполнения (мс)
    int required_power;     // требуемая мощность (1-3)
    bool is_critical;       // критическая задача?

    bool operator<(const ClusterTask& other) const {
        // Чем меньше priority, тем выше приоритет
        if (priority != other.priority)
            return priority > other.priority;
        // Если приоритет одинаковый, критическая задача важнее
        if (is_critical != other.is_critical)
            return !is_critical;
        return false;
    }
};

// ==================== КЛАСС СЕРВЕРА ====================
class Server {
public:
    int id;
    int max_power;
    int current_load;
    bool active;
    mutex mtx;

    Server(int _id, int _max_power = 3) : id(_id), max_power(_max_power),
        current_load(0), active(true) {
    }

    // Запрещаем копирование
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    // Разрешаем перемещение
    Server(Server&& other) noexcept
        : id(other.id),
        max_power(other.max_power),
        current_load(other.current_load),
        active(other.active) {
        // mutex не нужно перемещать
    }

    Server& operator=(Server&& other) noexcept {
        if (this != &other) {
            id = other.id;
            max_power = other.max_power;
            current_load = other.current_load;
            active = other.active;
        }
        return *this;
    }

    bool can_handle_task(const ClusterTask& task) {
        return active && (current_load + task.required_power <= max_power);
    }

    void add_task(const ClusterTask& task) {
        current_load += task.required_power;
    }

    void remove_task(const ClusterTask& task) {
        current_load -= task.required_power;
        if (current_load < 0) current_load = 0;
    }

    double get_load_percent() {
        return (double)current_load / max_power * 100.0;
    }
};

// ==================== КЛАСС КЛАСТЕРНОЙ СИСТЕМЫ ====================
class ClusterSystem {
private:
    vector<unique_ptr<Server>> servers;
    priority_queue<ClusterTask> task_queue;
    mutex queue_mtx;
    mutex output_mtx;
    condition_variable cv;
    atomic<bool> running{ true };
    atomic<int> active_servers{ 0 };
    atomic<int> total_tasks_processed{ 0 };

    random_device rd;
    mt19937 gen;
    uniform_int_distribution<> duration_dist{ 500, 5000 };
    uniform_int_distribution<> power_dist{ 1, 3 };

public:
    ClusterSystem(int initial_servers = 5) : gen(rd()) {
        for (int i = 0; i < initial_servers; i++) {
            servers.push_back(make_unique<Server>(i + 1));
        }
        active_servers = initial_servers;
    }

    // Добавление задачи в очередь
    void add_task(int id, int priority, bool is_critical = false) {
        ClusterTask task;
        task.id = id;
        task.priority = priority;
        task.duration = duration_dist(gen);
        task.required_power = power_dist(gen);
        task.is_critical = is_critical;

        {
            lock_guard<mutex> lock(queue_mtx);
            task_queue.push(task);
        }

        {
            lock_guard<mutex> lock(output_mtx);
            cout << "[Added] Task #" << id
                << " | Priority: " << priority
                << " | Power: " << task.required_power
                << " | Time: " << task.duration / 1000.0 << " сек";
            if (is_critical) cout << " (CRITICAL!)";
            cout << endl;
        }

        cv.notify_one();
    }

    // Запуск дополнительного сервера при перегрузке
    void add_backup_server() {
        lock_guard<mutex> lock(output_mtx);
        int new_id = servers.size() + 1;
        servers.push_back(make_unique<Server>(new_id));
        active_servers++;
        cout << "\n[!!!] Reloading extra server #" << new_id
            << " (total servers: " << active_servers << ")\n" << endl;
    }

    // Проверка нагрузки на кластер
    void check_cluster_load() {
        double total_load = 0;
        int active_count = 0;

        for (auto& server : servers) {
            if (server->active) {
                total_load += server->get_load_percent();
                active_count++;
            }
        }

        if (active_count > 0) {
            double avg_load = total_load / active_count;

            if (avg_load > 80 && active_servers == (int)servers.size()) {
                add_backup_server();
            }
        }
    }

    // Функция обработки задач на сервере
    void server_worker(int server_idx) {
        Server& server = *servers[server_idx];

        while (running) {
            ClusterTask current_task;
            bool has_task = false;

            {
                unique_lock<mutex> lock(queue_mtx);
                cv.wait(lock, [this] {
                    return !task_queue.empty() || !running;
                    });

                if (!running && task_queue.empty()) break;

                if (!task_queue.empty()) {
                    ClusterTask temp = task_queue.top();

                    if (server.can_handle_task(temp)) {
                        current_task = temp;
                        task_queue.pop();
                        has_task = true;
                        server.add_task(current_task);
                    }
                    else {
                        bool handled = false;
                        for (auto& other : servers) {
                            if (other.get() != &server && other->can_handle_task(temp)) {
                                current_task = temp;
                                task_queue.pop();
                                has_task = true;
                                other->add_task(current_task);
                                {
                                    lock_guard<mutex> lock(output_mtx);
                                    cout << "CHANGE task" << current_task.id
                                        << " changed from server  #" << server.id
                                        << " to server #" << other->id << endl;
                                }
                                break;
                            }
                        }
                        if (!handled) {
                            continue;
                        }
                    }
                }
            }

            if (has_task) {
                {
                    lock_guard<mutex> lock(output_mtx);
                    cout << "doing srver #" << server.id
                        << " | task#" << current_task.id
                        << " (priority: " << current_task.priority;
                    if (current_task.is_critical) cout << ", Critical";
                    cout << ") | Time: " << current_task.duration / 1000.0 << " sec" << endl;
                }

                this_thread::sleep_for(milliseconds(current_task.duration));

                server.remove_task(current_task);
                total_tasks_processed++;

                {
                    lock_guard<mutex> lock(output_mtx);
                    cout << "Ready server #" << server.id
                        << " task done #" << current_task.id
                        << " | Loading server: " << server.get_load_percent() << "%" << endl;
                }

                check_cluster_load();
            }
        }
    }

    // Запуск кластерной системы
    void run(int num_tasks) {
        vector<thread> workers;

        for (size_t i = 0; i < servers.size(); i++) {
            workers.emplace_back(&ClusterSystem::server_worker, this, i);
        }

        for (int i = 1; i <= num_tasks; i++) {
            int priority;
            bool is_critical = false;

            if (i % 5 == 0) {
                priority = 1;
                is_critical = (i % 10 == 0);
            }
            else if (i % 3 == 0) {
                priority = 2;
            }
            else {
                priority = 3;
            }

            add_task(i, priority, is_critical);
            this_thread::sleep_for(milliseconds(500));
        }

        this_thread::sleep_for(seconds(10));
        running = false;
        cv.notify_all();

        for (auto& t : workers) {
            t.join();
        }

        {
            lock_guard<mutex> lock(output_mtx);
           
            cout << "Total task done: " << total_tasks_processed << endl;
            cout << "Active servers: " << active_servers << endl;
            cout << "Status serverrs:" << endl;
            for (auto& server : servers) {
                cout << "  server #" << server->id << ": loading"
                    << server->get_load_percent() << "% | "
                    << (server->active ? "active" : "off") << endl;
            }
            cout << "==========================================" << endl;
        }
    }
};

// ==================== ГЛАВНАЯ ФУНКЦИЯ ====================
int main() {
   

    ClusterSystem cluster(5);
    cluster.run(15);

    cout << "\nDONE!" << endl;

    return 0;
}
