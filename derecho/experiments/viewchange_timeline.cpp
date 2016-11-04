#include <chrono>
#include <ratio>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <sstream>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "../derecho_group.h"
#include "../managed_group.h"
#include "../derecho_caller.h"
#include "../rdmc/util.h"
#include "../view.h"

using namespace std;
using namespace std::chrono_literals;
using std::chrono::high_resolution_clock;
using std::chrono::duration;
using std::chrono::microseconds;

const uint64_t SECOND = 1000000000ull;
const size_t message_size = 200000000;
const size_t block_size = 1000000;

uint32_t num_nodes, node_id;
map<uint32_t, std::string> node_addresses;

shared_ptr<derecho::ManagedGroup<Dispatcher<>>> managed_group;

void stability_callback(int sender_id, long long int index, char *data,
                        long long int size) {
    managed_group->log_event(std::stringstream() << "Message " << index
                                                 << " from sender " << sender_id
                                                 << " delivered");
}

void send_messages(uint64_t duration) {
    uint64_t end_time = get_time() + duration;
    while(get_time() < end_time) {
        char *buffer = managed_group->get_sendbuffer_ptr(message_size);
        if(buffer) {
            memset(buffer, rand() % 256, message_size);
            //          cout << "Send function call succeeded at the client
            //          side" << endl;
            managed_group->send();
        }
    }
}

void query_node_info(derecho::node_id_t& node_id, derecho::ip_addr& node_ip, derecho::ip_addr& leader_ip) {
     cout << "Please enter this node's ID: ";
     cin >> node_id;
     cout << "Please enter this node's IP address: ";
     cin >> node_ip;
     cout << "Please enter the leader node's IP address: ";
     cin >> leader_ip;
}

int main(int argc, char *argv[]) {
    srand(time(nullptr));
    if(argc < 2) {
        cout << "Error: Expected number of nodes in experiment as the first argument."
                << endl;
        return -1;
    }
    num_nodes = std::atoi(argv[1]);
    derecho::ip_addr my_ip;
    derecho::node_id_t leader_id = 0;
    derecho::ip_addr leader_ip;

    query_node_info(node_id, my_ip, leader_ip);

    // Synchronize clocks
    vector<uint32_t> members;
    for(uint32_t i = 0; i < num_nodes; i++) members.push_back(i);
    auto universal_barrier_group = make_unique<rdmc::barrier_group>(members);

    universal_barrier_group->barrier_wait();
    auto t1 = high_resolution_clock::now();
    universal_barrier_group->barrier_wait();
    auto t2 = high_resolution_clock::now();
    reset_epoch();
    derecho::program_start_time = high_resolution_clock::now();
    universal_barrier_group->barrier_wait();
    auto t3 = high_resolution_clock::now();

    printf(
        "Synchronized clocks.\nTotal possible variation = %5.3f us\n"
        "Max possible variation from local = %5.3f us\n",
        duration<double, std::micro>(t3 - t1).count(),
        duration<double, std::micro>(max((t2 - t1), (t3 - t2))).count());
    fflush(stdout);
    cout << endl
         << endl;

    string log_filename =
        (std::stringstream() << "events_node" << node_id << ".csv").str();
    derecho::CallbackSet callbacks{stability_callback, nullptr};
    derecho::DerechoParams param_object{message_size, block_size};
    Dispatcher<> empty_dispatcher(node_id);

    if(node_id == num_nodes - 1) {
        cout << "Sleeping for 10 seconds..." << endl;
        std::this_thread::sleep_for(10s);
        cout << "Connecting to group" << endl;
        managed_group = make_shared<derecho::ManagedGroup<Dispatcher<>>>(
            node_id, my_ip, leader_id, leader_ip, std::move(empty_dispatcher), callbacks);
        managed_group->log_event("About to start sending");
        send_messages(10 * SECOND);
        managed_group->log_event("About to exit");
        ofstream logfile(log_filename);
        managed_group->print_log(logfile);
        logfile.close();
        exit(0);
    } else {
        if(node_id == leader_id) {
            managed_group = make_shared<derecho::ManagedGroup<Dispatcher<>>>(
                    my_ip, std::move(empty_dispatcher), callbacks, param_object);
        } else {
            managed_group = make_shared<derecho::ManagedGroup<Dispatcher<>>>(
                    node_id, my_ip, leader_id, leader_ip, std::move(empty_dispatcher), callbacks);
        }
        cout << "Created group, waiting for others to join." << endl;
        while(managed_group->get_members().size() < (num_nodes - 1)) {
            std::this_thread::sleep_for(1ms);
        }
        send_messages(30 * SECOND);
        // managed_group->barrier_sync();
        ofstream logfile{log_filename};
        managed_group->print_log(logfile);
        std::this_thread::sleep_for(5s);
        managed_group->leave();
    }
}