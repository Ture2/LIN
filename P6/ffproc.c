/*fifoproc.c*/

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/uaccess.h>
#include <asm-generic/errno.h>
#include <linux/semaphore.h>
#include <linux/kfifo.h>
#include <linux/list.h>

MODULE_AUTHOR("Alberto Turégano Castedo");
MODULE_LICENSE("GPL");

#define proc_entry_name "fifoproc"
#define MAX_CBUFFER_LEN 64
#define MAX_KBUFF 128

DEFINE_SPINLOCK(sp);

struct private_data{
	char name[20];
	int prod_count; 
	int cons_count; 		
	int nr_prod_waiting;  
	int nr_cons_waiting; 	
	struct semaphore mtx; 
	struct semaphore sem_prod;
	struct semaphore sem_cons; 
	struct kfifo cbuffer; 	
	struct list_head links;
};

struct list_head myfifos;

static struct proc_dir_entry *control_entry;
static struct proc_dir_entry *parent;
static struct proc_dir_entry *proc_entry;

struct private_data *default_data=NULL;

static int max_entries=4;
static int max_size=64;

int current_entries=1;

module_param(max_entries, int, 0644);
module_param(max_size, int, 0644);

int fifo_add(char *name);
int fifo_delete(char *name, int all);


/*	The next  4 functions are exactly the same than in p4, just
	change the use of a private data structure which contains 
	all the semaphores, mutex, private kfifo and the link head 
	that need this functions to work*/

static int fifoproc_open(struct inode *i, struct file *file){
	struct private_data *pd =(struct private_data*)PDE_DATA(file->f_inode);
	
	if (down_interruptible(&(pd->mtx)))
			return -EINTR;


	if (file->f_mode & FMODE_READ)
	{
			
		pd->cons_count++;
		if(pd->nr_prod_waiting > 0){
			up(&(pd->sem_prod));	
			pd->nr_prod_waiting--;
		}

		while(pd->prod_count == 0){
			pd->nr_cons_waiting++;
			up(&(pd->mtx)); 
			
			if (down_interruptible(&(pd->sem_cons))){
				down(&(pd->mtx));
				pd->nr_cons_waiting--; 
				up(&(pd->mtx)); 
				return -EINTR;
			}
			
			if (down_interruptible(&(pd->mtx)))
				return -EINTR;
		}
	} 
	else{
			
		pd->prod_count++;

		if(pd->nr_cons_waiting > 0){
			up(&(pd->sem_cons));
			pd->nr_cons_waiting--;
		}

		while(pd->cons_count == 0){
			pd->nr_prod_waiting++;
			up(&(pd->mtx)); 
			
			if (down_interruptible(&(pd->sem_prod))){
				down(&(pd->mtx));
				pd->nr_prod_waiting--; 
				up(&(pd->mtx)); 
				return -EINTR;
			}
			
			if (down_interruptible(&(pd->mtx)))
				return -EINTR;
		}

	}

	up(&(pd->mtx));

	return 0;

}

static int fifoproc_release(struct inode *i, struct file *file){
    struct private_data *pd=(struct private_data*)PDE_DATA(file->f_inode);
	

	if (down_interruptible(&(pd->mtx)))
			return -EINTR;

	if (file->f_mode & FMODE_READ){

		pd->cons_count--;
   		
		if (pd->nr_prod_waiting>0) {
		up(&(pd->sem_prod));
		pd->nr_prod_waiting--;
		}
	}
	else{

		pd->prod_count--;
		
		if (pd->nr_cons_waiting>0) {
		up(&(pd->sem_cons));
		pd->nr_cons_waiting--;
		}
	}

	if(pd->cons_count == 0)
		kfifo_reset(&(pd->cbuffer)); 
	

	up(&(pd->mtx));

	return 0;
}

static ssize_t fifoproc_read(struct file *file, char *buff, size_t len, loff_t *off){
	struct private_data *pd=(struct private_data*)PDE_DATA(file->f_inode);
	char kbuffer[MAX_KBUFF];
	int items_copy = 0;
	
	if (len> MAX_CBUFFER_LEN || len> MAX_KBUFF){ 
		printk(KERN_INFO "Error: user buffer lengh too big\n");
		return -ENOSPC;
	} 

	if (down_interruptible(&(pd->mtx)))
			return -EINTR;

	
	while(kfifo_len(&(pd->cbuffer))< len && pd->prod_count>0){

		pd->nr_cons_waiting++;
		up(&(pd->mtx)); 
		
		if (down_interruptible(&(pd->sem_cons))){
			down(&(pd->mtx));
			pd->nr_cons_waiting--; 
			up(&(pd->mtx)); 
			return -EINTR;
		}
		
		if (down_interruptible(&(pd->mtx)))
			return -EINTR;
	}

	if(pd->prod_count == 0 && kfifo_is_empty(&(pd->cbuffer))){
		printk(KERN_INFO "Todos los productores se han cerrado.\n");
		up(&(pd->mtx));
		return 0;
	}

	items_copy = kfifo_out(&(pd->cbuffer),kbuffer,len);
	
	if (pd->nr_prod_waiting>0) {
		up(&(pd->sem_prod));
		pd->nr_prod_waiting--;
	}

	up(&(pd->mtx));
	
	if(copy_to_user(buff, kbuffer, items_copy)){
		printk(KERN_INFO "Error: can not copy kfifo to user buffer\n");
		return -EFAULT;
	}

	return items_copy;

}
static ssize_t fifoproc_write(struct file *file, const char *buff, size_t len, loff_t *off){
	struct private_data *pd=(struct private_data*)PDE_DATA(file->f_inode);
	char kbuffer[MAX_KBUFF];

	if (len> MAX_CBUFFER_LEN || len> MAX_KBUFF){ 
		printk(KERN_INFO "Error: user buffer lengh too big\n");
		return -ENOSPC;
	} 

	if (copy_from_user(kbuffer,buff,len)){ 
		printk(KERN_INFO "Error: imposible to copy from user buffer\n");
		return -EFAULT;
	}

	if (down_interruptible(&(pd->mtx)))
		return -EINTR;

	while (kfifo_avail(&(pd->cbuffer))<len && pd->cons_count>0){ 
		pd->nr_prod_waiting++;
		up(&(pd->mtx));  
		
		if (down_interruptible(&(pd->sem_prod))){
			down(&(pd->mtx));
			pd->nr_prod_waiting--; 
			up(&(pd->mtx)); 
			return -EINTR;
		}
		
		if (down_interruptible(&(pd->mtx)))
			return -EINTR;
	}


	if (pd->cons_count==0) {
		printk(KERN_INFO "Productor: no hay consumidores\n");
		up(&(pd->mtx)); 
		return -EPIPE;
	}

	kfifo_in(&(pd->cbuffer),kbuffer,len);
	
	if (pd->nr_cons_waiting>0) {
   		
		up(&(pd->sem_cons));
		pd->nr_cons_waiting--;
	}

	up(&(pd->mtx)); 

	return len;
}


/* 	How works:
	This function allows to user to add or delete a new fifo using the write option in file_operations, 
	parsing the user buffer to know what to do
*/

static ssize_t controlproc_write(struct file *filp, const char __user *buf, size_t len, loff_t *off){
	char name[20];
	char kbuf[MAX_KBUFF];
    
	if ((*off) > 0) /* The application can write in this entry just once !! */
		return 0;	

    /* Possible Err */
    if(len > MAX_KBUFF){
        printk(KERN_INFO "Error: No hay suficiente espacio en el buffer\n");
        return -ENOSPC;
    }
    
    //to , from , lengh
    if(copy_from_user(kbuf, buf, len)){
		printk(KERN_INFO "Error: copy from user\n");
        return -EFAULT;
	}

    kbuf[len] = '\n';
    *off += len;

    if(sscanf(kbuf, "create %s", name) == 1){

    	if((current_entries + 1) <= max_entries)
			fifo_add(name);
		else{
			printk(KERN_INFO "No space for more entries");
			return -ENOSPC;
		}
	}

	if(sscanf(kbuf, "delete %s", name) == 1){
		if(fifo_delete(name, 0) == -1){
			printk(KERN_INFO "No existe el fifo insertado");
			return -ENOENT;
		}

	}

	return len;
}

static const struct file_operations proc_entry_fops = {
	.open = fifoproc_open,
	.release = fifoproc_release,
	.read = fifoproc_read,
	.write = fifoproc_write
};


static const struct file_operations proc_control_fops = {
	.write = controlproc_write
};


/*	fifo_add parameter:
	name -> the name that we wanna give to the fifo, and also the entry on the proc file system

	How works:
	This function creates a new private_data structure for the fifo,
	initializing it and adding to the proc dir
*/
int fifo_add(char *name){
	struct private_data *pd = vmalloc(sizeof(struct private_data));
	//struct kfifo localkfifo;

	if(strlen(name) > 20){
		return -EINVAL;
	}

	if(kfifo_alloc(&(pd->cbuffer), MAX_CBUFFER_LEN, GFP_KERNEL)){
		vfree(pd);
		printk(KERN_INFO "Couldn't alloc memory for fifo");
		return -ENOMEM;
	}

	sema_init(&(pd->sem_prod),0);
	sema_init(&(pd->sem_cons),0);
	sema_init(&(pd->mtx),1);

	strcpy(pd->name, name);
	pd->prod_count=0; 
	pd->cons_count=0; 		
	pd->nr_prod_waiting=0;
	pd->nr_cons_waiting=0; 	

	
	proc_entry = proc_create_data(name, 0666, parent, &proc_entry_fops, (void *)pd);

    if (proc_entry == NULL) {
		kfifo_free(&(pd->cbuffer));
        vfree(pd);
		printk(KERN_INFO "Couldn't create the fifo: %s", name);
		return -ENOMEM;
	}


	spin_lock(&sp);
	list_add_tail(&(pd->links), &myfifos);
	current_entries++;
	spin_unlock(&sp);

	return 0;
}


/* 	fifo_delete parameters:
	name -> the name of the fifo to drop out, NULL droping all the fifos
	all  -> flag with value 0 to drop just one and value 1 to drop all the list
*/

int fifo_delete(char *name, int all){
	int find=0;
	struct private_data* pd=NULL;
    struct list_head* cur_node=NULL;
   	struct list_head* aux_node=NULL;

   	spin_lock(&sp);
	list_for_each_safe(cur_node, aux_node, &myfifos){
		pd = list_entry(cur_node, struct private_data, links);
		if(all){
			list_del(cur_node);
			kfifo_free(&(pd->cbuffer));

			spin_unlock(&sp);
			remove_proc_entry(pd->name, parent);
			spin_lock(&sp);

            vfree(pd);
            
		}else
			if(strcmp(pd->name,name) == 0){
				find = 1;
				list_del(cur_node);
				kfifo_free(&(pd->cbuffer));

				spin_unlock(&sp);
				remove_proc_entry(pd->name, parent);
				spin_lock(&sp);
				current_entries--;
	            vfree(pd);
            	break;
            }
	}
	spin_unlock(&sp);

	if((find == 0) && (all == 0)){
		return -1;
	}
	
	return 0;
}

/*This function checks if the number given in max_entries by the user is power of two,
	remember that kfifo_alloc works with power of 2 numbers*/
int power_of_two(int num){
	if((num != 0) && ((num &(num - 1)) == 0))
      return 1;
  	else return 0;
}

int init_module_fifo(void){
	int retval;

	if(max_entries < 0 || power_of_two(max_size) == 0){
		printk(KERN_INFO "The parameters are incorrect, must be higher than 0 and power of two (max_size)\n");
		return -EINVAL;
	}

	parent = proc_mkdir("fifo", NULL);

	INIT_LIST_HEAD(&myfifos);

	if(parent == NULL){
		printk(KERN_INFO "Couldn't create the father dir\n");
		return -ENOMEM;
	}
	
	control_entry = proc_create_data("control", 0666, parent, &proc_control_fops, NULL);
	retval = fifo_add("default");

	if(control_entry == NULL || retval == -1){
		remove_proc_entry("fifo", NULL);
		printk(KERN_INFO "Couldn't create the default fifo\n");
		return -ENOMEM;
	}
	
	return 0;
}

void exit_module_fifo(void){
	
	fifo_delete(NULL, 1);

	/*Delete the proc entries*/
	remove_proc_entry("control", parent);
	remove_proc_entry("fifo", NULL);

}


module_init( init_module_fifo );
module_exit( exit_module_fifo );