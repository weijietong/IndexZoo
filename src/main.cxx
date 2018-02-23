#include <cassert>
#include <cstdint>
#include <vector>
#include <thread>
#include <cstdio>
#include <chrono>
#include <atomic>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <cstring>
#include <unistd.h>
#include <getopt.h>

#include "fast_random.h"
#include "time_measurer.h"

#include "data_table.h"

#include "learned_index.h"

void usage(FILE *out) {
  fprintf(out,
          "Command line options : benchmark <options> \n"
          "   -h --help              :  print help message \n"
          "   -t --time_duration     :  time duration (default: 10) \n"
          "   -m --max_key_count     :  max key count (default: 0) \n"
          "   -n --init_key_count    :  init key count (default: 1<<20) \n"
          "   -r --reader_count      :  reader count (default: 0) \n"
          "   -s --inserter_count    :  inserter count (default: 1) \n"
  );
}

static struct option opts[] = {
    { "time_duration",   optional_argument, NULL, 't' },
    { "max_key_count",   optional_argument, NULL, 'm' },
    { "init_key_count",  optional_argument, NULL, 'n' },
    { "reader_count",    optional_argument, NULL, 'r' },
    { "inserter_count",  optional_argument, NULL, 's' },
    { NULL, 0, NULL, 0 }
};

struct Config {
  uint64_t time_duration_ = 10;
  double profile_duration_ = 0.5;
  uint64_t max_key_count_ = 0; // if max_key_count_ is set to 0, then generate insert key randomly.
  uint64_t init_key_count_ = 1ull<<20;
  uint64_t reader_count_ = 1;
  uint64_t inserter_count_ = 0;
  uint64_t thread_count_ = 1;
};

void parse_args(int argc, char* argv[], Config &config) {
  
  while (1) {
    int idx = 0;
    int c = getopt_long(argc, argv, "ht:m:n:r:s:", opts, &idx);

    if (c == -1) break;

    switch (c) {
      case 't': {
        config.time_duration_ = (uint64_t)atoi(optarg);
        break;
      }
      case 'm': {
        config.max_key_count_ = (uint64_t)atoi(optarg);
        break;
      }
      case 'n': {
        config.init_key_count_ = (uint64_t)atoi(optarg);
        break;
      }
      case 'r': {
        config.reader_count_ = (uint64_t)atoi(optarg);
        break;
      }
      case 's': {
        config.inserter_count_ = (uint64_t)atoi(optarg);
        break;
      }
      case 'h': {
        usage(stderr);
        exit(EXIT_FAILURE);
        break;
      }
      default: {
        fprintf(stderr, "Unknown option: -%c-\n", c);
        usage(stderr);
        exit(EXIT_FAILURE);
        break;
      }
    }
  }

  if (config.max_key_count_ != 0) {
    assert(config.init_key_count_ <= config.max_key_count_);
  }

  config.thread_count_ = config.inserter_count_ + config.reader_count_;

}

typedef Uint64 KeyT;
typedef Uint64 ValueT;

/////////////////////////////////////////
// key generation

static std::atomic<uint64_t> global_curr_key;
static uint64_t global_max_key = 0;

class BatchKeys {
public:
  BatchKeys(const uint64_t thread_id) :
    rand_gen_(thread_id),
    thread_id_(thread_id), 
    local_curr_key_(0), 
    local_max_key_(0) {}
  
  KeyT get_insert_key() {
    if (global_max_key == 0) {

      if (local_curr_key_ == local_max_key_){
        uint64_t key = global_curr_key.fetch_add(batch_key_count_, std::memory_order_relaxed);
        local_curr_key_ = key;
        local_max_key_ = key + batch_key_count_;
      }

      uint64_t retKey = local_curr_key_;
      ++local_curr_key_;
      return retKey;

    } else {
      return rand_gen_.next() % global_max_key;

    }
  }

  KeyT get_random_key() {
    if (global_max_key == 0) {
      return rand_gen_.next() % global_curr_key;
    } else {
      return rand_gen_.next() % global_max_key;
    }
  }
  
private:
  FastRandom rand_gen_;

  uint64_t thread_id_;
  
  uint64_t local_curr_key_;
  uint64_t local_max_key_;
  const uint64_t batch_key_count_ = 1ull << 10;
};
/////////////////////////////////////////

bool is_running = false;
uint64_t *operation_counts = nullptr;


// table and index
std::unique_ptr<DataTable<KeyT, ValueT>> data_table(nullptr);
std::unique_ptr<LearnedIndex<KeyT>> data_index(nullptr);

void run_inserter_thread(const uint64_t &thread_id, const Config &config) {

  pin_to_core(thread_id);

  BatchKeys batch_keys(thread_id);

  FastRandom rand_gen(thread_id);

  uint64_t &operation_count = operation_counts[thread_id];
  operation_count = 0;
  while (true) {
    if (is_running == false) {
      break;
    }

    // insert
    KeyT key = batch_keys.get_insert_key();
    ValueT value = 100;
    
    OffsetT offset = data_table->insert_tuple(key, value);

    data_index->insert(key, offset.raw_data());
    
    ++operation_count;
  }
}

void run_reader_thread(const uint64_t &thread_id, const Config &config) {

  pin_to_core(thread_id);

  BatchKeys batch_keys(thread_id);

  FastRandom rand_gen(thread_id);

  uint64_t &operation_count = operation_counts[thread_id];
  operation_count = 0;
  while (true) {
    if (is_running == false) {
      break;
    }

    KeyT key = batch_keys.get_random_key();
    
    std::vector<Uint64> values;

    data_index->find(key, values);
    
    ++operation_count;
  }
}


void run_workload(const Config &config) {
  
  BatchKeys batch_keys(0);
  for (size_t i = 0; i < config.init_key_count_; ++i) {

    KeyT key = batch_keys.get_insert_key();
    ValueT value = 100;
    
    OffsetT offset = data_table->insert_tuple(key, value);

    data_index->insert(key, offset.raw_data());
  }

  data_index->reorganize();

  operation_counts = new uint64_t[config.thread_count_];
  uint64_t profile_round = (uint64_t)(config.time_duration_ / config.profile_duration_);

  uint64_t **operation_counts_profiles = new uint64_t*[profile_round];
  for (uint64_t round_id = 0; round_id < profile_round; ++round_id) {
    operation_counts_profiles[round_id] = new uint64_t[config.thread_count_];
    memset(operation_counts_profiles[round_id], 0, config.thread_count_ * sizeof(uint64_t));
  }
  std::vector<double> act_size_profiles; // actual allocated size. Unit: GB.
  std::vector<size_t> approx_size_profiles; // approximate data size. Unit: #tuples.

  std::vector<uint64_t> insert_counts; // number of insert operations performed.
  std::vector<uint64_t> read_counts; // number of read operations performed.

  double init_mem_size = get_memory_gb();
  std::cout << "init memory size = " << init_mem_size << " GB" << std::endl;
  
  // launch a group of threads
  is_running = true;
  std::vector<std::thread> worker_threads;
  uint64_t thread_count = 0;

  // inserter threads
  for (; thread_count < config.inserter_count_; ++thread_count) {
    worker_threads.push_back(std::move(std::thread(run_inserter_thread, thread_count, config)));
  }
  // reader threads
  for (; thread_count < config.inserter_count_ + config.reader_count_; ++thread_count) {
    std::cout << "run reader thread" << std::endl;
    worker_threads.push_back(std::move(std::thread(run_reader_thread, thread_count, config)));
  }

  std::cout << "        TIME         INSERT      READ       RAM (act.)   RAM (est.)" << std::endl;

  for (uint64_t round_id = 0; round_id < profile_round; ++round_id) {
    std::this_thread::sleep_for(std::chrono::milliseconds(int(config.profile_duration_ * 1000)));
    
    memcpy(operation_counts_profiles[round_id], operation_counts, sizeof(uint64_t) * config.thread_count_);

    act_size_profiles.push_back(get_memory_gb());
    approx_size_profiles.push_back(data_table->size_approx());
    if (round_id == 0) {
      // first round
      uint64_t insert_count = 0;
      uint64_t read_count = 0;

      uint64_t thread_count = 0;
      // count inserts
      for (; thread_count < config.inserter_count_; ++thread_count) {
        insert_count += operation_counts_profiles[0][thread_count];
      }
      // count reads
      for (; thread_count < config.inserter_count_ + config.reader_count_; ++thread_count) {
        read_count += operation_counts_profiles[0][thread_count];
      }
      insert_counts.push_back(insert_count);
      read_counts.push_back(read_count);

    } else {
      // remaining rounds
      uint64_t insert_count = 0;
      uint64_t read_count = 0;

      uint64_t thread_count = 0;
      // count inserts
      for (; thread_count < config.inserter_count_; ++thread_count) {
        insert_count += operation_counts_profiles[round_id][thread_count] - operation_counts_profiles[round_id - 1][thread_count];
      }
      // count reads
      for (; thread_count < config.inserter_count_ + config.reader_count_; ++thread_count) {
        read_count += operation_counts_profiles[round_id][thread_count] - operation_counts_profiles[round_id - 1][thread_count];
      }
      insert_counts.push_back(insert_count);
      read_counts.push_back(read_count);
    }

    // print out
    std::cout << std::fixed << std::setprecision(2) << std::right
              << "[" 
              << std::setw(5) 
              << config.profile_duration_ * round_id << " - " 
              << std::setw(5)
              << config.profile_duration_ * (round_id + 1) 
              << " s]:  "
              << std::setw(5)
              << insert_counts.at(round_id) * 1.0 / 1000 / 1000 
              << " M  |  " 
              << std::setw(5)
              << read_counts.at(round_id) * 1.0 / 1000 / 1000 
              << " M  |  " 
              << std::setw(5)
              << act_size_profiles.at(round_id) 
              << " GB  |  "
              << std::setw(5)
              << approx_size_profiles.at(round_id) * (sizeof(KeyT) + sizeof(ValueT)) * 1.0 / 1024 / 1024 / 1024
              << " GB"
              << std::endl;
  }
  
  // join all the threads
  is_running = false;

  for (uint64_t i = 0; i < config.thread_count_; ++i) {
    worker_threads.at(i).join();
  }
  
  uint64_t total_count = 0;
  for (uint64_t i = 0; i < config.thread_count_; ++i) {
    total_count += operation_counts[i];
  }

  std::cout << "insert = " << config.inserter_count_ << ", "
            << "read = " << config. reader_count_ << ", "
            << "throughput = " << total_count * 1.0 / config.time_duration_ / 1000 / 1000 << " M ops" 
            << std::endl;

  for (uint64_t round_id = 0; round_id < profile_round; ++round_id) {
    delete[] operation_counts_profiles[round_id];
    operation_counts_profiles[round_id] = nullptr;
  }

  delete[] operation_counts_profiles;
  operation_counts_profiles = nullptr;

  delete[] operation_counts;
  operation_counts = nullptr;
}

int main(int argc, char* argv[]) {

  Config config;

  parse_args(argc, argv, config);

  global_max_key = config.max_key_count_;
  global_curr_key = 0;

  data_table.reset(new DataTable<KeyT, ValueT>());
  data_index.reset(new LearnedIndex<KeyT>());

  run_workload(config);
  
}