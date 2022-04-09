#define QSIZE 3
#define BOOSTPERIOD 100
#define SLEEPQ -1
#define FREEQ -2
#define TQ(l) (l==0 ? 1 : l==1 ? 2 : 4)
#define TA(l) (l==0 ? 5 : 10)
#define LARGENUM 10000
#define RESERVE 20
#define MAXUINT 0xffffffff
#define STRD(t) (LARGENUM / (t))

struct queue {
  struct proc *head;
  struct proc *tail;
};

struct mlfq {
  // Stride part
  uint tickets;
  uint pass;
  // MLFQ part
  uint ticks;
  struct queue queue[QSIZE]; // RUN queue
  struct proc* pin[QSIZE];  
};

struct stride {
  uint size;
  struct proc* minheap[NPROC+1]; // RUNNABLE heap
};
