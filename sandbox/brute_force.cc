/**
 * @author jeff.daily@pnnl.gov
 *
 * A first attempt at a brute force alignment of an input dataset using work
 * stealing. Each MPI task reads the input file.
 */
#include <mpi.h>
#include <tascel.h>
#include <tascel/UniformTaskCollection.h>
#include <tascel/UniformTaskCollSplitHybrid.h>
#include <gmp.h>

#include <sys/time.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <strstream>
#include <vector>

#include "combinations.h"
#include "dynamic.h"

using namespace std;
using namespace tascel;

#define ARG_LEN_MAX 1024

#define MPI_CHECK(what) do {                              \
    int __err;                                            \
    __err = what;                                         \
    ++check_count;                                        \
    if (MPI_SUCCESS != __err) {                           \
        printf("[%d] FAILED FILE=%s LINE=%d:" #what "\n", \
                rank, __FILE__, __LINE__);                \
        MPI_Abort(comm, check_count);                     \
    }                                                     \
} while (0)

int rank = 0;
int nprocs = 0;
int check_count = 0;
cell_t **tbl = NULL;
int **del = NULL;
int **ins = NULL;
vector<string> sequences;
ProcGroup* pgrp = NULL;
UniformTaskCollSplitHybrid* utcs[NUM_WORKERS];
long align_counts[NUM_WORKERS];
long long align_times[NUM_WORKERS];
long long align_times_max[NUM_WORKERS];
// Synchronization for worker threads
pthread_barrier_t workersStart, workersEnd;
// Synchronization for server thread
pthread_barrier_t serverStart, serverEnd;
static pthread_t threadHandles[NUM_WORKERS + NUM_SERVERS];
static unsigned threadRanks[NUM_WORKERS + NUM_SERVERS];
volatile bool serverEnabled = true;
unsigned long combinations_per_task=0;

static unsigned long long timer_start()
{
    struct timeval timer;
    (void)gettimeofday(&timer, NULL);
    return timer.tv_sec * 1000000 + timer.tv_usec;
}

static unsigned long long timer_end(unsigned long long begin)
{
    return timer_start() - begin;
}

void *serverThd(void *args)
{
#if defined(SET_AFFINITY)
    cpu_set_t cpuset;
    pthread_t thread;
    CPU_ZERO(&cpuset);
    thread = pthread_self();

    int rank = theTwoSided().getProcRank().toInt();

    CPU_SET(NUM_WORKERS, &cpuset);

    int ret = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
#endif

    // When enabled execute any active messages that arrive
    while (1) {
        pthread_barrier_wait(&serverStart);
        while (serverEnabled) {
            AmListenObjCodelet<NullMutex>* lcodelet;
            if((lcodelet=theAm().amListeners[0]->progress()) != NULL) {
                lcodelet->execute();
            }
            Codelet* codelet;
            if ((codelet = serverDispatcher.progress()) != NULL) {
                codelet->execute();
                // Assume that codelet was an AmRequest and needs to be freed
                delete reinterpret_cast<AmRequest*>(codelet);
            }
        }
        pthread_barrier_wait(&serverEnd);
    }
    return NULL;
}

void *workerThd(void *args)
{
    const unsigned threadRank = *(unsigned*)args;

#if defined(SET_AFFINITY)
    cpu_set_t cpuset;
    pthread_t thread;
    CPU_ZERO(&cpuset);
    thread = pthread_self();

    int rank = theTwoSided().getProcRank().toInt();

    CPU_SET(threadRank, &cpuset);

    int ret = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
#endif

    while (1) {
        pthread_barrier_wait(&workersStart);
        utcs[threadRank]->process(threadRank);
        pthread_barrier_wait(&workersEnd);
    }

    return NULL;
}

void amBarrier()
{
    int epoch = pgrp->signalBarrier();
    while(!pgrp->testBarrier(epoch)) {
        AmListenObjCodelet<NullMutex>* codelet;
        if((codelet=theAm().amListeners[0]->progress()) != NULL) {
            codelet->execute();
        }
    }
}

void amBarrierThd()
{
    int epoch = pgrp->signalBarrier();
    while(!pgrp->testBarrier(epoch)) { }
}

static int trank(int thd)
{
    return (theTwoSided().getProcRank().toInt() * NUM_WORKERS) + thd;
}

typedef struct {
    unsigned long id;
} task_description;

static void alignment_task(
        UniformTaskCollection *utc,
        void *_bigd, int bigd_len,
        void *pldata, int pldata_len,
        vector<void *> data_bufs, int thd) {
    task_description *desc = (task_description*)_bigd;
    unsigned long task_id = desc->id;
    unsigned long combo_start_index = task_id * combinations_per_task;
    unsigned long seq_id[2];
    cell_t result;
    is_edge_param_t param;
    int is_edge_answer = 0;
    long long t = 0;
    unsigned long i;

    k_combination(combo_start_index, 2, seq_id);

    for (i=0; i<combinations_per_task; ++i) {
        /* the last task may run out of sequences */
        if (seq_id[0] >= sequences.size() || seq_id[1] >= sequences.size()) {
            cout << "task_id " << task_id << " left early" << endl;
            break;
        }
        t = timer_start();
        affine_gap_align(
                sequences[seq_id[0]].c_str(), sequences[seq_id[0]].size(),
                sequences[seq_id[1]].c_str(), sequences[seq_id[1]].size(),
                &result, tbl, del, ins);
        param.AOL = 8;
        param.SIM = 4;
        param.OS = 3;
        is_edge_answer = is_edge(result,
                sequences[seq_id[0]].c_str(), sequences[seq_id[0]].size(),
                sequences[seq_id[1]].c_str(), sequences[seq_id[1]].size(),
                param);
        ++align_counts[thd];

        if (is_edge_answer) {
            cout << trank(thd)
                << ": aligned " << seq_id[0] << " " << seq_id[1]
                << ": (score,ndig,alen)=("
                << result.score << ","
                << result.ndig << ","
                << result.alen << ")"
                << ": edge? " << is_edge_answer << endl;
        }
        t = timer_end(t);
        align_times[thd] += t;
        align_times_max[thd] = MAX(align_times_max[thd],t);
        next_combination(2, seq_id);
    }
#if DEBUG
    cout << trank(thd) << " finished task_id " << task_id << endl;
#endif
}


int main(int argc, char **argv)
{
    MPI_Comm comm = MPI_COMM_NULL;
    int provided;
    vector<string> all_argv;
    long file_size = -1;
    char *file_buffer = NULL;
    MPI_File fh;
    MPI_Status status;
    long seg_count = 0;
    size_t max_seq_len = 0;
    unsigned long nCk;

    check_count = 0;

    /* initialize MPI */
    MPI_CHECK(MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided));
    assert(provided == MPI_THREAD_MULTIPLE);
    MPI_CHECK(MPI_Comm_rank(MPI_COMM_WORLD, &rank));
    MPI_CHECK(MPI_Comm_size(MPI_COMM_WORLD, &nprocs));
    MPI_CHECK(MPI_Comm_dup(MPI_COMM_WORLD, &comm));

    /* initialize tascel */
    TascelConfig::initialize(NUM_WORKERS, MPI_COMM_WORLD);
    pgrp = ProcGroup::construct();
    for (int worker=0; worker<NUM_WORKERS; ++worker) {
        threadRanks[worker] = worker;
    }

    /* initialize dynamic code */
    init_map(SIGMA);

    /* MPI standard does not guarantee all procs receive argc and argv */
    if (0 == rank) {
        MPI_CHECK(MPI_Bcast(&argc, 1, MPI_INT, 0, comm));
        for (int i=0; i<argc; ++i) {
            int length = strlen(argv[i])+1;
            MPI_CHECK(MPI_Bcast(&length, 1, MPI_INT, 0, comm));
            MPI_CHECK(MPI_Bcast(argv[i], length, MPI_CHAR, 0, comm));
            all_argv.push_back(argv[i]);
        }
    } else {
        int all_argc;
        MPI_CHECK(MPI_Bcast(&all_argc, 1, MPI_INT, 0, comm));
        for (int i=0; i<all_argc; ++i) {
            int length;
            char buffer[ARG_LEN_MAX];
            MPI_CHECK(MPI_Bcast(&length, 1, MPI_INT, 0, comm));
            MPI_CHECK(MPI_Bcast(buffer, length, MPI_CHAR, 0, comm));
            all_argv.push_back(buffer);
        }
    }

#if DEBUG
    /* print the command line arguments */
    for (int i=0; i<nprocs; ++i) {
        if (i == rank) {
            int j;
            for (j=0; j<all_argv.size(); ++j) {
                printf("[%d] argv[%d]=%s\n", rank, j, all_argv[j].c_str());
            }
        }
        MPI_Barrier(comm);
    }
#endif

    /* sanity check that we got the correct number of arguments */
    if (all_argv.size() <= 1 || all_argv.size() >= 4) {
        if (0 == rank) {
            if (all_argv.size() <= 1) {
                printf("missing input file\n");
            }
            else if (all_argv.size() >= 4) {
                printf("too many arguments\n");
            }
            printf("usage: brute_force sequence_file [combinations_per_task]\n");
        }
        MPI_Comm_free(&comm);
        MPI_Finalize();
        return 1;
    }

    /* process 0 open the file locally to determine its size */
    if (0 == rank) {
        FILE *file = fopen(all_argv[1].c_str(), "r");
        if (NULL == file) {
            printf("unable to open file on process 0\n");
            MPI_Abort(comm, 1);
        }
        if (0 != fseek(file, 0, SEEK_END)) {
            printf("unable to seek to end of file on process 0\n");
            MPI_Abort(comm, 1);
        }
        file_size = ftell(file);
        if (-1 == file_size) {
            printf("unable to get size of file on process 0\n");
            MPI_Abort(comm, 1);
        }
        if (0 != fclose(file)) {
            printf("unable to get close file on process 0\n");
            MPI_Abort(comm, 1);
        }
    }

    /* the file_size is broadcast to all */
    MPI_CHECK(MPI_Bcast(&file_size, 1, MPI_LONG, 0, comm));
    if (0 == trank(0)) {
        printf("file_size=%ld\n", file_size);
    }
    /* allocate a buffer for the file, of the entire size */
    /* TODO: this is not memory efficient since we allocate a buffer to read
     * the entire input file and then parse the buffer into a vector of
     * strings, essentially doubling the memory requirement */
    file_buffer = new char[file_size];

    /* all procs read the entire file */
    MPI_CHECK(MPI_File_open(comm, const_cast<char*>(all_argv[1].c_str()),
                MPI_MODE_RDONLY|MPI_MODE_UNIQUE_OPEN,
                MPI_INFO_NULL, &fh));
    MPI_CHECK(MPI_File_read_all(fh, file_buffer, file_size, MPI_CHAR, &status));
    MPI_CHECK(MPI_File_close(&fh));

    /* each process counts how many '>' characters are in the file_buffer */
    for (int i=0; i<file_size; ++i) {
        if (file_buffer[i] == '>') {
            ++seg_count;
        }
    }
#if DEBUG
    /* print the seg_count on each process */
    for (int i=0; i<nprocs; ++i) {
        if (i == rank) {
            printf("[%d] seg_count=%ld\n", rank, seg_count);
        }
        MPI_Barrier(comm);
    }
#endif

    /* TODO declare these at the top */
    /* each process indexes the file_buffer */
    istrstream input_stream(const_cast<const char*>(file_buffer), file_size);
    string line;
    string sequence;
    sequences.reserve(seg_count);
    while (getline(input_stream, line)) {
        if (line[0] == '>') {
            if (!sequence.empty()) {
                sequences.push_back(sequence);
                max_seq_len = max(max_seq_len, sequence.size());
            }
            sequence.clear();
            continue;
        }
        sequence += line;
    }
    /* add the last sequence in the file since we wouldn't encounter another
     * '>' character but rather an EOF */
    if (!sequence.empty()) {
        sequences.push_back(sequence);
        max_seq_len = max(max_seq_len, sequence.size());
    }
    sequence.clear();
#if DEBUG
    /* print the seg_count on each process */
    for (int i=0; i<nprocs; ++i) {
        if (i == rank) {
            printf("[%d] sequences.size()=%ld sequences.capacity()=%ld\n",
                    rank, long(sequences.size()), long(sequences.capacity()));
        }
        MPI_Barrier(comm);
    }
    /* print the max_seq_len on each process */
    for (int i=0; i<nprocs; ++i) {
        if (i == rank) {
            printf("[%d] max_seq_len=%ld\n", rank, long(max_seq_len));
        }
        MPI_Barrier(comm);
    }
#endif
#if DEBUG
    /* print the first sequence on each process */
    for (int i=0; i<nprocs; ++i) {
        if (i == rank) {
            printf("[%d] sequences[0]=%s\n", rank, sequences[0].c_str());
        }
        MPI_Barrier(comm);
    }
    /* print the last sequence on each process */
    for (int i=0; i<nprocs; ++i) {
        if (i == rank) {
            printf("[%d] sequences.back()=%s\n",
                    rank, sequences.back().c_str());
        }
        MPI_Barrier(comm);
    }
#endif
    /* how many combinations of sequences are there? */
    nCk = binomial_coefficient(sequences.size(), 2);
    if (0 == trank(0)) {
        printf("brute force %lu C 2 has %lu combinations\n",
                sequences.size(), nCk);
    }

    /* set the combinations_per_task variable */
    if (all_argv.size() == 3) {
        combinations_per_task = strtoll(all_argv[2].c_str(), NULL, 10);
    }
    else {
        combinations_per_task = sequences.size();
    }
    unsigned long ntasks = nCk / combinations_per_task;
    if (nCk % combinations_per_task != 0) {
        ntasks += 1;
    }
    unsigned long global_num_workers = nprocs*NUM_WORKERS;
    unsigned long tasks_per_worker = ntasks / global_num_workers;
    unsigned long max_tasks_per_worker = ntasks / global_num_workers;
    max_tasks_per_worker += ntasks % global_num_workers;
    //for (int i=0; i<nprocs; ++i) {
        //if (i == rank) {
        if (0 == trank(0)) {
            printf("combinations_per_task=%lu\n", combinations_per_task);
            printf("ntasks=%lu\n", ntasks);
            printf("global_num_workers=%lu\n", global_num_workers);
            printf("tasks_per_worker=%lu\n", tasks_per_worker);
            printf("max_tasks_per_worker=%lu\n", max_tasks_per_worker);
        }
        MPI_Barrier(comm);
    ////}

    /* some more dynamic initialization */
    assert(NROW == 2);
    tbl = alloc_tbl(NROW, max_seq_len);
    del = alloc_int(NROW, max_seq_len);
    ins = alloc_int(NROW, max_seq_len);

    /* the tascel part */
    for (int worker=0; worker<NUM_WORKERS; ++worker)
    {
        UniformTaskCollSplitHybrid*& utc = utcs[worker];
        align_counts[worker] = 0;
        align_times[worker] = 0;
        align_times_max[worker] = 0;
        TslFuncRegTbl *frt = new TslFuncRegTbl();
        TslFunc tf = frt->add(alignment_task);
        TaskCollProps props;
        props.functions(tf, frt)
            .taskSize(sizeof(task_description))
            .maxTasks(max_tasks_per_worker);
        utc = new UniformTaskCollSplitHybrid(props, worker);

        /* add some tasks, different amounts to different tranks */
        task_description desc;
        int count = 0;

        printf("%d(%d): trank(%d)=%d\n",
                rank, threadRanks[worker], worker, trank(worker));
        unsigned long i = trank(worker)*tasks_per_worker;
        unsigned long limit = i + tasks_per_worker;
        printf("%d(%d): i=%lu limit=%lu\n",
                rank, threadRanks[worker], i, limit);

        for (/*ignore*/; i<limit; ++i) {
            count++;
            desc.id = i;
            utcs[worker]->addTask(&desc, sizeof(desc));
        }
        /* if I'm the last worker, add the remainder of the tasks */
        if (trank(worker) == nprocs*NUM_WORKERS-1) {
            limit = i + (ntasks % global_num_workers);
            for (/*ignore*/; i<limit; ++i) {
                count++;
                desc.id = i;
                utcs[worker]->addTask(&desc, sizeof(desc));
            }
        }
        printf("%d(%d): added %d tasks\n", rank, threadRanks[worker], count);
    }

    amBarrier();

#if defined(THREADED)
    cpu_set_t cpuset;
    pthread_t thread;
    CPU_ZERO(&cpuset);
    thread = pthread_self();
    CPU_SET(0, &cpuset);
    pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);

    pthread_barrier_init(&workersStart, 0, NUM_WORKERS);
    pthread_barrier_init(&workersEnd, 0, NUM_WORKERS);
    pthread_barrier_init(&serverStart, 0, NUM_SERVERS + 1);
    pthread_barrier_init(&serverEnd, 0, NUM_SERVERS + 1);
    asm("mfence");
    for (unsigned i = 1; i < NUM_WORKERS; ++i) {
      pthread_create(&threadHandles[i], NULL, workerThd, &threadRanks[i]);
    }
    pthread_create(&threadHandles[NUM_WORKERS], NULL,
           serverThd, &threadRanks[NUM_WORKERS]);
#endif

#if defined(THREADED)
    serverEnabled = true;
    pthread_barrier_wait(&serverStart);
    pthread_barrier_wait(&workersStart);
#endif

    utcs[0]->process(0);

#if defined(THREADED)
    pthread_barrier_wait(&workersEnd);
    amBarrierThd();

    serverEnabled = false;
    asm("mfence");
    pthread_barrier_wait(&serverEnd);

    amBarrier();
#endif

    for (int i = 0; i < NUM_WORKERS; i++) {
        utcs[i]->restore();
    }

    amBarrier();

    for (int worker=0; worker<NUM_WORKERS; ++worker) {
        printf("%d(%d) aligned %ld sequence pairs\n",
                rank, worker, align_counts[worker]);
    }
    for (int worker=0; worker<NUM_WORKERS; ++worker) {
        printf("%d(%d) times     %lld ms\n",
                rank, worker, align_times[worker]);
    }
    for (int worker=0; worker<NUM_WORKERS; ++worker) {
        printf("%d(%d) max times %lld ms\n",
                rank, worker, align_times_max[worker]);
    }
    for (int worker=0; worker<NUM_WORKERS; ++worker) {
        utcs[worker]->printStats();
    }

    /* clean up */
    delete [] file_buffer;
    free_tbl(tbl, NROW);
    free_int(del, NROW);
    free_int(ins, NROW);

    TascelConfig::finalize();
    MPI_Comm_free(&comm);
    MPI_Finalize();

    return 0;
}
