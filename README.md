# NP Schedulability Test

This repository contains the implementations of schedulability tests for **sets of non-preemptive moldable gang jobs** with **precedence constraints** and **self-suspension delays** scheduled on **uniprocessor**, **partitioned multiprocessor**, **globally scheduled identical multiprocessors** or **heterogeneous clusters of processors**. The analyses are described in the following papers:

- M. Nasri and B. Brandenburg, “[An Exact and Sustainable Analysis of Non-Preemptive Scheduling](https://people.mpi-sws.org/~bbb/papers/pdf/rtss17.pdf)”, *Proceedings of the 38th IEEE Real-Time Systems Symposium (RTSS 2017)*, pp. 12–23, December 2017.
- M. Nasri, G. Nelissen, and B. Brandenburg, “[Response-Time Analysis of Limited-Preemptive Parallel DAG Tasks under Global Scheduling](http://drops.dagstuhl.de/opus/volltexte/2019/10758/pdf/LIPIcs-ECRTS-2019-21.pdf)”, *Proceedings of the 31st Euromicro Conference on Real-Time Systems (ECRTS 2019)*, pp. 21:1–21:23, July 2019.
- G. Nelissen, J. Marcè i Igual, and M. Nasri, “[Response-time analysis for non-preemptive periodic moldable gang tasks](http://dagstuhl.sunsite.rwth-aachen.de/volltexte/2022/16329/pdf/LIPIcs-ECRTS-2022-12.pdf)”, *Proceedings of the 34th Euromicro Conference on Real-Time Systems (ECRTS 2022)*, pp. 12:1–12:22, July 2022.
- S. Srinivasan, M. Gunzel, and G. Nelissen, “[Response-Time Analysis for Limited-Preemptive Self-Suspending and Event-Driven Delay-Induced Tasks]()”, *Proceedings of the 45th IEEE Real-Time Systems Symposium (RTSS 2024)*, to appear.
- G. Nelissen, “[Work-in-Progress: Response-Time Analysis of Partitioned and Clustered Systems with the Schedule-Abstraction Framework]()”, *Proceedings of the 45th IEEE Real-Time Systems Symposium (RTSS 2024)*, to appear.

An [earlier version of this tool](https://github.com/brandenburg/np-schedulability-analysis/releases/tag/ECRTS18-last) (i.e., up to tag [`ECRTS18-last`](https://github.com/brandenburg/np-schedulability-analysis/releases/tag/ECRTS18-last)) implemented the analysis for independent jobs on globally scheduled multiprocessors presented at ECRTS'18.

- M. Nasri, G. Nelissen, and B. Brandenburg,  
“[A Response-Time Analysis for Non-Preemptive Job Sets under Global Scheduling](http://drops.dagstuhl.de/opus/volltexte/2018/8994/pdf/LIPIcs-ECRTS-2018-9.pdf)”,  
*Proceedings of the 30th Euromicro Conference on Real-Time Systems (ECRTS 2018)*, pp. 9:1–9:23, July 2018.


The uniprocessor analysis (Nasri & Brandenburg, 2017) is exact (at least in the absence of precedence constraints); the multiprocessor analyses (Nasri et al., 2018, 2019)(Nelissen et al., 2022)(Srinivasa et al, 2024) are sufficient. 

## Dependencies

- A modern C++ compiler supporting the **C++14 standard**. Recent versions `MSVC` on windows and of `clang` and `g++` on Linux and macOS are known to work. 

- The [CMake](https://cmake.org) build system.

- The [Intel oneAPI Threading Building Blocks (oneTBB)](https://www.threadingbuildingblocks.org) library and parallel runtime. 

- The [jemalloc](http://jemalloc.net) scalable memory allocator. Alternatively, the TBB allocator can be used instead; see build options below.

- The [yaml-cpp](https://github.com/jbeder/yaml-cpp) library.

## Build Instructions
### Windows
For Windows, we recommend to load the project in Visual Studio and use cmake to build a solution. The easiest is to open a terminal (Tools > Command Line > Developper Command Prompt) and type the command 
```bash
cd build
cmake ..
```
You can then open the solution and build it in VS. 

### Linux and macOS
The rest of the instructions assume a Linux or macOS host.

If `yaml-cpp` is not installed on your system, its submodule should be pulled by running the following command:
```bash
git submodule update --init --recursive
```

To compile the tool, first generate an appropriate `Makefile` with `cmake` and then use it to actually build the source tree.
```bash
 # (1) enter the build directory
 cd build
 # (2) generate the Makefile
 cmake ..
 # (3) build everything
 make -j
```

The last step yields two binaries:

- `nptest`, the actually schedulability analysis tool, and
- `runtests`, the unit-test suite. 

## Build Options

The build can be configured in a number of ways by passing options via the `-D` flag to `cmake` in step (2). 

To enable debug builds, pass the `DEBUG` option to `cmake` .

    cmake -DDEBUG=yes ..

To enable the collection of schedule graphs (the `-g` option in `nptest`), set the option `COLLECT_SCHEDULE_GRAPHS` to `yes`. 

    cmake -DCOLLECT_SCHEDULE_GRAPHS=yes  ..

Note that enabling `COLLECT_SCHEDULE_GRAPHS` disallow parallel analysis, i.e., the analysis is single-threaded. We do not recommend to turn it on by default. It is primarily a debugging aid. 

By default, `nptest` uses the default `libc` memory allocator (which may be a tremendous scalability bottleneck if the parallel execution is turned on). To instead use the parallel allocator that comes with Intel TBB, set `USE_JE_MALLOC` to `no` and `USE_TBB_MALLOC` to `yes`. 

    cmake -DUSE_JE_MALLOC=no -DUSE_TBB_MALLOC=yes ..

If you prefer using `je_malloc` instead set `USE_JE_MALLOC` to `yes` and `USE_TBB_MALLOC` to `no`.

    cmake -DUSE_JE_MALLOC=yes -DUSE_TBB_MALLOC=no ..

## Unit Tests

The tool comes with a test driver (based on [C++ doctest](https://github.com/onqtam/doctest)) named `runtests`. After compiling everything, just run the tool to make sure everything works. 

```
$ ./runtests 
[doctest] doctest version is "1.2.6"
[doctest] run with "--help" for options
===============================================================================
[doctest] test cases:     57 |     57 passed |      0 failed |      0 skipped
[doctest] assertions:    537 |    537 passed |      0 failed |
[doctest] Status: SUCCESS!
```

## Input Format

The tool operates on CSV files with a fixed column order. There are three main input formats: *job sets*, *precedence constraints*, and *abort actions*. 

### Job Sets

Job set input CSV files describe a set of jobs, where each row specifies one job. The following columns are required for sequential jobs (i.e, jobs that require a single processor to execute).

1.   **Task ID** — an arbitrary numeric ID to identify the task to which a job belongs
2.   **Job ID** — a unique numeric ID that identifies the job
3.   **Release min** — the earliest-possible release time of the job (equivalently, this is the arrival time of the job)
4.   **Release max** — the latest-possible release time of the job (equivalently, this is the arrival time plus maximum jitter of the job)
5.   **Cost min** — the best-case execution time of the job (can be zero)
6.   **Cost max** — the worst-case execution time of the job
7.   **Deadline** — the absolute deadline of the job
8.   **Priority** — the priority of the job (EDF: set it equal to the deadline)

Gang jobs (i.e., jobs that may require more than one core to start executing) use the following format.

1.   **Task ID** — an arbitrary numeric ID to identify the task to which a job belongs
2.   **Job ID** — a unique numeric ID that identifies the job
3.   **Release min** — the earliest-possible release time of the job (equivalently, this is the arrival time of the job)
4.   **Release max** — the latest-possible release time of the job (equivalently, this is the arrival time plus maximum jitter of the job)
5.   **Cost per parallelism** — a list mapping levels of parallelism to a minimum and maximum execution time. The list must follow the following format '{ paral:cost_min:cost_max; paral:cost_min:cost_max, ...}'
7.   **Deadline** — the absolute deadline of the job.
8.   **Priority** — the priority of the job (EDF: set it equal to the deadline).
9.   [Optional] **Affinity** — the ID of the core or cluster of cores on which job is assigned to execute. If not specified, the job is mapped on cluster 0 by default.

All numeric parameters can be 64-bit integers (preferred) or floating point values (slower, not recommended). 

Example job set input files are provided in the `examples/` folder (e.g., [examples/fig1a.csv](examples/fig1a.csv)).

### Precedence Constraints

A precedent constraints CSV files define a DAG on the set of jobs provided in a job set CSV file in a straightforward manner. Each row specifies a forward edge. The following four columns are required.

1. **Predecessor task ID** - the task ID of the source of the edge
2. **Predecessor job ID** - the job ID of the source of the edge
3. **Successor task ID** - the task ID of the target of the edge
4. **Successor job ID** - the job ID of the target of the edge
5. **Delay min** [optional] - the minimum delay between the execution completion of the predecessor job to the release of the successor job 
6. **Delay max** [optional] - the maximum delay between the execution completion of the predecessor job to the release of the successor job

An example precedent constraints file is provided in the `examples/` folder (e.g., [examples/fig1a.prec.csv](examples/fig1a.prec.csv)).


### Abort Actions

A file containing abort actions lists for (some of) the jobs comprising a workload a *trigger interval* and a *cleanup cost interval*, with the semantics that if a job is still executing during its trigger interval, the runtime system will trigger a cleanup routine that aborts and discards the job. Each row specifies the abort time and cost for one job. The following six columns are required.

1. **Task ID** - the task ID of the job that has an abort action
2. **Job ID** - the job ID of the job that has an abort action
3. **Earliest Trigger Time** - the earliest time the abort action can trigger (= the job’s absolute deadline, usually)
4. **Latest Trigger Time** - the latest time the abort action will trigger if the job is still running (= usually the job’s absolute deadline + any imprecision of the runtime environment in aborting jobs)
5. **Least Cleanup Cost** - the minimum time required by the runtime system to abort the job
6. **Maximum Cleanup Cost** - the maximum time required by the runtime system to abort the job

An example abort actions file is provided in the `examples/` folder (e.g., [examples/abort.actions.csv](examples/abort.actions.csv)).

### Execution platform description 

A CSV file containing a basic description of the platform on which jobs execute. Each row represent a different cluster of cores. A single column is required:

1. **Number of cores** - the number of identical cores in the cluster.

Any number of clusters and combination of number of cores per cluster is allowed. However we identify the two following special cases; If all clusters contain a single core, then the system behaves like a fully partioned system. If there is a single row (single cluster) containing more than one core, then the system behaves like a globally scheduled multicore system (equivalent to using the `-m` option).

## Analyzing a Job Set

To run the tool on a given set, just pass the filename as an argument. For example:

```
$ build/nptest examples/fig1a.csv 
examples/fig1a.csv,  0,  9,  6,  5,  1,  0.000329,  820.000000,  0
```

By default, the tool assumes that *jobs are independent* (i.e., there no precedence constraints) and runs the *uniprocessor analysis* (RTSS'17). To specify precedence constraints (optionally with delays) or to run the (global) multiprocessor analysis (RTSS'24), the `-p` and  `-m`  options need to be specified, respectively, as discussed below.

The output format is explained below.  

If no input files are provided, or if `-` is provided as an input file name, `nptest` will read the job set description from standard input, which allows applying filters etc. For example:

```
$ cat examples/fig1a.csv | grep -v '3, 9' | build/nptest 
-,  1,  8,  9,  8,  0,  0.000069,  816.000000,  0
```

When analyzing a job set with **dense-time parameters** (i.e., time values specified as floating-point numbers), the option `-t dense` **must** be passed. 

To use the multiprocessor analysis, use the `-m` option. 

See the builtin help (`nptest -h`) for further options.

### Global Multiprocessor Analysis

To run the analysis for globally scheduled multiprocessors, simply provide the number of (identical) processors via the `-m` option. For example: 

```
$ build/nptest -m 2 examples/fig1a.csv 
examples/fig1a.csv,  1,  9,  9,  9,  1,  0.000379,  1760.000000,  0,  2
```

### Partitioned or Clustered Systems Analysis

To run the analysis for partitioned multiprocessors or clustered systems, you must provide a specification of the platform using the `--platform` option followed by the appropriate CSC file. For example: 

```
$ build/nptest --platform examples/platform_desc.csv examples/fig1a.csv 
examples/fig1a.csv,  1,  9,  9,  9,  1,  0.000379,  1760.000000,  0,  2
```

### Precedence Constraints

To impose precedence constraints (optionally with delays) on the job set, provide the DAG structure in a separate CSV file via the `-p` option. For example:

```
$ build/nptest examples/fig1a.csv -p examples/fig1a.prec.csv 
examples/fig1a.csv,  1,  9,  10,  9,  0,  0.000135,  1784.000000,  0,  1
```

The tool does not check whether the provided structure actually forms a DAG. If a cycle is (accidentally) introduced, which essentially represents a deadlock as no job that is part of the cycle can be scheduled first, the analysis will simply discover and report that the workload is unschedulable. 

### Aborting Jobs Past a Certain Point

The analysis also supports so-called **abort actions**, which allow specifying that if a job executes past a certain point in time, it will be forcefully stopped and discarded by the runtime environment. To enable this support, pass a CSV file containing a list of abort actions using the `-a` option. For example:

```
$ build/nptest -g examples/abort.jobs.csv -c -a examples/abort.actions.csv
examples/abort.jobs.csv,  1,  4,  5,  4,  0,  0.000089,  1796.000000,  0,  1
```

**NOTE**: Aborted jobs are considered completed: thus if all tardy jobs are aborted by their deadline, then the tool will report the workload to be schedulable (as in the above example)! This can be observed by omitting the abort actions (`-a` option): 

```
$ build/nptest -g examples/abort.jobs.csv -c
examples/abort.jobs.csv,  0,  4,  5,  4,  0,  0.000088,  1760.000000,  0,  1
```

Without the job abort action specified in [examples/abort.actions.csv](examples/abort.actions.csv), the workload can indeed miss deadlines and is thus unschedulable.

## Output Format

The output is provided in CSV format and consists of the following columns:

1. The input file name.
2. The schedulability result:
    - 1 if the job is *is* schedulable (i.e., the tool could prove the absence of deadline misses),
    - 0 if it is *not*, or if the analysis timed out, if it reached the depth limit, or if the analysis cannot prove the absence of deadline misses (while the RTSS'17 analysis is exact, the ECRTS'19 analysis is only sufficient, but not exact). 
3. The number of jobs in the job set.
4. The number of nodes that were created.
5. The number of states that were explored.
6. The number of edges that were discovered. 
7. The maximum “exploration front width” of the schedule graph, which is the maximum number of unprocessed states  that are queued for exploration (at any point in time). 
8. The CPU time used in the analysis (in seconds).
9. The peak amount of memory used (as reported by `getrusage()`), divided by 1024. Due to non-portable differences in `getrusage()`, on Linux this reports the memory usage in megabytes, whereas on macOS it reports the memory usage in kilobytes. On Windows, the memory usage reporting is deactivated. It will always show 0.
10. A timeout indicator: 1 if the state-space exploration was aborted due to reaching the time limit (as set with the `-l` option); 0 otherwise. 
11. The number of processors assumed during the analysis. 

Pass the `--header` flag to `nptest` to print out column headers. 

## Obtaining Response Times

The analysis computes for each job the earliest and latest possible completion times, from which it is trivial to infer minimum and maximum response times. To obtain this information, pass the `-r` option to `nptest`. 

If invoked on an input file named `foo.csv`, the completion times will be stored in a file `foo.rta.csv` and follow the following format, where each row corresponds to one job in the input job set. 

1. Task ID
2. Job ID
3. BCCT, the best-case completion time
4. WCCT, the worst-case completion time 
5. BCRT, the best-case response time (relative to the minimum release time)
6. WCRT, the worst-case response time (relative to the minimum release time)

Note that the analysis by default aborts after finding the first deadline miss, in which case some of the rows may report nonsensical default values.  To force the analysis to run to completion despite deadline misses, pass the `-c` flag to `nptest`.

## Additional options

### Merge aggressivity

To curb the state space explosion inherent to reachability-based analyses, the SAG merges similar states together. For the analysis of self-suspending tasks and the analysis of multicore systems, merging states may result in losing information and thus reducing the accuracy of the analysis. The `--merge` option allows to define how aggressively should the tool try to merge state. A more aggressive merge reduces runtime but may reduce accuracy. A less aggressive merge may improve accuracy (up to exactness in some cases) but may sometimes have a high cost in terms of runtime.

The option `--merge` can currently be set to 7 different levels:
+ `no`: de-activate merge. This leads to a rapid state-space explosion. It should only be used for very small examples.
+ `c1`: conservative merge level 1. Merge two states only if the availability intervals of one of them are sub-intervals of the other.
+ `c2`: conservative merge level 2. Merge two states only if the availability intervals and job finish time intervals recorded in one of them are sub-intervals of the other.
+ `l1`: lossy level 1. Merge two states if their availability intervals overlap or are contiguous.
+ `l2`; same as `l1` but try to merge up to three states (instead of just two) each time a new state is created.
+ `l3`; same as `l1` but try to merge up to four states (instead of just two) each time a new state is created.
+ `lmax`; same as `l1` but try to merge as many states as possible (instead of just two) each time a new state is created.

By default the merge level is set to `l1`.
  
### Verbose

If the flag `--verbose` is set when launching an analysis, the tool will show the analysis progress in the terminal.

### Evolution of the SAG width

The `-r` option reports the response time bounds of every job but also reports the evolution of the width (in terms of nodes) of the SAG for each depth level.

If invoked on an input file named `foo.csv`, the SAG width evolution will be stored in a file named `foo.width.csv`. 

## Questions, Patches, or Suggestions

In case of questions, please contact [Geoffrey Nelissen](https://www.tue.nl/en/research/researchers/geoffrey-nelissen/), the current maintainer of the project.

Patches and feedback welcome — please send a pull request or open a ticket. 

## License 

The code is released under a 3-clause BSD license. 


## Credits

The software was originally developed by [Björn Brandenburg](https://people.mpi-sws.org/~bbb/). It is now being maintained by [Geoffrey Nelissen](https://www.tue.nl/en/research/researchers/geoffrey-nelissen/). Mitra Nasri, Joan Marcè i Igual, Sayra Ranjha, Srinidhi Srinivasan, Pourya Gohari and Richard Verhoeven contributed major analysis and code improvements. 

When using this software in academic work, please cite the papers listed at the top of this file.  
