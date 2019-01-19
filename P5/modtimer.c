
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/uaccess.h>
#include <asm-generic/errno.h>
#include <linux/semaphore.h>
#include <linux/kfifo.h>
#include <linux/random.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/list.h>
//#include <linux/ftrace.h>
#include <linux/spinlock.h>


MODULE_AUTHOR("Alberto Turegano Catedo");
MODULE_LICENSE("GPL");


#define MAX_CBUFFER_LEN 32
#define MAX_NORMAL_BUFFER 64


/*Parte TOP-HALF*/
DEFINE_SPINLOCK(sp);
struct timer_list my_timer; /* Structure that describes the kernel timer */
struct kfifo cbuffer;
unsigned long flags;


/*Parte BOTTOM-HALF*/
struct semaphore mtx;
int nr_waiting=0;  
int num_process_open=0;
struct workqueue_struct *my_wq; 
struct work_struct transfer_task;

/*Parte UPPER_LAYER*/
struct semaphore sem_list_empty;
unsigned long timer_period_ms=500;
unsigned int max_random=300;
int emergency_threshold=75;

struct list_head mylist;
struct list_item{
	int data;
    struct list_head links;
};

static struct proc_dir_entry *proc_entry_modtimer;
static struct proc_dir_entry *proc_entry_modconfig;

/*Cabeceras funciones auxiliares */
int drop_all_list(void);

static void copy_items_into_list(struct work_struct *work){
	struct list_item *item=NULL;
	int aux[MAX_CBUFFER_LEN/4];
	int cont = 0;
	int pos = 0;

	spin_lock_irqsave(&sp,flags);
	while(!kfifo_is_empty(&cbuffer)){
		kfifo_out(&cbuffer, &aux[cont], 4);
		cont++;

	}
	spin_unlock_irqrestore(&sp,flags);


	if (down_interruptible(&mtx))
			return -EINTR;

	while(cont > pos){
		//trace_printk("Elem: %i\n", aux[pos]);
		item = vmalloc(sizeof(struct list_item));
		item->data = aux[pos];

		list_add_tail(&item->links, &mylist);
		pos++;
	}
	up(&sem_list_empty);
	up(&mtx);

	printk("%i movidos del buffer a la lista\n", cont);
	
}

static void fire_timer(unsigned long data){
	unsigned long delay = msecs_to_jiffies(timer_period_ms);
	int r;

	r = (get_random_int()%max_random);

	printk("Numero generado: %i", r);

	spin_lock_irqsave(&sp,flags);
	kfifo_in(&cbuffer, &r, sizeof(r));

	if(kfifo_len(&cbuffer) >= ((MAX_CBUFFER_LEN*emergency_threshold)/100)){
		
		queue_work(my_wq, &transfer_task);
	}
	spin_unlock_irqrestore(&sp,flags);
	mod_timer(&(my_timer), jiffies + delay);
	
}

/*Operaciones modtimer*/

static int modtimer_open(struct inode *i, struct file *file){
	unsigned long delay; 

	if(down_interruptible(&mtx))
		return -EINTR;

	if(num_process_open == 0){
		printk("Entrando en el modulo, unico proceso\n");
		num_process_open++;
	}
	else{
		printk(KERN_INFO "One process has already opened modtimer, try later\n");
		up(&mtx);
		return -EACCES;
	}
	up(&mtx);

	delay = msecs_to_jiffies(timer_period_ms);
	
	my_timer.data=0;
    my_timer.function=fire_timer;
    my_timer.expires=jiffies + delay;
    
    add_timer(&my_timer);

    INIT_WORK(&transfer_task, copy_items_into_list);
    
    try_module_get(THIS_MODULE);
    return 0;

}


static int modtimer_release(struct inode *i, struct file *file){
	int retval = 0;

	del_timer_sync(&my_timer);
	flush_workqueue(my_wq);

	spin_lock_irqsave(&sp, flags);
	kfifo_reset(&cbuffer);
	spin_unlock_irqrestore(&sp, flags);

	retval = drop_all_list();

	num_process_open--;
	printk("Saliendo del modulo\n");
	module_put(THIS_MODULE);
	return retval;
}


static ssize_t modtimer_read(struct file *file, char *buff, size_t len, loff_t *off){
	char kbuff[MAX_NORMAL_BUFFER];
	struct list_item *item=NULL;
	struct list_head *cur_node=NULL;
	struct list_head* aux_node=NULL;
	char aux[6];
	char *dst = kbuff;
	unsigned int num_bytes=0;
	int retval=0;

	if (len < MAX_CBUFFER_LEN || len < MAX_NORMAL_BUFFER){ 
		printk(KERN_INFO "Error: user buffer lengh too big\n");
		return -ENOSPC;
	} 


	if (down_interruptible(&mtx))
		return -EINTR;

	while(list_empty(&mylist)){
		nr_waiting++;
		up(&mtx); 
		if (down_interruptible(&sem_list_empty)){
			down(&mtx); 
			nr_waiting--; 
			up(&mtx);
			return -EINTR;
		}
		if (down_interruptible(&mtx)) 
			return -EINTR;
	}
	list_for_each_safe(cur_node, aux_node, &mylist){
		item = list_entry(cur_node, struct list_item, links);
		num_bytes = sprintf(aux, "%i\n", item->data);
		printk("Mostrando y eliminando: %s\n", aux);
        if((num_bytes + dst) > &kbuff[MAX_NORMAL_BUFFER]){
            printk(KERN_INFO "Error: lengh list too big\n");
            retval=-ENOSPC;
            up(&mtx);
            goto exit;
        }
        else{
            strcpy(dst, aux);
            dst += num_bytes;
            list_del(cur_node);
            vfree(item);
        }

	}
	up(&mtx);
	num_bytes = dst - kbuff;

	if(copy_to_user(buff, kbuff, num_bytes)){
		printk(KERN_INFO "Error: copy to user\n");
        retval=-EFAULT;
	}

	(*off) += num_bytes;
	
    return num_bytes;
    
exit:
	return retval;

}


/*Operaciones modconfig*/

static ssize_t modconfig_write(struct file *file, const char *buff, size_t len, loff_t *off){
	char kbuff[32];
	int value;

	if ((*off) > 0) /* The application can write in this entry just once !! */
		return 0;	

	if(copy_from_user(kbuff, buff, len)){
		printk(KERN_INFO "Error: copy from user\n");
        return -ENOSPC;
	} 

	if(sscanf(kbuff, "timer_period_ms %i", &value) == 1){
		timer_period_ms = value;
	}else 
		if(sscanf(kbuff, "max_random %i", &value) == 1){
			max_random = value;
	}
	else 
		if(sscanf(kbuff, "emergency_threshold %i", &value) == 1){
			emergency_threshold = value;
	}
	else{
		printk(KERN_INFO "Incorrect value\n");
		return -EINVAL;
	}

	return len;

}

static ssize_t modconfig_read(struct file *file, char *buff, size_t len, loff_t *off){
	char kbuff[MAX_NORMAL_BUFFER];
	unsigned int num_bytes=0;

	if(*off > 0){
		return 0;
	}
	
	num_bytes=sprintf(kbuff, "timer_period_ms=%i\nmax_random=%i\nemergency_threshold=%i\n", timer_period_ms, max_random, emergency_threshold);

	if(num_bytes > MAX_NORMAL_BUFFER){
		return -ENOSPC;
	}

	if(copy_to_user(buff, kbuff, num_bytes)){
		printk(KERN_INFO "Error: copy to user\n");
		return -EFAULT;
	}


	(*off) += num_bytes;

	return num_bytes;
}



static const struct file_operations proc_modtimer_fops = {
	.open = modtimer_open,
	.read = modtimer_read,
	.release = modtimer_release
};

static const struct file_operations proc_modconfig_fops = {
	.read = modconfig_read,
	.write = modconfig_write
};

int init_module_modtimer(void){
	
	int retval;

	retval=kfifo_alloc(&cbuffer, MAX_CBUFFER_LEN, GFP_KERNEL);
	
	my_wq=create_workqueue("my_queue");
	
	if(retval == -1 || my_wq == NULL)
		return -EFAULT;

	init_timer(&my_timer);
	sema_init(&mtx,1);
	sema_init(&sem_list_empty,1);

    proc_entry_modtimer=proc_create_data("modtimer",0666, NULL, &proc_modtimer_fops, NULL);
    proc_entry_modconfig=proc_create_data("modconfig", 0666, NULL, &proc_modconfig_fops, NULL);


    if(proc_entry_modtimer == NULL || proc_entry_modconfig == NULL){
    	kfifo_free(&cbuffer);
    	destroy_workqueue(my_wq);
    	return -EFAULT;
    }

    INIT_LIST_HEAD(&mylist);

    return 0;
}


void exit_module_modtimer(void){
	destroy_workqueue(my_wq);
	spin_lock_irqsave(&sp, flags);
	kfifo_free(&cbuffer);
	spin_unlock_irqrestore(&sp, flags);
	remove_proc_entry("modconfig", NULL);
	remove_proc_entry("modtimer", NULL);
}

module_init(init_module_modtimer);
module_exit(exit_module_modtimer);

/*Funciones auxiliares*/

int drop_all_list(void){
	struct list_item* item=NULL;
    struct list_head* cur_node=NULL;
   	struct list_head* aux_node=NULL;
    
	if (down_interruptible(&mtx))
		return -EINTR;

    list_for_each_safe(cur_node, aux_node, &mylist){
    /* cur_node: posicion actual, tipo donde esta list_head, links_ variable list_head */
        item = list_entry(cur_node, struct list_item, links);
        list_del(cur_node);
        vfree(item);
    }
    printk("Lista borrada\n");
    up(&mtx);
    return 0;
}