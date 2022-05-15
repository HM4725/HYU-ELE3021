#ifndef _LIST_H
#define _LIST_H

typedef unsigned int size_t;

#define container_of(ptr, type, member) ({              \
  const typeof( ((type *)0)->member ) *__mptr = (ptr);  \
  (type *)( (char *)__mptr - offsetof(type,member) );})

#define offsetof(TYPE, MEMBER) ((size_t) & ((TYPE *)0)->MEMBER)

#define list_entry(ptr, type, member) \
  container_of(ptr, type, member)

#define list_first_entry(ptr, type, member) \
  list_entry((ptr)->next, type, member)

#define list_last_entry(ptr, type, member) \
  list_entry((ptr)->prev, type, member)

struct list_head {
  struct list_head *prev, *next;
};

static inline void 
list_head_init(struct list_head *list)
{
  list->next = list;
  list->prev = list;
}

static inline void
__list_add(struct list_head *new,
           struct list_head *prev,
           struct list_head *next)
{
  next->prev = new;
  new->next = next;
  new->prev = prev;
  prev->next = new;
}

static inline void
list_add(struct list_head *new,
         struct list_head *head)
{
  __list_add(new, head, head->next);
}

static inline void
list_add_tail(struct list_head *new,
              struct list_head *head)
{
  __list_add(new, head->prev, head);
}

static inline void
__list_del(struct list_head *prev,
           struct list_head *next)
{
  prev->next = next;
  next->prev = prev;
}

static inline void
__list_del_entry(struct list_head *entry)
{
  __list_del(entry->prev, entry->next);
}

static inline void
list_del(struct list_head *entry)
{
  __list_del_entry(entry);
  entry->prev = 0;
  entry->next = 0;
}

static inline void
list_replace(struct list_head *old,
			 struct list_head *new)
{
  new->next = old->next;
  new->next->prev = new;
  new->prev = old->prev;
  new->prev->next = new;
}

static inline int
list_empty(struct list_head *head)
{
  return head->next == head;
}

static inline int
list_is_first(const struct list_head *list,
             const struct list_head *head)
{
  return list->prev == head;
}

static inline int
list_is_last(const struct list_head *list,
             const struct list_head *head)
{
  return list->next == head;
}

static inline int
list_is_head(const struct list_head *list,
             const struct list_head *head)
{
    return list == head;
}

static inline void
list_bulk_move_tail(struct list_head *src,
                    struct list_head *dst)
{
  if(!list_empty(src)){
    dst->prev->next = src->next;
    src->next->prev = dst->prev;

    src->prev->next = dst;
    dst->prev = src->prev;

    src->next = src;
    src->prev = src;
  }
}

#endif
