## Benchmark results
These results are for andy-byers/CalicoDB@30fe767.
The benchmarks are run during C/I.

### Disclaimer
SQLite is a relational database manangement system (RDBMS), while CalicoDB is a key-value store.
SQLite was chosen for comparison, because CalicoDB's backend architecture is largely based off of it.
Just keep in mind the fact that SQLite has more overhead per-operation than CalicoDB.
Various SQLite parameters can be tuned to give more-similar performance characteristics (see [Parameters](#parameters)).

The original benchmarks used `PRAGMA synchronous=OFF` for the non-`*sync` benchmarks.
Both CalicoDB and SQLite have a `NORMAL` sync mode, where `fsync()` (or similar) is called during checkpoint operations.
This is the default mode for both libraries, so it makes sense to use it for the benchmarks.
Also, note that CalicoDB runs very slowly on `OSX` during the `*sync` benchmarks.
This is because `fcntl(fd, F_FULLFSYNC)` is used instead of `fsync()`.
The version of SQLite that ships with OSX seems to call `fcntl(fd, F_BARRIERFSYNC)`, which is much faster, but may not provide true durabiility in the event of a power failure (see [this blog post](https://mjtsai.com/blog/2022/02/17/apple-ssd-benchmarks-and-f_fullsync/) for more details).
SQLite3 built from source on OSX will actually call `fcntl(fd, F_FULLFSYNC)`, however.

The reported benchmarks were all compiled with `gcc`.

### Parameters
SQLite tables are created using `WITHOUT_ROWID`.
This prevents an automatic index from being created on the table (it isn't necessary: lookups use binary search directly on the primary key).
WAL mode was enabled for SQLite and the page size was changed to 4 KiB (CalicoDB always uses a WAL and has 4 KiB pages).
Additional library parameters include:

|Type|CalicoDB|SQLite3|Notes|
|:---|:-------|:------|:----|
|Normal sync mode|`kSyncNormal`|`NORMAL`|Used in non-`*sync` benchmarks|
|Full sync mode|`kSyncFull`|`FULL`|Used in `*sync` benchmarks|
|Lock mode|`kLockExclusive`|`EXCLUSIVE`|Reduces transaction overhead|

### `ubuntu-latest` Results (μs/operation)

|Benchmark|CalicoDB (0.0.1)|SQLite3 (3.39.5)|
|:--------|---:|---:|
|fillseq|23.056|29.767|
|fillseqsync|155.166|226.606|
|fillseqbatch|1.040|2.033|
|fillrandom|113.370|103.860|
|fillrandsync|156.590|215.685|
|fillrandbatch|107.732|89.841|
|overwrite|121.170|120.676|
|overwritebatch|116.873|113.079|
|readrandom|4.613|4.270|
|readseq|0.144|0.193|
|fillrand100K|799.439|790.666|
|fillseq100K|1056.763|1113.057|
|readseq100K|58.518|89.065|
|readrand100K|58.801|252.367|

### `macos-latest` Results (μs/operation)

|Benchmark|CalicoDB (0.0.1)|SQLite3 (3.39.5)|
|:--------|---:|---:|
|fillseq|22.841|45.449|
|fillseqsync|1101.377|291.237|
|fillseqbatch|1.857|4.566|
|fillrandom|165.762|157.887|
|fillrandsync|1117.884|213.655|
|fillrandbatch|153.705|116.973|
|overwrite|194.099|175.583|
|overwritebatch|175.590|143.547|
|readrandom|7.513|16.156|
|readseq|0.183|0.203|
|fillrand100K|1680.123|1855.326|
|fillseq100K|1280.572|1615.777|
|readseq100K|74.773|203.458|
|readrand100K|79.160|495.537|

### Notes
+ `db_bench_sqlite3.cc` was changed to use 4 KiB pages, since that's what CalicoDB uses.

### Raw `db_bench*` output:
See the [C/I results](https://github.com/andy-byers/CalicoDB/actions/runs/5437371027) for the raw output.
