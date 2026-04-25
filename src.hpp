
#pragma once
#include "interface.h"
#include "definition.h"
// You should not use those functions in runtime.h

#include <algorithm>
#include <queue>
#include <vector>
#include <unordered_map>
#include <cmath>

namespace oj {

// Structure to track task state
struct TaskState {
    task_id_t task_id;
    time_t time_passed;
    cpu_id_t cpu_count;
    time_t start_time;
    bool is_running;
    
    // Default constructor
    TaskState() : task_id(0), time_passed(0), cpu_count(0), 
                 start_time(0), is_running(false) {}
    
    // Constructor with task_id
    TaskState(task_id_t id) : task_id(id), time_passed(0), cpu_count(0), 
                             start_time(0), is_running(false) {}
};

// Comparator for priority queue (higher priority first)
struct PriorityComparator {
    std::unordered_map<task_id_t, Task> task_map;
    
    PriorityComparator() = default;
    
    PriorityComparator(const std::vector<Task>& tasks) {
        for (size_t i = 0; i < tasks.size(); ++i) {
            task_map[task_id_t(i)] = tasks[i];
        }
    }
    
    bool operator()(const task_id_t& a, const task_id_t& b) const {
        auto it_a = task_map.find(a);
        auto it_b = task_map.find(b);
        if (it_a == task_map.end() || it_b == task_map.end()) {
            return false; // Should not happen in normal operation
        }
        return it_a->second.priority < it_b->second.priority;
    }
};

auto generate_tasks(const Description &desc) -> std::vector<Task> {
    std::vector<Task> tasks;
    tasks.reserve(desc.task_count);
    
    // Generate tasks with random but valid parameters within the given ranges
    // Use a simple deterministic approach for reproducibility
    time_t total_execution_time = 0;
    priority_t total_priority = 0;
    
    for (task_id_t i = 0; i < desc.task_count; ++i) {
        // Calculate remaining ranges
        time_t remaining_execution_max = desc.execution_time_sum.max - total_execution_time;
        
        priority_t remaining_priority_max = desc.priority_sum.max - total_priority;
        
        // Adjust single task ranges based on remaining totals
        time_t exec_min = desc.execution_time_single.min;
        time_t exec_max = std::min(desc.execution_time_single.max, remaining_execution_max);
        if (exec_min > exec_max) exec_min = exec_max;
        
        priority_t prio_min = desc.priority_single.min;
        priority_t prio_max = std::min(desc.priority_single.max, remaining_priority_max);
        if (prio_min > prio_max) prio_min = prio_max;
        
        // Generate values
        time_t execution_time = exec_min + (i % (std::max(exec_max - exec_min + 1, time_t(1))));
        if (execution_time > remaining_execution_max) {
            execution_time = remaining_execution_max;
        }
        
        priority_t priority = prio_min + (i % (std::max(prio_max - prio_min + 1, priority_t(1))));
        if (priority > remaining_priority_max) {
            priority = remaining_priority_max;
        }
        
        // Ensure we meet minimum requirements
        if (i == desc.task_count - 1) {
            if (total_execution_time + execution_time < desc.execution_time_sum.min) {
                execution_time = desc.execution_time_sum.min - total_execution_time;
            }
            if (total_priority + priority < desc.priority_sum.min) {
                priority = desc.priority_sum.min - total_priority;
            }
        }
        
        // Generate launch time and deadline
        time_t deadline = desc.deadline_time.min + (i % (std::max(desc.deadline_time.max - desc.deadline_time.min + 1, time_t(1))));
        time_t launch_time = std::max(time_t(0), deadline - 100); // Ensure launch time is before deadline
        
        tasks.push_back({
            launch_time,
            deadline,
            execution_time,
            priority
        });
        
        total_execution_time += execution_time;
        total_priority += priority;
    }
    
    // Sort by launch time
    std::ranges::sort(tasks, {}, &Task::launch_time);
    
    return tasks;
}

auto schedule_tasks(time_t time, std::vector<Task> list, const Description &desc) -> std::vector<Policy> {
    static std::unordered_map<task_id_t, TaskState> task_states;
    static std::priority_queue<task_id_t, std::vector<task_id_t>, PriorityComparator> pending_tasks;
    static PriorityComparator comparator;
    
    // Update the comparator with current tasks if it's empty
    if (comparator.task_map.empty() && !list.empty()) {
        comparator = PriorityComparator(list);
    }
    
    // Update pending tasks with new arrivals
    for (size_t i = 0; i < list.size(); ++i) {
        task_id_t task_id = task_id_t(i);
        // Only add if not already in task_states
        if (task_states.find(task_id) == task_states.end()) {
            task_states[task_id] = TaskState(task_id);
            pending_tasks.push(task_id);
        }
    }
    
    std::vector<Policy> policies;
    cpu_id_t cpu_usage = 0;
    
    // Check if any running tasks should be canceled or saved
    for (auto& [task_id, state] : task_states) {
        if (!state.is_running) continue;
        
        // Make sure task_id is valid
        if (task_id >= list.size()) continue;
        
        const Task& task = list[task_id];
        time_t time_used = time - state.start_time;
        double progress = state.time_passed + time_policy(time_used, state.cpu_count);
        
        // If task is complete, save it
        if (progress >= task.execution_time) {
            policies.push_back(Saving{task_id});
            state.is_running = false;
            continue;
        }
        
        // If deadline is approaching and we can't finish in time, consider canceling
        // or adjust resources
        time_t remaining_time = task.deadline - time;
        if (remaining_time <= 0) {
            policies.push_back(Cancel{task_id});
            state.is_running = false;
            continue;
        }
    }
    
    // Allocate resources to pending tasks
    while (!pending_tasks.empty()) {
        task_id_t task_id = pending_tasks.top();
        pending_tasks.pop();
        
        // Make sure task_id is valid
        if (task_id >= list.size()) continue;
        
        const Task& task = list[task_id];
        auto it = task_states.find(task_id);
        if (it == task_states.end()) continue;
        
        TaskState& state = it->second;
        
        if (state.is_running) continue; // Already running
        
        // Calculate required resources to complete before deadline
        time_t remaining_time = task.deadline - time - PublicInformation::kSaving - PublicInformation::kStartUp;
        if (remaining_time <= 0) continue; // Can't complete in time
        
        // Calculate minimum CPU count needed
        double required_progress = task.execution_time - state.time_passed;
        double min_cpu_power = required_progress / remaining_time;
        cpu_id_t min_cpu_count = 1;
        
        // Find minimum CPU count that satisfies the requirement
        for (cpu_id_t c = 1; c <= desc.cpu_count; ++c) {
            double power = std::pow(c, PublicInformation::kAccel);
            if (power >= min_cpu_power) {
                min_cpu_count = c;
                break;
            }
        }
        
        // Check if we have enough CPU resources
        if (cpu_usage + min_cpu_count <= desc.cpu_count) {
            policies.push_back(Launch{min_cpu_count, task_id});
            state.cpu_count = min_cpu_count;
            state.start_time = time;
            state.is_running = true;
            cpu_usage += min_cpu_count;
        }
    }
    
    return policies;
}

} // namespace oj
