#define DTQ      5
#define TQ(l)    ((l)==0 ? 5 : 10*(l))
#define TA(l)    (4*TQ(l))
#define LARGENUM 1000
#define MAXINT   0x7fffffff
#define BARRIER  0x6fffffff
#define STRD(t)  (LARGENUM / (t))

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
  struct proc* minheap[NPROC];   // RUNNABLE & SLEEPING
  struct list_head run;          // RUNNING
};

struct ptable {
  struct spinlock lock;
  struct proc proc[NPROC];
  struct mlfq mlfq;
  struct stride stride;
  struct list_head sleep;
  struct list_head free;
};
