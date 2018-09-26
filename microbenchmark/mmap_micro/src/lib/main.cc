#include <cmath>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/mman.h>
#include <pthread.h>
#include <getopt.h>
#include <errno.h>

#include <vector>

#include <util/runnable.h>
#include <util/util.h>

using namespace latr::util;

#define PAGE_SIZE 4096

struct config_t {
  uint16_t count_cores;
  uint16_t count_sockets;
  uint32_t iterations;
  uint32_t pages;
  bool use_madvise;
};

struct thread_args_t {
  void* mmap_base;
};

static int parseOption(int argc, char* argv[], config_t& config) {
  static struct option options[] = {
      {"cores", required_argument, 0, 'a'},
      {"sockets", required_argument, 0, 'b'},
      {"iterations", required_argument, 0, 'c'},
      {"pages", required_argument, 0, 'd'},
      {"use-madvise", required_argument, 0, 'e'},
      {0, 0, 0, 0},
  };
  int arg_cnt;

  for (arg_cnt = 0; 1; ++arg_cnt) {
    int c, idx = 0;
    c = getopt_long(argc, argv, "a:b:c:d:e:", options, &idx);
    if (c == -1)
      break;

    switch (c) {
    case 'a':
      config.count_cores = std::stoi(std::string(optarg));
      break;
    case 'b':
      config.count_sockets = std::stoi(std::string(optarg));
      break;
    case 'c':
      config.iterations = std::stoi(std::string(optarg));
      break;
    case 'd':
      config.pages = std::stoi(std::string(optarg));
      break;
    case 'e':
      config.use_madvise = (std::string(optarg) == "1");
      break;
    default:
      return -EINVAL;
    }
  }
  return arg_cnt;
}

static void usage(FILE* out) {
  extern const char* __progname;

  fprintf(out, "Usage: %s\n", __progname);
  fprintf(out, "  --cores        = number of cores to run on\n");
  fprintf(out, "  --sockets      = number of sockets to run on\n");
  fprintf(out, "  --iterations   = number of iterations to run\n");
  fprintf(out, "  --pages        = number of pages to allocate\n");
  fprintf(out, "  --use-madvise  = use madvise instead of munmap\n");
}

class ReferenceThread : public Runnable {
public:
  ReferenceThread(const config_t& config, pthread_barrier_t* barrier)
      : config_(config), barrier_(barrier) {}

  virtual void run() {

    for (int i = 0; i < config_.iterations; ++i) {
      pthread_barrier_wait(barrier_);

      // Touch each page once
      for (int j = 0; j < config_.pages; ++j) {
        data_[j * PAGE_SIZE] = 0;
      }
      pthread_barrier_wait(barrier_);
    }
  }

  void updateData(char* new_data) { data_ = new_data; }

private:
  config_t config_;

  pthread_barrier_t* barrier_;

  char* data_;
};

class MainThread : public Runnable {
public:
  MainThread(const config_t& config) : config_(config) {}

  virtual void run() {

    uint64_t total_time_spent = 0;
    uint64_t minimum_time_per_iteration = UINT64_MAX;

    pthread_barrier_t barrier_reference;
    pthread_barrier_init(&barrier_reference, NULL,
                         config_.count_cores * config_.count_sockets + 1);

    std::vector<ReferenceThread*> threads;
    for (int j = 0; j < config_.count_sockets; ++j) {
      cpu_id_t affinity(j, 0, 0);

      for (int k = 0; k < config_.count_cores; ++k) {
        ReferenceThread* thread =
            new ReferenceThread(config_, &barrier_reference);
        thread->setAffinity(affinity);

        threads.push_back(thread);

        // Increase affinity for the next thread
        affinity.nextNearest(false);
        affinity.smt = 0;
      }
    }

    // Start all reference threads
    for (auto& thread : threads) {
      thread->start();
    }

    for (int i = 0; i < config_.iterations; ++i) {
      uint64_t length = PAGE_SIZE * config_.pages;
      char* data = (char*)mmap(NULL, length, PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

      if (data == MAP_FAILED) {
        printf("mmap failed");
        exit(1);
      }

      // Pass this address to all reference threads.
      for (auto& thread : threads) {
        thread->updateData(data);
      }

      // Signal reference threads to reference.
      pthread_barrier_wait(&barrier_reference);
      // Wait for references to be done.
      pthread_barrier_wait(&barrier_reference);

      // Now unmap and take the time it takes to do this
      uint64_t time_before = get_time_nsec();
      if (config_.use_madvise) {
        madvise(data, length, MADV_DONTNEED);
      } else {
        munmap(data, length);
      }
      uint64_t time_after = get_time_nsec();
      uint64_t time_per_iteration = time_after - time_before;
      total_time_spent += time_per_iteration;
      minimum_time_per_iteration =
          std::min(minimum_time_per_iteration, time_per_iteration);
    }

    // Wait for all reference threads to be done.
    for (auto& thread : threads) {
      thread->join();
    }

    double time_per_iteration = (double)total_time_spent / config_.iterations;
    printf("Total time spent: %lu\n", total_time_spent);
    printf("time per iteration: %f\n", time_per_iteration);
    printf("minimum time per iteration: %lu\n", minimum_time_per_iteration);
  }

private:
  config_t config_;
};

int main(int argc, char** argv) {
  config_t config;

  // parse command line options
  if (parseOption(argc, argv, config) != 5) {
    usage(stderr);
    return 1;
  }

  printf("Running with %hu cores on %hu sockets\n", config.count_cores,
         config.count_sockets);

  MainThread mainThread(config);
  // Pin the allocator on the first socket.
  cpu_id_t allocatorAffinity(0, 0, 0);
  mainThread.setAffinity(allocatorAffinity);

  // Wait for the allocation to be done.
  mainThread.start();
  // Wait for the allocator to be done.
  mainThread.join();

  return 0;
}
