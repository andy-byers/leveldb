## Benchmark results

### Platform info

| Name       | Value                                       |
|:-----------|:--------------------------------------------|
| OS version | Ubuntu 22.04.2 LTS                          |
| Filesystem | ext4                                        |
| Drive      | WD_BLACK SN770 NVMe™ SSD (250 GB)           |
| `CPU`      | `16 * 12th Gen Intel(R) Core(TM) i5-12600K` |
| `CPUCache` | `20480 KB`                                  |

### Results (μs/operation)

| Benchmark        |  CalicoDB (0.0.1) | SQLite3 (3.37.2) |  Kyoto Cabinet (1.2.79) |       LevelDB (1.23) |
|:-----------------|------------------:|-----------------:|------------------------:|---------------------:|
| `fillseq`        |             2.035 |            2.865 |                   0.807 |                0.924 |
| `fillseqsync`    |           165.334 |          204.731 |                1796.861 |                      |
| `fillseqbatch`   |             0.400 |            0.759 |                         |                      |
| `fillrandom`     |             4.296 |            6.230 |                   3.081 |                1.256 |
| `fillrandsync`   |           166.913 |          198.992 |                1677.132 |  335.440<sup>*</sup> |
| `fillrandbatch`  |             4.630 |            6.561 |                         |                      |
| `overwrite`      |             4.375 |            5.851 |                   4.268 |                1.567 |
| `overwritebatch` |             4.816 |            5.922 |                         |                      |
| `readrandom`     |             2.063 |            1.736 |                   2.565 |                1.432 |
| `readseq`        |             0.050 |            0.089 |                   0.385 |                0.073 |
| `fillrand100K`   |            88.935 |          196.910 |                  99.039 |  356.504<sup>*</sup> |
| `fillseq100K`    |            91.179 |          133.427 |                 117.135 |                      |
| `readseq100K`    |            21.088 |           38.545 |                  16.834 |                      |
| `readrand100K`   |            21.068 |          100.689 |                  16.184 |                      |

<sup>*</sup> `db_bench.cc` calls these benchmarks `fillsync` and `fill100K`. 
They insert records in random order, so they are grouped with the `fillrand*`.

### Notes
+ `db_bench_sqlite3.cc` was changed to use 4 KiB pages, since that's what CalicoDB uses.

### Raw `db_bench*` output:
```
LevelDB:    version 1.23
Date:       Mon Jun 26 18:22:19 2023
CPU:        16 * 12th Gen Intel(R) Core(TM) i5-12600K
CPUCache:   20480 KB
Keys:       16 bytes each
Values:     100 bytes each (50 bytes after compression)
Entries:    1000000
RawSize:    110.6 MB (estimated)
FileSize:   62.9 MB (estimated)
------------------------------------------------
fillseq      :       0.924 micros/op;  119.8 MB/s     
fillsync     :     335.440 micros/op;    0.3 MB/s (1000 ops)
fillrandom   :       1.256 micros/op;   88.1 MB/s     
overwrite    :       1.567 micros/op;   70.6 MB/s     
readrandom   :       2.141 micros/op; (864322 of 1000000 found)
readrandom   :       1.893 micros/op; (864083 of 1000000 found)
readseq      :       0.081 micros/op; 1365.5 MB/s    
readreverse  :       0.139 micros/op;  797.3 MB/s    
compact      :  266915.000 micros/op;
readrandom   :       1.432 micros/op; (864105 of 1000000 found)
readseq      :       0.073 micros/op; 1516.1 MB/s    
readreverse  :       0.123 micros/op;  896.6 MB/s    
fill100K     :     356.504 micros/op;  267.6 MB/s (1000 ops)
crc32c       :       0.751 micros/op; 5200.0 MB/s (4K per op)
snappycomp   :       2.089 micros/op; 1870.1 MB/s (output: 55.1%)
snappyuncomp :       0.390 micros/op; 10021.2 MB/s   
zstdcomp     :    1210.000 micros/op; (zstd failure)
zstduncomp   :    1202.000 micros/op; (zstd failure)

Kyoto Cabinet:    version 1.2.79, lib ver 16, lib rev 14
Date:           Mon Jun 26 18:22:31 2023
CPU:            16 * 12th Gen Intel(R) Core(TM) i5-12600K
CPUCache:       20480 KB
Keys:       16 bytes each
Values:     100 bytes each (50 bytes after compression)
Entries:    1000000
RawSize:    110.6 MB (estimated)
FileSize:   62.9 MB (estimated)
------------------------------------------------
fillseq      :       0.807 micros/op;  137.0 MB/s     
fillseqsync  :    1796.861 micros/op;    0.1 MB/s (10000 ops)
fillrandsync :    1677.132 micros/op;    0.1 MB/s (10000 ops)
fillrandom   :       3.081 micros/op;   35.9 MB/s     
overwrite    :       4.268 micros/op;   25.9 MB/s     
readrandom   :       2.565 micros/op;                 
readseq      :       0.385 micros/op;  287.1 MB/s    
fillrand100K :      99.039 micros/op;  963.1 MB/s (1000 ops)
fillseq100K  :     117.135 micros/op;  814.3 MB/s (1000 ops)
readseq100K  :      16.834 micros/op; 5666.1 MB/s  
readrand100K :      16.184 micros/op;              

SQLite:     version 3.37.2
Date:       Mon Jun 26 21:18:53 2023
CPU:        16 * 12th Gen Intel(R) Core(TM) i5-12600K
CPUCache:   20480 KB
Keys:       16 bytes each
Values:     100 bytes each
Entries:    1000000
RawSize:    110.6 MB (estimated)
------------------------------------------------
fillseq      :       2.865 micros/op;   38.6 MB/s     
fillseqsync  :     204.731 micros/op;    0.5 MB/s (10000 ops)
fillseqbatch :       0.759 micros/op;  145.8 MB/s     
fillrandom   :       6.230 micros/op;   17.8 MB/s     
fillrandsync :     198.992 micros/op;    0.6 MB/s (10000 ops)
fillrandbatch :       6.561 micros/op;   16.9 MB/s    
overwrite    :       5.851 micros/op;   18.9 MB/s     
overwritebatch :       5.922 micros/op;   18.7 MB/s   
readrandom   :       1.736 micros/op;                 
readseq      :       0.089 micros/op; 1076.9 MB/s    
fillrand100K :     196.910 micros/op;  484.4 MB/s (1000 ops)
fillseq100K  :     133.427 micros/op;  714.9 MB/s (1000 ops)
readseq100K  :      38.545 micros/op;              
readrand100K :     100.689 micros/op;  

CalicoDB:   version 0.0.1
Date:       Mon Jun 26 18:23:50 2023
CPU:        16 * 12th Gen Intel(R) Core(TM) i5-12600K
CPUCache:   20480 KB
Keys:       16 bytes each
Values:     100 bytes each
Entries:    1000000
RawSize:    110.6 MB (estimated)
------------------------------------------------
fillseq      :       2.035 micros/op;   54.4 MB/s     
fillseqsync  :     165.334 micros/op;    0.7 MB/s (10000 ops)
fillseqbatch :       0.400 micros/op;  276.6 MB/s     
fillrandom   :       4.296 micros/op;   25.7 MB/s     
fillrandsync :     166.913 micros/op;    0.7 MB/s (10000 ops)
fillrandbatch :       4.630 micros/op;   23.9 MB/s    
overwrite    :       4.375 micros/op;   25.3 MB/s     
overwritebatch :       4.816 micros/op;   23.0 MB/s   
readrandom   :       2.063 micros/op;                 
readseq      :       0.050 micros/op; 2200.8 MB/s    
fillrand100K :      88.935 micros/op; 1072.5 MB/s (1000 ops)
fillseq100K  :      91.179 micros/op; 1046.1 MB/s (1000 ops)
readseq100K  :      21.088 micros/op;              
readrand100K :      21.068 micros/op;              
```