#define TQ(l) (l==0 ? 1 : 2*l)
#define TA(l) (5*TQ(l))
#define LARGENUM 1000
#define MAXINT 0x7fffffff
#define BARRIER 0x6fffffff
#define STRD(t) (LARGENUM / (t))

struct mlfq {
  // Stride fields
  int tickets;
  int pass;
  // MLFQ fields
  uint ticks;
  struct list_head queue[QSIZE]; // RUNNING & RUNNABLE
  struct list_head* pin[QSIZE];
};

struct stride {
  int size;                      // Size of minheap
  struct proc* minheap[NPROC+1]; // RUNNABLE & SLEEPING
  struct list_head run;          // RUNNING
};
