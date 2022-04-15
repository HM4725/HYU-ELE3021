#define QSIZE 3
#define BOOSTPERIOD 100
#define SLEEPQ -1
#define FREEQ -2
#define STRIDEQ -3
#define TQ(l) (l==0 ? 1 : l==1 ? 2 : 4)
#define TA(l) (l==0 ? 5 : 10)
#define LARGENUM 1000
#define RESERVE 20
#define MAXINT 0x7fffffff
#define BARRIER 0x6fffffff
#define STRD(t) (LARGENUM / (t))

struct queue {
  struct proc *head;
  struct proc *tail;
};

struct mlfq {
  // Stride fields
  int tickets;
  int pass;
  // MLFQ fields
  uint ticks;
  struct queue queue[QSIZE];     // RUNNING & RUNNABLE
  struct proc* pin[QSIZE];  
};

struct stride {
  int size;                      // Size of minheap
  struct proc* minheap[NPROC+1]; // RUNNABLE & SLEEPING
  struct queue run;              // RUNNING
};
