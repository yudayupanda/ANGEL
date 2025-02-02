#define AMEM_MOUDLE

#include <string.h>
#include <stdlib.h>
#include <memory.h>
#include "amem.h"
#include "execute.h"
#include "runtime.h"
#include "compilerutil.h"


/*
添加一个内部的数据类型首先要定义类型宏，实现析构（代码添加到dec_element中）和deep_flag，deep_recovery_angel，实现其他的

angel语言默认是用utf-8来显示
towide是转化为同意的宽字符unicode，tomult是转化为统一的utf-8，然后在输出的时候转化为本地编码。
固定内存（对象）和可变内存（字符串和集合类对象的数据而且是经常需要申请释放的）的管理
为了效率。全局变量和局部变量都直接用列表（object_list）和额外的映射表(map类型，这只是简单的名字映射)。
一个做

流式内存的申请分为两类，一类是小数据直接由objectheap申请，大的数据由malloc申请，并将释放的内存放在队列中，队列中的内存块大小以maxsize为基础按照2的倍数不断增加
如果超过队列的大小则直接free掉。 
大内存机制与对象内存不同，它不是靠空闲列表申请的，而是直接移动指针，回收时直接合并
后面可以考虑将数据内存的常量放在一起，以后就不用扫描了。


垃圾回收采用局部扫描的策略，避免频繁的访问，一般情况下值扫描当前最新的field，如果空闲大小达到一定大小则可以对前面的field进行
扫描，否则逢5个field做一次大的扫描(FULL GC)
当然page的原理相同


需要处理两类重点的内存转化：一个是栈中间变量到堆变量、一个是常量到堆变量
用大根堆来计算page的剩余空间并合并

在申请内存和写入引用直接保证有引用计数
*/

static angel_memry angel_object_heap,angel_data_heap;
static block free_block;
object_string charset[cellnum];  //这里是英文的字符池和字节池
object_bytes byteset[cellnum];
object angel_true,angel_false,angel_null,angel_uninitial;
extern linkcollection angel_stack_list;
extern object_list global_value_list,obj_list;
extern classlist clist;


#define MEM_LOCK fast_lock();
#define MEM_UNLOCK fast_unlock();

field init_block_field();
void block_gc();
void page_gc();
void page_extend_gc();


angel_memry get_object_heap()
{
	return angel_object_heap;
}
angel_memry get_data_heap()
{
	return angel_data_heap;
}





#define ADDFREELIST_O(_free) _free->next = free_block->next; \
							free_block->next = _free; \
							angel_object_heap->free_size += _free->block_size;
#define TEST_O(o) ((object)o)


void addextendfield(angel_memry am,field f,int allocsize)
{
	f->next = am->extend_head->next;
	am->extend_head->next = f;
	am->extend_size += allocsize;
	am->total_size += allocsize;
}
void addfield(angel_memry am,field f)
{
	am->field_current->next = f;
	am->field_current = f;
	am->field_count++;
	am->total_size += max_block_size;
}
/*

初始化堆内存
*/
void lock_const_sector()
{
	;
}
void add_free_block(field f)
{
	block init = (block)f->memry;
	memset(init,0,max_block_size);
	init->block_size = max_block_size;
	init->next = free_block->next;
	free_block->next = init; //这将前面的圾回收的结果都淘汰到链的末尾。
}
field init_block_field()
{
	field f = (field)calloc(1,sizeof(fieldnode));
	char *addr = (char *)malloc(max_block_size);
	f->memry = addr;
	add_free_block(f);
	return f;
}
void init_page_field_info(field f,int allocsize=max_page_size)
{
	void *addr = f->memry;
	memset(addr,0,allocsize);
	f->free_size = allocsize;
	f->alloc_ptr = 0;
	page p = (page)addr;
	p->page_size = allocsize;
	p->ref = NULL;
}
field init_page_field(int allocsize = max_page_size)
{
	field f = (field)calloc(1,sizeof(fieldnode));
	char *addr = (char *)malloc(allocsize);
	f->memry = addr;
	init_page_field_info(f,allocsize);
	return f;
}
angel_memry init_block_heap()
{
	angel_memry am = (angel_memry)calloc(1,sizeof(angel_memrynode));
	am->total_size += max_block_size;
	am->field_head = am->field_current = init_block_field();
	am->field_count = 1;
	return am;
}
angel_memry init_page_heap()
{
	angel_memry am = (angel_memry)calloc(1,sizeof(angel_memrynode));
	am->total_size += max_page_size;
	am->field_head = am->field_current = init_page_field();
	am->extend_head = (field)calloc(1,sizeof(fieldnode));
	am->field_count = 1;
	return am;
}
object getpuppet()
{
	object res = (object)malloc(sizeof(int)*2);
	res->type = INT;
	res->refcount = 2;
	return res;
}
void init_block_const()
{
	angel_null = (object)init_perpetual_number();
	angel_null->type = NU;
	angel_true = (object)init_perpetual_number();
	angel_true->type = BOOLEAN;
	angel_false = (object)init_perpetual_number();
	angel_false->type = BOOLEAN;
	angel_uninitial = (object)init_perpetual_number();
	angel_uninitial->type = NOTYPE;
}
void init_page_const()
{
	object cellhead = getpuppet();
	object_stringnode *head = (object_stringnode *)angel_alloc_page(cellhead,(STRBASESIZE+4)*cellnum);
	for(int i = 0; i < cellnum; i++)
	{
		object_string item = &head[i];
		item->type = STR;
		item->refcount = 2;
		item->flag = FLAG_POOL;
		int16_t *content = (int16_t *)((char *)item+STRBASESIZE);
		content[0] = i;
		content[1] = 0;
		item->s = (char *)content;
		item->len = 2;
		charset[i] = item;
	}
	object_bytesnode *headb = (object_bytesnode *)angel_alloc_page(cellhead,(STRBASESIZE+4)*cellnum);
	for(int i = 0; i < cellnum; i++)
	{
		object_bytes item = &headb[i];
		item->type = BYTES;
		item->refcount = 2;
		item->flag = FLAG_POOL;
		char *content = ((char *)item+STRBASESIZE);
		content[0] = i;
		item->bytes = (char *)content;
		item->len = 1;
		byteset[i] = item;
	}
}
void init_heap()
{
	free_block = (block)calloc(1,sizeof(blocknode));
	angel_object_heap = init_block_heap();
	angel_data_heap = init_page_heap();
}
void free_field(field f)
{
	free(f->memry);
	free(f);
}
void reset_object_heap()
{
	free_block->next = NULL;
	field left = angel_object_heap->field_head;
	field p = left->next;
	left->next = NULL;
	while(p)
	{
		field temp = p;
		p = p->next;
		free_field(temp);
	}
	add_free_block(left);

	angel_object_heap->free_size = 0;
	angel_object_heap->total_size = max_block_size;
	
	angel_object_heap->field_current = angel_object_heap->field_head;
	angel_object_heap->field_count = 1;

	init_block_const();
}
void reset_data_heap()
{
	field left = angel_data_heap->field_head,p;
	field base = angel_data_heap->extend_head;
	p = base->next;
	while(p)
	{
		field temp = p;
		p = p->next;
		base->next = temp->next;
		free_field(temp);
	}
	angel_data_heap->extend_head->next = NULL;

	p = left->next;
	left->next = NULL;
	while(p)
	{
		field temp = p;
		p = p->next;
		free_field(temp);
	}
	init_page_field_info(left);
	
	angel_data_heap->free_size = 0;
	angel_data_heap->total_size = max_page_size;

	angel_data_heap->field_current = angel_data_heap->field_head;
	angel_data_heap->field_count = 1;


	init_page_const();
}
void reset_heap()
{
	reset_object_heap();
	reset_data_heap();
}





/*

内存的申请与释放
*/

#define CALLABLE(o) (o->refcount == 0)
inline int callable(object o,int mode)
{
	if(mode == 1)
		return !ISFLAGED(o);
	return o->refcount == 0;
}
__forceinline void dec_element(object p,int mode = 0)
{
	//任何一个对象在满足gc条件是都应该调用dec_element
	//注意本内存系统的引用计数加减与回收分两个不同阶段做
#define DEEP_DECREF(o) if(0 == --(o)->refcount) dec_element((object)o);
#define DECFIX(addr) ((page)(addr)-1)->ref = NULL;
	if(!p)return ;
	char *test = (char *)p;
	fun fp;
	object_ext ext;
	if(mode == 1)
	{
		p->refcount = 0;
		return ;
	}
	if(!ISDECED(p))
		p->extra_flag = IS_DECED;
	else
		return ;
	char *ss;
	switch(TEST_O(p)->type)
	{
	case NOTYPE:
	case INT:
	case FLOAT:
	case NU:
	case TRUE:
	case FALSE:
	case BOOLEAN:
		return ;
	case FUNP:
		if(ISUSERFUN(GETFUN(p)))
		{
			fp = GETFUN(p)->funinfo.user;
			if(fp->name) //有名字说明是公共函数
				return ;
			clearlink(fp->current_scope);
			deletetoken(fp->grammar);
			clearlink(fp->local_scope);
			free(fp);
		}
		return ;
	case REGULAR:
		freebytecode(GETREGULAR(p)->code);
		return ;
	case ITERATOR:
		DEEP_DECREF(GETITER(p)->base);
		return ;
	case SLICE:
		DEEP_DECREF(GETSLICE(p)->base);
		DEEP_DECREF(GETSLICE(p)->range);
		return ;
	case ENTRY:
		DEEP_DECREF(GETENTRY(p)->key);
		DEEP_DECREF(GETENTRY(p)->value);
		return ;
	case STR:
		ss = GETSTR(p)->s;
		if(GETSTR(p)->len >= PAGEALLOCMIN)
		{
			DECFIX(GETSTR(p)->s);
		}
		return ;
	case LIST:
		for(int i = 0; i<GETLIST(p)->len; i++)
		{
			object item = GETLIST(p)->item[i];
			if(item)
			{
				DEEP_DECREF(item);
			}
		}
		DECFIX(GETLIST(p)->item);
		return ;
	case SET:
		for(int i = 0; i<GETSET(p)->alloc_size; i++)
		{
			object item = GETSET(p)->element[i];
			if(item)
			{
				DEEP_DECREF(item);
			}
		}
		DECFIX(GETSET(p)->element);
		return ;
	case DICT:
		for(int i = 0; i<GETDICT(p)->alloc_size; i++)
		{
			object_entry entry = GETDICT(p)->hashtable[i];
			if(entry)
			{
				DEEP_DECREF(entry);
			}
		}
		DECFIX(GETDICT(p)->hashtable);
		return ;
	case OBJECT:
		p->mem_value->refcount = 0;
		dec_element((object)p->mem_value);
		p->pri_mem_value->refcount = 0;
		dec_element((object)p->pri_mem_value);
		return ;
	case EXT_TYPE:
		ext = (object_ext)p;
		if(ext->_type->dealloc)
			ext->_type->dealloc(p);
		return ;
	default:
		//可以针对拓展对象的析构函数进行
		return ;
	}
}
void free_extend_page()
{
	field pre = angel_data_heap->extend_head,p = pre->next;
	while(p)
	{
		object test = ((page)p->memry)->ref;
		if(test)  //超大快的内存极少使用，所以一旦回收可以立即释放
		{
			if(CALLABLE(test))
			{
				dec_element(test);
				goto _free_big;
			}
		}
		else
		{
_free_big:
			field temp = p;
			angel_data_heap->total_size -= temp->free_size;
			angel_data_heap->extend_size -= temp->free_size;
			p = temp->next;
			pre->next = p;
			free(temp->memry);
			free(temp);
			continue ;
		}
		pre = p;
		p = pre->next;
	}
}
object angel_alloc_block(int tsize)  //争取所有的对象大小都在16字节以内，当然字符串和列表除外
{
	object o;
	block p,pre;
	MEM_LOCK
alloc:
	for(pre=free_block,p=pre->next; p; pre=p,p=p->next)
	{
		//对于大对象第一次可能耗时较长
		int left = p->block_size-tsize;
		block old;
		if(left >= 0)  
		{
			o = (object)((char *)p+left);
			if(left < MINSIZE)
			{
				pre->next = p->next;
				*(char *)p = left; //很大胆的尝试，即要求一定要在字节表达允许范围内
			}
			else
				p->block_size = left;
			*(int *)o = 0;
			o->type = DRIFT_SIZE;
			o->refcount = 1;  //注意这里开始初始化为1
			o->osize = tsize;

			MEM_UNLOCK
			return o;
		}
	}
	//表明此时没有符合要求的
	block_gc();
	goto alloc;
}
void* angel_alloc_page(object head,int len)								
{
#define HUGE_SIZE 5
	page res;
	int allocsize = len + data_head;
	MEM_LOCK
	if(allocsize > max_page_size/2)  //巨大的内存申请
	{
		if(angel_data_heap->extend_size > max_page_size*20)
			page_extend_gc(); 
		field f = init_page_field(allocsize);
		res = (page)f->memry;
		addextendfield(angel_data_heap,f,allocsize);
		res->ref = head;
		
		MEM_UNLOCK
		return (void *)(res+1);
	}
alloc:
	ASCREF(head);
	for(field f = angel_data_heap->field_head; f; f = f->next)
	{
		if(f->flag == 1) continue ;
		if(f->free_size > allocsize) //第一次够分
		{
			//这里的申请是从前向后的的
			res = (page)((char *)f->memry+f->alloc_ptr);
			res->page_size = allocsize;
			res->ref = head;
			f->alloc_ptr += allocsize;
			f->free_size -= allocsize;

			
			DECREF(head);
			
			MEM_UNLOCK

			return (void *)(res+1);
		}
	}
	//开始垃圾回收
	page_gc();
	goto alloc;
}


void sys_realloc_list(object_list l,int resize)
{
	void *addr = l->item;
	angel_free_page(addr);
	l->item = (object *)malloc(sizeof(object)*resize);
}
object initext(int size)
{
	object ext = angel_alloc_block(size);
	ext->type = EXT_TYPE;
	return ext;
}
/*

垃圾回收
*/
#define GETPAGE(base,i) (page)((char *)base+i)
#define ISDRIFT(b) (TEST_O(b)->type < DRIFT_SIZE)
inline void flag(object o)
{
	ASCREF(o);
}
void deep_flag(object p)

{
#define FLAG_EXTRA(o) o->extra_flag = IS_FLAGED;
	//注意本内存系统的引用计数加减与回收分两个不同阶段做
	if(!p)return ;
	if(!ISFLAGED(p))
		return ;
	switch(p->type)
	{
	case NOTYPE:
	case FUNP:
	case INT:
	case FLOAT:
	case NU:
	case TRUE:
	case FALSE:
	case BOOLEAN:
	case STR:
	case OBJECT:
		break ;
	case ITERATOR:
		FLAG_EXTRA(GETITER(p)->base);
		break ;
	case SLICE:
		FLAG_EXTRA(GETSLICE(p)->base);
		FLAG_EXTRA(GETSLICE(p)->range);
		break ;
	case ENTRY:
		FLAG_EXTRA(GETENTRY(p)->key);
		FLAG_EXTRA(GETENTRY(p)->value);
		break ;
	case LIST:
		for(int i = 0; i<GETLIST(p)->len; i++)
		{
			object item = GETLIST(p)->item[i];
			if(item)
			{
				deep_flag(item);
			}
		}
		break ;
	case SET:
		for(int i = 0; i<GETSET(p)->alloc_size; i++)
		{
			object item = GETSET(p)->element[i];
			if(item)
			{
				deep_flag(item);
			}
		}
		break ;
	case DICT:
		for(int i = 0; i<GETDICT(p)->alloc_size; i++)
		{
			object_entry entry = GETDICT(p)->hashtable[i];
			if(entry)
			{
				deep_flag((object)entry);
			}
		}
		break ;
	default:
		//可以针对拓展对象的析构函数进行
		break ;
	}
	FLAG_EXTRA(p);
}
void flag(object o,int mode)
{
	if(!o)
		return ;
	if(mode == 0)
		flag(o);
	else
		deep_flag(o);
}
void angel_flag(int mode)  //即判断是否是当前的
{
	int i;
	object o,pre;
	//应该又对一些系统的列表进行标记

	for(i = global_value_list->alloc_size; i<global_value_list->len; i++){
		flag(global_value_list->item[i],mode);
	}
	for(linkcollection p = angel_stack_list->next; p != angel_stack_list; p = p->next)
	{
		runtime_stack stack = ((runable)p->data)->_stack;
		if(!stack) continue;
		for(int j = 0; j<stack->push_pos+1; j++)
			flag(stack->data[j],mode);
	}
}
inline void recovery_angel(object o)
{
	DECREF(o);
}
void deep_recovery_angel(object p)
{
#define RECOVERY_EXTRA(o) o->extra_flag = 0;
	//注意本内存系统的引用计数加减与回收分两个不同阶段做
	if(!p)return ;
	if(!ISFLAGED(p))
		return ;
	switch(p->type)
	{
	case NOTYPE:
	case FUNP:
	case INT:
	case FLOAT:
	case NU:
	case TRUE:
	case FALSE:
	case BOOLEAN:
	case STR:
	case OBJECT:
		break ;
	case ITERATOR:
		RECOVERY_EXTRA(GETITER(p)->base);
		break ;
	case SLICE:
		RECOVERY_EXTRA(GETSLICE(p)->base);
		RECOVERY_EXTRA(GETSLICE(p)->range);
		break ;
	case ENTRY:
		RECOVERY_EXTRA(GETENTRY(p)->key);
		RECOVERY_EXTRA(GETENTRY(p)->value);
		break ;
	case LIST:
		for(int i = 0; i<GETLIST(p)->len; i++)
		{
			object item = GETLIST(p)->item[i];
			deep_recovery_angel(item);
		}
		break ;
	case SET:
		for(int i = 0; i<GETSET(p)->alloc_size; i++)
		{
			object item = GETSET(p)->element[i];
			if(item)
			{
				deep_recovery_angel(item);
			}
		}
		break ;
	case DICT:
		for(int i = 0; i<GETDICT(p)->alloc_size; i++)
		{
			object_entry entry = GETDICT(p)->hashtable[i];
			if(entry)
			{
				deep_recovery_angel((object)entry);
			}
		}
		break ;
	case EXT_TYPE:
		 
	default:
		//可以针对拓展对象的析构函数进行
		break ;
	}
	RECOVERY_EXTRA(p);
}
void recovery_angel(object o,int mode)
{
	if(!o)
		return ;
	if(mode == 0)
		recovery_angel(o);
	else
		deep_recovery_angel(o);
}
void recovery_angel_flag(int mode)  //即判断是否是当前的
{
	int i;
	object o,pre;
	//应该又对一些系统的列表进行标记

	for(i = global_value_list->alloc_size; i<global_value_list->len; i++)
		recovery_angel(global_value_list->item[i],mode);
	for(linkcollection p = angel_stack_list->next; p != angel_stack_list; p = p->next)
	{
		runtime_stack stack = ((runable)p->data)->_stack;
		if(!stack) continue;
		for(int j = 0; j<stack->top+1; j++)
			recovery_angel(stack->data[j],mode);
	}
}
inline void flag_page(object o)  //其本质上就是选择性的对对象标记
{
	if(!o) return ;
	if(IS_GC_REFCOUNT_WAY(o))
	{
		ASCREF(o);
	}
}
void angel_flag_page()
{
	int i;
	object o,pre;
	//应该又对一些系统的列表进行标记

	for(i = global_value_list->alloc_size; i<global_value_list->len; i++)
		flag_page(global_value_list->item[i]);
	for(linkcollection p = angel_stack_list->next; p != angel_stack_list; p = p->next)
	{
		runtime_stack stack = ((runable)p->data)->_stack;
		if(!stack) continue;
		for(int j = 0; j<stack->push_pos+1; j++)
			flag_page(stack->data[j]);
	}
}
static void recovery_page(object o)
{
	if(!o) return ;
	if(IS_GC_REFCOUNT_WAY(o))
	{
		DECREF(o);
	}
}
void recovery_page_flag()  //即判断是否是当前的
{
	int i;
	object o,pre;
	//应该又对一些系统的列表进行标记

	for(i = global_value_list->alloc_size; i<global_value_list->len; i++)
		recovery_page(global_value_list->item[i]);
	for(linkcollection p = angel_stack_list->next; p != angel_stack_list; p = p->next)
	{
		runtime_stack stack = ((runable)p->data)->_stack;
		if(!stack) continue;
		for(int j = 0; j<stack->top+1; j++)
			recovery_page(stack->data[j]);
	}
}

typedef struct check_pagenode{
	int size;
	char type,flag,extra_flag;
	char refcount;
}check_page;

int count2 = 0;
block g_block;
void select_free_block(field scan,int mode)
{
	count2 = 0;
#define PRE_DEC_ELEMENT(o) dec_element(o,mode);
	free_block->next = NULL;
	page prepage;
	for(field f = scan; f; f=f->next)
	{
		angel_object_heap->field_current = f;
		char *base = (char *)f->memry,*p;
		p = base;
		while(p-base < max_block_size)  //直接遍历
		{
			int driftsize = -1;
			block _free;
			object test = (object)p;
			if(ISDRIFT(p))
			{
				driftsize = *p;
				p += driftsize;
				_free = (block)p;
				goto recycle;
			}
			if(callable(TEST_O(p),mode))  //未被引用
			{
				_free = (block)(p);
				int freesize=0;
				int flag = 0;
				object test = TEST_O(p);
				p += TEST_O(p)->osize;
				PRE_DEC_ELEMENT(test);
recycle:
				while(p-base < max_block_size)
				{
					test = TEST_O(p);
					if(ISDRIFT(p))
					{
						p += *p;
						continue ;
					}
					if(!callable(TEST_O(p),mode))  //表示此时要跳出来,
						break ;
					PRE_DEC_ELEMENT(TEST_O(p));
					p += TEST_O(p)->osize;
				}

				freesize = p-(char *)_free;
				if(freesize >= MINSIZE)
				{
					_free->block_size = freesize;
					ADDFREELIST_O(_free);
				}
				continue ;
			}
			p += TEST_O(p)->osize;
		}
		angel_object_heap->field_current = f;
	}
}
inline int _merge(page p,page q)
{
	int offset = q->page_size;
	object test = q->ref;
	memmove(p,q,offset);   //将q后面的字节copy到_free为首的内存中
	switch(test->type)
	{
	case STR:
		GETSTR(test)->s = (char *)(p+1);
		break ;
	case LIST:
		GETLIST(test)->item = (object *)(p+1);
		break ;
	case SET:
		GETSET(test)->element = (object *)(p+1);
		break ;
	case DICT:
		GETDICT(test)->hashtable = (object_entry *)(p+1);
		break ;
	}
	return offset;
}
void merge_page(field scan_field)
{
#define MERGE(_free,q) i += _merge(_free,q); merge += _free->page_size;
	int i=0,merge;
	//free_block->next = NULL;
	angel_data_heap->free_size = 0;
	for(field f = scan_field; f; f=f->next)
	{
		i = 0;
		page base = (page)f->memry,p;
		int end = f->alloc_ptr;
		
		merge = i;
		while(i < end)  //直接遍历
		{
			p = GETPAGE(base,i);
			page _free = GETPAGE(base,merge);
			object test = p->ref;
			if(!test) //那么后面的肯定都没有使用啦，直接返回即可
				goto _free_;
			if(CALLABLE(test))  //未被引用
			{
				dec_element((object)test);	
_free_:
				page q = p;
				int freesize=0;
				
				while(i < end)
				{
					q = GETPAGE(base,i);  //试探的块
					object tmp = q->ref;
					if(tmp)
					{
						if(!CALLABLE(tmp))  //表示此时要跳出来,
						{
							MERGE(_free,q);
							break ;
						}
						//这里再内存合并的时候就已经进行预处理了
						dec_element((object)tmp);
					}
					i += q->page_size;
				}
			}
			else 
			{
				MERGE(_free,p);
			}
		}
		//memset((char *)f->memry+merge,0,`-merge);
		f->alloc_ptr = merge;
		f->free_size = max_page_size - merge - 1;
		angel_data_heap->free_size += f->free_size;
		angel_data_heap->field_current = f;
	}
}
void merge_page()
{
	merge_page(angel_data_heap->field_current);
}
int block_gc_count= 0;
int g_test[max_block_size];
void gc_with_mode(field scan_field,int mode)
{
	stopworld();

	//这里要让flag与recover的空间是一样的
	angel_flag(mode);
	
	//merge_page(FLAG_FLAGCLEAN);
	select_free_block(scan_field,mode);  //选择性处理，scan_field是处理的起点,这个过程中也可以dec_element
	
	recovery_angel_flag(mode);

	goahead();
}
#define SWAP(i,j) {field temp; temp = base[i]; base[i] = base[j]; base[j] = temp;}
void ajust(field *base,int index,int size)
{
	int i = index;
	while(i*2 <= size)
	{
		int pi = i;
		i *= 2;
		field test = base[i];  //表示孩子中最小的一个
		if(i+1 <= size)
		{
			if(base[i]->alloc_ptr < base[i+1]->alloc_ptr)
			{
				test = base[i];
			}
			else
			{
				test = base[i+1];
				i++;
			}
		}
		if(base[pi]->alloc_ptr > test->alloc_ptr)
		{
			SWAP(pi,i);
		}
	}
}
field *createheap()
{
	int heap_size = angel_data_heap->field_count,i = 1;
	field *heap = (field *)calloc(heap_size+1,sizeof(field));
	field p = angel_data_heap->field_head;
	while(p)
	{
		heap[i++] = p;
		p = p->next;
	}
	for(int i = heap_size/2; i > 0; i--)
	{
		ajust(heap,i,heap_size);
	}
	return heap;
}
void _global_merge(field base,field merge)
{
	int i = 0;
	while(i < merge->alloc_ptr)
	{
		page p = GETPAGE(merge->memry,i);
		int freesize = p->page_size;
		_merge(GETPAGE(base->memry,base->alloc_ptr),p);
		base->alloc_ptr += freesize;
		base->free_size -= freesize;
		i += freesize;
	}
	merge->alloc_ptr = 0;
	merge->free_size = max_page_size;
}
int global_merge()
{
	field *base = createheap();
	int end = angel_data_heap->field_count;
	int totalsize = 0,select_count = 0;
	for(int i = 1; i <= angel_data_heap->field_count; i++)
	{
		if(base[i]->alloc_ptr > max_page_size/4)  //保证每次移动的元素不要太多
		{
			totalsize += base[i]->alloc_ptr;
			if(totalsize <= max_page_size)  //可以全局合并一个page
			{
				SWAP(i,end);
				end--;
				select_count++;
				ajust(base,1,end);
			}
			else
			{
				field empty_field = base[end+1];
				if(select_count == 1)
					goto exit;  //后面肯定不能实现合并
				for(int i = 1; i < select_count; i++)
				{
					int index = end+1+i;
					_global_merge(empty_field,base[index]); //进行全局合并
				}
			}
		}
		else
		{
exit:
			free(base);
			break ;
		}
	}
	return select_count;
}
void block_gc()
{
#define IS_GLOBAL_FLAG(heap_count) ((heap_count & (heap_count-1) == 0) && heap_count > 4)
#define STEP_SMALL 1/2
#define STEP_BIG 3/5
	angel_object_heap->free_size = 0;
	field scan_field;
	scan_field = angel_object_heap->field_current;
	int flag_mode = 0;
	int field_count = angel_object_heap->field_count;
	field oldcur = angel_object_heap->field_current;

	if(IS_GLOBAL_FLAG(field_count))//需要进行全局gc
	{
		block_gc_count = 0;
		flag_mode = 1;
	}

	gc_with_mode(scan_field,flag_mode);

	int freesize = angel_object_heap->free_size,totalsize;
	if(oldcur == angel_object_heap->field_head)
	{
		totalsize = angel_object_heap->total_size;
	}
	else
		totalsize = max_block_size;
	//对象内存永远都不会合并，只会做空闲列表，实在不行就申请新的空间，申请新的空间不行再考虑非对象内存（即对非对象内存进行回收，如果再不行就GG）
	
	if(field_count % 5 == 0)  //到达周期gc条件
	{
		if(angel_object_heap->flag == CYCLE_GC_FLAG)
		{
			angel_object_heap->flag = 0;
			if(freesize < totalsize*STEP_SMALL)  //一次周期gc之后还是很多垃圾
			{
				goto addfield;
			}
			else if(freesize < totalsize * STEP_BIG)
			{
				;
			}
			else
			{
				angel_object_heap->field_current = angel_object_heap->field_head;
			}
		}
		else
		{
			angel_object_heap->field_current = angel_object_heap->field_head;
			angel_object_heap->flag = CYCLE_GC_FLAG;
		}
		return ;
	}
	if(freesize < totalsize*STEP_SMALL)  //表示此时不在
	{
addfield:
		//开始开辟新的内存空间并将原来的淘汰掉
		field f = init_block_field();
		addfield(angel_object_heap,f);
		if(angel_data_heap->field_count % 5 == 0)
			angel_object_heap->field_current = angel_object_heap->field_head;
	}
}
void page_gc()
{
#define STEP 1/2
#define MERGE_STEP 1/3
	object test;

	field oldcur = angel_data_heap->field_current;


	stopworld();
	angel_flag_page();
	merge_page(angel_data_heap->field_current);
	recovery_page_flag();
	goahead();


	int freesize = angel_data_heap->free_size,totalsize;
	if(oldcur == angel_data_heap->field_head)
	{
		totalsize = angel_data_heap->total_size;
		int merge_count = global_merge();
		if(merge_count < angel_data_heap->field_count*MERGE_STEP)
			goto addfield;
	}
	else
		totalsize = max_page_size;

	if(angel_data_heap->field_count%4 == 0)  //一旦到5的倍数就要增加内存
	{
		if(freesize < totalsize*STEP)
		{
			goto addfield; //大规模的浪费
		}
		else//从头开始来
		{
			//要么垃圾多，要么到8的倍数强制回收
			angel_data_heap->field_current = angel_data_heap->field_head;  //维持
		}
		return ;
	}
	if(freesize < totalsize*STEP)  //表示此时不在
	{
addfield:
		//开始开辟新的内存空间并将原来的淘汰掉
		field f = init_page_field();
		addfield(angel_data_heap,f);
		if(angel_data_heap->field_count % 4 == 0)
			angel_data_heap->field_current = angel_data_heap->field_head;  //初始current指针
	}
}
void page_extend_gc()
{
	object test;
	angel_flag_page();
	free_extend_page();
	recovery_page_flag();
}




/*

各个对象的创建
*/
object_entry initentry(object key,object value)
{
	//这两个已经实现了ref加1
	STACKTOHEAP(key);
	STACKTOHEAP(value);


	object_entry res = (object_entry)angel_alloc_block(APPLYSIZE(sizeof(object_entrynode)));

	res->key = key;
	res->value = value;
	res->type = ENTRY;
	return res;
}
int getnumber_binary(int val)
{
	int count = 0;
	while(val)
	{
		val >>= 1;
		count++;
	}
	return count;
}
void free_perpetual(object o)
{
	o->refcount = 0;
	//angel_free_page(NULL,0);
}
object_slice initslice(object base,object_range range)
{
	STACKTOHEAP(base);
	ASCREF(range);
	object_slice res = (object_slice)angel_alloc_block(sizeof(object_slicenode));
	res->type = SLICE;
	res->base = base;
	res->range = range;
	return res;
}