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
#include <memory>

using namespace std;
using namespace chrono;

//создаю структуру заадания
struct ClusterTask {
    int id;
    int priority;           // 1 - высокая, 2 - средняя, 3 - низкая
    int duration;
    int required_power;
    bool is_critical;


    //  неявно исп внутри priority_queue
    // явно  в функции compare_tasks() для демонстрации
    bool operator<(const ClusterTask& other) const {
        if (priority != other.priority)
            return priority > other.priority;
        if (is_critical != other.is_critical)
            return !is_critical;
        return false;
    }
};

//метод сравнеия для очереди
void compare_tasks(const ClusterTask& t1, const ClusterTask& t2) {
    cout << "Comparing tasks #" << t1.id << " and #" << t2.id << ": ";
    if (t1 < t2) {
        cout << "Task #" << t1.id << " is HIGHER priority than #" << t2.id << endl;
    }
    else if (t2 < t1) {
        cout << "Task #" << t2.id << " is HIGHER priority than #" << t1.id << endl;
    }
    else {
        cout << "Tasks have the SAME priority" << endl;
    }
}


class Server {
public:
    int id;
    int max_power;
    int current_load;
    bool active;
    mutex mtx;
    counting_semaphore<> sem;
    //(бинарный семафор)
    //один сервер может выполнять только одну задачу одновременно

    Server(int _id, int _max_power = 3)
        : id(_id), max_power(_max_power), current_load(0), active(true), sem(1) {
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

class ClusterSystem {
private:
    vector<unique_ptr<Server>> servers;
    priority_queue<ClusterTask> task_queue;
    mutex queue_mtx;//защита очереди
    mutex output_mtx;//и вывода
    condition_variable cv;//мех синх потоков
    //один поток ждет другой уведомляет
    //далее есть поток add task и серверы ожидающие задачи
    atomic<bool> running{ true };
    atomic<int> total_tasks_processed{ 0 };

    random_device rd;
    mt19937 gen;
    uniform_int_distribution<> duration_dist{ 500, 5000 };
    uniform_int_distribution<> power_dist{ 1, 3 };

public:
    ClusterSystem(int num_servers = 5) : gen(rd()) {
        for (int i = 0; i < num_servers; i++) {
            servers.push_back(make_unique<Server>(i + 1));
        }
    }

    void add_task(int id, int priority, bool is_critical = false) {
        ClusterTask task;
        task.id = id;
        task.priority = priority;
        task.duration = duration_dist(gen);
        task.required_power = power_dist(gen);
        task.is_critical = is_critical;


        {
            lock_guard<mutex> lock(queue_mtx);
            if (!task_queue.empty()) {
                const ClusterTask& top = task_queue.top();
                cout << "[COMPARE] New task #" << id;
                if (task < top) {
                    cout << " has higher priority than current top task (#" << top.id << ")" << endl;
                }
                else if (top < task) {
                    cout << " has lower priority than current top task (#" << top.id << ")" << endl;
                }
                else {
                    cout << " has the same priority as current top task (#" << top.id << ")" << endl;
                }
            }
            task_queue.push(task);
        }

        {
            lock_guard<mutex> lock(output_mtx);
            cout << "[ADD] Task #" << id
                << " | Priority: " << priority
                << " | Power: " << task.required_power
                << " | Time: " << task.duration / 1000.0 << "sec";
            if (is_critical) cout << " [CRITICAL]";
            cout << endl;
        }

        cv.notify_one();//в произв исп при добавл зад
    }

    // Функция для демонстрации сортировки с использованием operator<
    void demonstrate_priority_queue() {
        lock_guard<mutex> lock(queue_mtx);
        if (task_queue.empty()) {
            cout << "Queue is empty" << endl;
            return;
        }

        cout << "\n=== CURRENT QUEUE ===" << endl;
        auto temp_queue = task_queue;
        int pos = 1;
        while (!temp_queue.empty()) {
            ClusterTask t = temp_queue.top();
            temp_queue.pop();
            cout << pos++ << ". task #" << t.id
                << " (priority=" << t.priority
                << (t.is_critical ? ", CRITICAL" : "")
                << ")" << endl;
        }
        cout << "                                                \n" << endl;
    }

    void server_worker(int server_idx) {
        Server& server = *servers[server_idx];

        while (running) {
            ClusterTask current_task;
            bool has_task = false;

            {
                unique_lock<mutex> lock(queue_mtx);
                //чждем покп в очереди покажутся задаи
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
                        server.sem.acquire();//захват семафора

                        // === ЕЩЕ ОДНО ЯВНОЕ ИСПОЛЬЗОВАНИЕ operator< ===
                        if (!task_queue.empty()) {
                            const ClusterTask& next = task_queue.top();
                            cout << "[SCHED] Next task in queue: #" << next.id;
                            if (next < current_task) {
                                cout << " (has higher priority than the most recent task #" << current_task.id << ")" << endl;
                            }
                        }
                    }
                }
            }

            if (has_task) {
                {
                    lock_guard<mutex> lock(output_mtx);
                    cout << "[WORK] server #" << server.id
                        << " | task #" << current_task.id
                        << " (priority=" << current_task.priority;
                    if (current_task.is_critical) cout << ", CRITICAL";
                    cout << ") | " << current_task.duration / 1000.0 << "sec" << endl;
                }

                this_thread::sleep_for(milliseconds(current_task.duration));

                server.sem.release();
                server.remove_task(current_task);
                total_tasks_processed++;

                {
                    lock_guard<mutex> lock(output_mtx);
                    cout << "[DONE] server #" << server.id
                        << " is done #" << current_task.id << endl;
                }
            }
        }
    }

    void run(int num_tasks) {
        vector<thread> workers;
        for (size_t i = 0; i < servers.size(); i++) {
            workers.emplace_back(&ClusterSystem::server_worker, this, i);
        }

        // Добавляем задачи с разными приоритетами
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

            // Каждые 3 задачи показываем состояние очереди
            if (i % 3 == 0) {
                demonstrate_priority_queue();
            }

            this_thread::sleep_for(milliseconds(800));
        }

        this_thread::sleep_for(seconds(8));
        running = false;
        cv.notify_all();//исп в производителе при добавлении задач
        //нужен для меньшего потребления cpu

        for (auto& t : workers) {
            t.join();
        }

        cout << "\n=== THE END ===" << endl;
        cout << "Tasks done: " << total_tasks_processed << endl;
    }
};

// ==================== ГЛАВНАЯ ====================
int main() {
    ClusterSystem cluster(3);

    // Демонстрация явного сравнения задач

    ClusterTask taskA{ 1, 1, 1000, 2, false };
    ClusterTask taskB{ 2, 2, 1000, 2, false };
    ClusterTask taskC{ 3, 1, 1000, 2, true };

    compare_tasks(taskA, taskB);
    compare_tasks(taskA, taskC);
    compare_tasks(taskB, taskC);
    cout << endl;

    cluster.run(10);

    return 0;
}
