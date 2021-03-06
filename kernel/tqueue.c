/* defines functions for task queues */
#include <kernel.h>
#include <tqueue.h>
#include <mutex.h>
#include <ll.h>
#include <cpu.h>
/* the rules for tqueue's are simple:
 * 1) you MUST disable interrupts when accessing them. This makes
 *    it safe for single-cpu machines to use these.
 * 2) you MUST acquire the mutex to access them. This makes it safe
 *    for multi-cpu machines to use them too.
 * 3) While accessing them, you MAY NOT call any functions that may
 *    possibly reschedule, enable interrupts, or access tqueues.
 */

tqueue_t *tqueue_create(tqueue_t *tq, unsigned flags)
{
	if(!tq) {
		tq = (void *)kmalloc(sizeof(tqueue_t));
		tq->flags = (TQ_ALLOC | flags);
	} else
		tq->flags=flags;
	mutex_create(&tq->lock, MT_NOSCHED);
	ll_create_lockless(&tq->tql);
	tq->num=0;
	return tq;
}

void tqueue_destroy(tqueue_t *tq)
{
	mutex_destroy(&tq->lock);
	ll_destroy(&tq->tql);
	if(tq->flags & TQ_ALLOC)
		kfree(tq);
}

struct llistnode *tqueue_insert(tqueue_t *tq, void *item, struct llistnode *node)
{
	int old = set_int(0);
	mutex_acquire(&tq->lock);
	ll_do_insert(&tq->tql, node, item);
	if(!tq->current)
		tq->current = tq->tql.head;
	tq->num++;
	mutex_release(&tq->lock);
	set_int(old);
	return node;
}

void tqueue_remove(tqueue_t *tq, struct llistnode *i)
{
	int old = set_int(0);
	mutex_acquire(&tq->lock);
	if(tq->current == i) tq->current=0;
	ll_do_remove(&tq->tql, i, 0);
	tq->num--;
	mutex_release(&tq->lock);
	set_int(old);
}

void *tqueue_next(tqueue_t *tq)
{
	int old = set_int(0);
	mutex_acquire(&tq->lock);
	if(tq->current) tq->current = tq->current->next;
	/* can't use else here. Need to catch the case when current->next is
	 * null above */
	if(!tq->current) tq->current = tq->tql.head;
	void *ret = tq->current->entry;
	mutex_release(&tq->lock);
	set_int(old);
	assert(ret);
	return ret;
}
