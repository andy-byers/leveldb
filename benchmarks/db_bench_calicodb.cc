// Copyright (c) 2022, The CalicoDB Authors. All rights reserved.
//
// This file was modified from db_bench_sqlite3.cc. Original copyright:
//
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include <calicodb/cursor.h>
#include <calicodb/db.h>

#include <cstdio>
#include <cstdlib>

#include "util/histogram.h"
#include "util/random.h"
#include "util/testutil.h"

// Comma-separated list of operations to run in the specified order
//   Actual benchmarks:
//
//   fillseq       -- write N values in sequential key order in async mode
//   fillseqsync   -- write N/100 values in sequential key order in sync mode
//   fillseqbatch  -- batch write N values in sequential key order in async mode
//   fillrandom    -- write N values in random key order in async mode
//   fillrandsync  -- write N/100 values in random key order in sync mode
//   fillrandbatch -- batch write N values in sequential key order in async mode
//   overwrite     -- overwrite N values in random key order in async mode
//   fillrand100K  -- write N/1000 100K values in random order in async mode
//   fillseq100K   -- write N/1000 100K values in sequential order in async mode
//   readseq       -- read N times sequentially
//   readrandom    -- read N times in random order
//   readseq100K  -- read N/1000 100K values in sequential order in async mode
//   readrand100K  -- read N/1000 100K values in random order in async mode
static const char* FLAGS_benchmarks =
    "fillseq,"
    "fillseqsync,"
    "fillseqbatch,"
    "fillrandom,"
    "fillrandsync,"
    "fillrandbatch,"
    "overwrite,"
    "overwritebatch,"
    "readrandom,"
    "readseq,"
    "fillrand100K,"
    "fillseq100K,"
    "readseq100K,"
    "readrand100K,";

// Number of key/values to place in database
static int FLAGS_num = 1000000;

// Number of read operations to do.  If negative, do FLAGS_num reads.
static int FLAGS_reads = -1;

// Size of each value
static int FLAGS_value_size = 100;

// Print histogram of operation timings
static bool FLAGS_histogram = false;

// Arrange to generate values that shrink to this fraction of
// their original size after compression
static double FLAGS_compression_ratio = 0.5;

// Page size. Default 4 KB. (must be 4 KB right now - Andy)
static int FLAGS_page_size = calicodb::kPageSize;

// Number of pages.
// Default cache size = FLAGS_page_size * FLAGS_num_pages = 4 MB.
static int FLAGS_num_pages = 1024;

// If true, do not destroy the existing database.  If you set this
// flag and also specify a benchmark that wants a fresh database, that
// benchmark will fail.
static bool FLAGS_use_existing_db = false;

// Use the db with the following name.
static const char* FLAGS_db = nullptr;

inline static void ErrorCheck(const calicodb::Status &status) {
  if (!status.is_ok()) {
    std::fprintf(stderr, "calicodb error: status = %s\n", status.to_string().c_str());
    std::exit(1);
  }
}

inline static void WalCheckpoint(calicodb::DB* db_) {
  // Flush all writes to disk
  ErrorCheck(db_->checkpoint(true));
}

namespace leveldb {

// Helper for quickly generating random data.
namespace {
class RandomGenerator {
 private:
  std::string data_;
  int pos_;

 public:
  RandomGenerator() {
    // We use a limited amount of data over and over again and ensure
    // that it is larger than the compression window (32KB), and also
    // large enough to serve all typical value sizes we want to write.
    Random rnd(301);
    std::string piece;
    while (data_.size() < 1048576) {
      // Add a short fragment that is as compressible as specified
      // by FLAGS_compression_ratio.
      test::CompressibleString(&rnd, FLAGS_compression_ratio, 100, &piece);
      data_.append(piece);
    }
    pos_ = 0;
  }

  Slice Generate(int len) {
    if (pos_ + len > data_.size()) {
      pos_ = 0;
      assert(len < data_.size());
    }
    pos_ += len;
    return Slice(data_.data() + pos_ - len, len);
  }
};

static Slice TrimSpace(Slice s) {
  int start = 0;
  while (start < s.size() && isspace(s[start])) {
    start++;
  }
  int limit = s.size();
  while (limit > start && isspace(s[limit - 1])) {
    limit--;
  }
  return Slice(s.data() + start, limit - start);
}

}  // namespace

class Benchmark {
 private:
  calicodb::DB* db_;
  int db_num_;
  int num_;
  int reads_;
  double start_;
  double last_op_finish_;
  int64_t bytes_;
  std::string message_;
  Histogram hist_;
  RandomGenerator gen_;
  Random rand_;

  // State kept for progress messages
  int done_;
  int next_report_;  // When to report next

  void PrintHeader() {
    const int kKeySize = 16;
    PrintEnvironment();
    std::fprintf(stdout, "Keys:       %d bytes each\n", kKeySize);
    std::fprintf(stdout, "Values:     %d bytes each\n", FLAGS_value_size);
    std::fprintf(stdout, "Entries:    %d\n", num_);
    std::fprintf(stdout, "RawSize:    %.1f MB (estimated)\n",
                 ((static_cast<int64_t>(kKeySize + FLAGS_value_size) * num_) /
                  1048576.0));
    PrintWarnings();
    std::fprintf(stdout, "------------------------------------------------\n");
  }

  void PrintWarnings() {
#if defined(__GNUC__) && !defined(__OPTIMIZE__)
    std::fprintf(
        stdout,
        "WARNING: Optimization is disabled: benchmarks unnecessarily slow\n");
#endif
#ifndef NDEBUG
    std::fprintf(
        stdout,
        "WARNING: Assertions are enabled; benchmarks unnecessarily slow\n");
#endif
  }

  void PrintEnvironment() {
    std::fprintf(stderr, "CalicoDB:   version %d.%d.%d\n",
                 CALICODB_VERSION_MAJOR,
                 CALICODB_VERSION_MINOR,
                 CALICODB_VERSION_PATCH);

#if defined(__linux)
    time_t now = time(nullptr);
    std::fprintf(stderr, "Date:       %s",
                 ctime(&now));  // ctime() adds newline

    FILE* cpuinfo = std::fopen("/proc/cpuinfo", "r");
    if (cpuinfo != nullptr) {
      char line[1000];
      int num_cpus = 0;
      std::string cpu_type;
      std::string cache_size;
      while (fgets(line, sizeof(line), cpuinfo) != nullptr) {
        const char* sep = strchr(line, ':');
        if (sep == nullptr) {
          continue;
        }
        Slice key = TrimSpace(Slice(line, sep - 1 - line));
        Slice val = TrimSpace(Slice(sep + 1));
        if (key == "model name") {
          ++num_cpus;
          cpu_type = val.ToString();
        } else if (key == "cache size") {
          cache_size = val.ToString();
        }
      }
      std::fclose(cpuinfo);
      std::fprintf(stderr, "CPU:        %d * %s\n", num_cpus, cpu_type.c_str());
      std::fprintf(stderr, "CPUCache:   %s\n", cache_size.c_str());
    }
#endif
  }

  void Start() {
    start_ = Env::Default()->NowMicros() * 1e-6;
    bytes_ = 0;
    message_.clear();
    last_op_finish_ = start_;
    hist_.Clear();
    done_ = 0;
    next_report_ = 100;
  }

  void FinishedSingleOp() {
    if (FLAGS_histogram) {
      double now = Env::Default()->NowMicros() * 1e-6;
      double micros = (now - last_op_finish_) * 1e6;
      hist_.Add(micros);
      if (micros > 20000) {
        std::fprintf(stderr, "long op: %.1f micros%30s\r", micros, "");
        std::fflush(stderr);
      }
      last_op_finish_ = now;
    }

    done_++;
    if (done_ >= next_report_) {
      if (next_report_ < 1000)
        next_report_ += 100;
      else if (next_report_ < 5000)
        next_report_ += 500;
      else if (next_report_ < 10000)
        next_report_ += 1000;
      else if (next_report_ < 50000)
        next_report_ += 5000;
      else if (next_report_ < 100000)
        next_report_ += 10000;
      else if (next_report_ < 500000)
        next_report_ += 50000;
      else
        next_report_ += 100000;
      std::fprintf(stderr, "... finished %d ops%30s\r", done_, "");
      std::fflush(stderr);
    }
  }

  void Stop(const Slice& name) {
    double finish = Env::Default()->NowMicros() * 1e-6;

    // Pretend at least one op was done in case we are running a benchmark
    // that does not call FinishedSingleOp().
    if (done_ < 1) done_ = 1;

    if (bytes_ > 0) {
      char rate[100];
      std::snprintf(rate, sizeof(rate), "%6.1f MB/s",
                    (bytes_ / 1048576.0) / (finish - start_));
      if (!message_.empty()) {
        message_ = std::string(rate) + " " + message_;
      } else {
        message_ = rate;
      }
    }

    std::fprintf(stdout, "%-12s : %11.3f micros/op;%s%s\n",
                 name.ToString().c_str(), (finish - start_) * 1e6 / done_,
                 (message_.empty() ? "" : " "), message_.c_str());
    if (FLAGS_histogram) {
      std::fprintf(stdout, "Microseconds per op:\n%s\n",
                   hist_.ToString().c_str());
    }
    std::fflush(stdout);
  }

 public:
  enum Order { SEQUENTIAL, RANDOM };
  enum DBState { FRESH, EXISTING };

  Benchmark()
      : db_(nullptr),
        db_num_(0),
        num_(FLAGS_num),
        reads_(FLAGS_reads < 0 ? FLAGS_num : FLAGS_reads),
        bytes_(0),
        rand_(301) {
    std::vector<std::string> files;
    std::string test_dir;
    Env::Default()->GetTestDirectory(&test_dir);
    Env::Default()->GetChildren(test_dir, &files);
    if (!FLAGS_use_existing_db) {
      for (int i = 0; i < files.size(); i++) {
        if (Slice(files[i]).starts_with("dbbench_calicodb")) {
          std::string file_name(test_dir);
          file_name += "/";
          file_name += files[i];
          Env::Default()->RemoveFile(file_name.c_str());
        }
      }
    }
  }

  ~Benchmark() {
    delete db_;
  }

  void Run() {
    PrintHeader();
    Open(false);

    const char* benchmarks = FLAGS_benchmarks;
    while (benchmarks != nullptr) {
      const char* sep = strchr(benchmarks, ',');
      Slice name;
      if (sep == nullptr) {
        name = benchmarks;
        benchmarks = nullptr;
      } else {
        name = Slice(benchmarks, sep - benchmarks);
        benchmarks = sep + 1;
      }

      bytes_ = 0;
      Start();

      bool known = true;
      bool write_sync = false;
      if (name == Slice("fillseq")) {
        Write(write_sync, SEQUENTIAL, FRESH, num_, FLAGS_value_size, 1);
        WalCheckpoint(db_);
      } else if (name == Slice("fillseqbatch")) {
        Write(write_sync, SEQUENTIAL, FRESH, num_, FLAGS_value_size, 1000);
        WalCheckpoint(db_);
      } else if (name == Slice("fillrandom")) {
        Write(write_sync, RANDOM, FRESH, num_, FLAGS_value_size, 1);
        WalCheckpoint(db_);
      } else if (name == Slice("fillrandbatch")) {
        Write(write_sync, RANDOM, FRESH, num_, FLAGS_value_size, 1000);
        WalCheckpoint(db_);
      } else if (name == Slice("overwrite")) {
        Write(write_sync, RANDOM, EXISTING, num_, FLAGS_value_size, 1);
        WalCheckpoint(db_);
      } else if (name == Slice("overwritebatch")) {
        Write(write_sync, RANDOM, EXISTING, num_, FLAGS_value_size, 1000);
        WalCheckpoint(db_);
      } else if (name == Slice("fillrandsync")) {
        write_sync = true;
        Write(write_sync, RANDOM, FRESH, num_ / 100, FLAGS_value_size, 1);
        WalCheckpoint(db_);
      } else if (name == Slice("fillseqsync")) {
        write_sync = true;
        Write(write_sync, SEQUENTIAL, FRESH, num_ / 100, FLAGS_value_size, 1);
        WalCheckpoint(db_);
      } else if (name == Slice("fillrand100K")) {
        Write(write_sync, RANDOM, FRESH, num_ / 1000, 100 * 1000, 1);
        WalCheckpoint(db_);
      } else if (name == Slice("fillseq100K")) {
        Write(write_sync, SEQUENTIAL, FRESH, num_ / 1000, 100 * 1000, 1);
        WalCheckpoint(db_);
      } else if (name == Slice("readseq")) {
        ReadSequential();
      } else if (name == Slice("readrandom")) {
        Read(RANDOM, 1);
      } else if (name == Slice("readseq100K")) {
        int n = reads_;
        reads_ /= 1000;
        Read(SEQUENTIAL, 1);
        reads_ = n;
      } else if (name == Slice("readrand100K")) {
        int n = reads_;
        reads_ /= 1000;
        Read(RANDOM, 1);
        reads_ = n;
      } else if (name == Slice("stats")) {
        PrintStats("calicodb.stats");
      } else {
        known = false;
        if (name != Slice()) {  // No error message for empty name
          std::fprintf(stderr, "unknown benchmark '%s'\n",
                       name.ToString().c_str());
        }
      }
      if (known) {
        Stop(name);
      }
    }
  }

  void PrintStats(const char* key) {
    std::string stats;
    if (!db_->get_property(key, &stats)) {
      stats = "(failed)";
    }
    std::fprintf(stdout, "\n%s\n", stats.c_str());
  }

  void Open(bool full_sync) {
    assert(db_ == nullptr);

    calicodb::Status status;
    char file_name[100];
    db_num_++;

    // Open database
    std::string tmp_dir;
    Env::Default()->GetTestDirectory(&tmp_dir);
    std::snprintf(file_name, sizeof(file_name), "%s/dbbench_calicodb-%d.db",
                  tmp_dir.c_str(), db_num_);
    calicodb::Options options;
    // Andy: db_bench_sqlite3 sets the value of "synchronous" to either "FULL"
    //       or "OFF" (not "NORMAL").
    options.sync_mode = full_sync ? calicodb::Options::kSyncFull
                                  : calicodb::Options::kSyncOff;
    options.lock_mode = calicodb::Options::kLockExclusive;
    options.cache_size = FLAGS_num_pages * FLAGS_page_size;
    status = calicodb::DB::open(options, file_name, db_);
    if (!status.is_ok()) {
      std::fprintf(stderr, "open error: %s\n", status.to_string().c_str());
      std::exit(1);
    }

    status = db_->update([](auto &tx) {
      return tx.create_bucket(calicodb::BucketOptions(), "default", nullptr);
    });
    ErrorCheck(status);
  }

  void Write(bool write_sync, Order order, DBState state, int num_entries,
             int value_size, int entries_per_batch) {
    // Create new database if state == FRESH
    if (state == FRESH) {
      if (FLAGS_use_existing_db) {
        message_ = "skipping (--use_existing_db is true)";
        return;
      }
      delete db_;
      db_ = nullptr;
      Open(write_sync);
      Start();
    }

    if (num_entries != num_) {
      char msg[100];
      std::snprintf(msg, sizeof(msg), "(%d ops)", num_entries);
      message_ = msg;
    }

    calicodb::Status status;
    int64_t prev_bytes = bytes_;

    for (int i = 0; i < num_entries; i += entries_per_batch) {
      // Begin write transaction
      status = db_->update([this, entries_per_batch, i, num_entries, order, value_size](auto &tx) {
          calicodb::Bucket b;
          calicodb::Status s = tx.create_bucket(
              calicodb::BucketOptions(), "default", &b);
          if (!s.is_ok()) {
            return s;
          }

          for (int j = 0; j < entries_per_batch; j++) {
            const char* value = gen_.Generate(value_size).data();

            // Create values for key-value pair
            const int k =
                (order == SEQUENTIAL) ? i + j : (rand_.Next() % num_entries);
            char key[100];
            std::snprintf(key, sizeof(key), "%016d", k);

            const calicodb::Slice _k(key, 16);
            const calicodb::Slice _v(value, value_size);
            bytes_ += static_cast<std::int64_t>(_k.size() + _v.size());
            s = tx.put(b, _k, _v);
            if (!s.is_ok()) {
              break;
            }

            FinishedSingleOp();
          }
          return s;
      });
      ErrorCheck(status);

      // TODO: This block tries to simulate the SQLite PRAGMA "wal_autocheckpoint"
      //       using the number of bytes of payload written. Should implement an
      //       autocheckpoint option for CalicoDB. For now, just run a checkpoint
      //       after 8 pages-worth of record has been added.
      const auto added_bytes = bytes_ - prev_bytes;
      if (added_bytes / FLAGS_page_size >= 8) {
        status = db_->checkpoint(true);
        prev_bytes = bytes_;
        ErrorCheck(status);
      }
    }
  }

  void Read(Order order, int entries_per_batch) {
    calicodb::Status status;

    for (int i = 0; i < reads_; i += entries_per_batch) {
      status = db_->view([this, entries_per_batch, i, order](const auto &tx) {
        calicodb::Bucket b;
        calicodb::Status s = tx.open_bucket("default", b);
        if (!s.is_ok()) {
          return s;
        }
        for (int j = 0; j < entries_per_batch; j++) {
          // Create key value
          char key[100];
          int k = (order == SEQUENTIAL) ? i + j : (rand_.Next() % reads_);
          std::snprintf(key, sizeof(key), "%016d", k);

          std::string value;
          const calicodb::Slice _k(key, 16);
          s = tx.get(b, _k, &value);
          if (s.is_not_found()) {
            s = calicodb::Status::ok();
          }
          if (!s.is_ok()) {
            break;
          }

          FinishedSingleOp();
        }
        return s;
      });
      ErrorCheck(status);
    }
  }

  void ReadSequential() {
    calicodb::Status status;
    status = db_->view([this](const auto &tx) {
      calicodb::Bucket b;
      calicodb::Status s = tx.open_bucket("default", b);
      if (!s.is_ok()) {
        return s;
      }
      calicodb::Cursor *c = tx.new_cursor(b);
      for (int i = 0; i < reads_; i++) {
        if (!c->is_valid()) {
          c->seek_first();
          continue;
        }
        bytes_ += static_cast<std::int64_t>(c->key().size() + c->value().size());
        c->next();
        FinishedSingleOp();
      }
      s = c->status();
      delete c;
      return s;
    });

    ErrorCheck(status);
  }
};

}  // namespace leveldb

int main(int argc, char** argv) {
  std::string default_db_path;
  for (int i = 1; i < argc; i++) {
    double d;
    int n;
    char junk;
    if (leveldb::Slice(argv[i]).starts_with("--benchmarks=")) {
      FLAGS_benchmarks = argv[i] + strlen("--benchmarks=");
    } else if (sscanf(argv[i], "--histogram=%d%c", &n, &junk) == 1 &&
               (n == 0 || n == 1)) {
      FLAGS_histogram = n;
    } else if (sscanf(argv[i], "--compression_ratio=%lf%c", &d, &junk) == 1) {
      FLAGS_compression_ratio = d;
    } else if (sscanf(argv[i], "--use_existing_db=%d%c", &n, &junk) == 1 &&
               (n == 0 || n == 1)) {
      FLAGS_use_existing_db = n;
    } else if (sscanf(argv[i], "--num=%d%c", &n, &junk) == 1) {
      FLAGS_num = n;
    } else if (sscanf(argv[i], "--reads=%d%c", &n, &junk) == 1) {
      FLAGS_reads = n;
    } else if (sscanf(argv[i], "--value_size=%d%c", &n, &junk) == 1) {
      FLAGS_value_size = n;
    } else if (sscanf(argv[i], "--num_pages=%d%c", &n, &junk) == 1) {
      FLAGS_num_pages = n;
    } else if (strncmp(argv[i], "--db=", 5) == 0) {
      FLAGS_db = argv[i] + 5;
    } else {
      std::fprintf(stderr, "Invalid flag '%s'\n", argv[i]);
      std::exit(1);
    }
  }

  // Choose a location for the test database if none given with --db=<path>
  if (FLAGS_db == nullptr) {
    leveldb::Env::Default()->GetTestDirectory(&default_db_path);
    default_db_path += "/dbbench";
    FLAGS_db = default_db_path.c_str();
  }

  leveldb::Benchmark benchmark;
  benchmark.Run();
  return 0;
}
