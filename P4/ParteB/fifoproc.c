/*fifoproc.c*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/uaccess.h>
#include <asm-generic/errno.h>
#include <linux/semaphore.h>
#include <linux/kfifo.h>

MODULE_AUTHOR("Alberto Turégano Castedo");
MODULE_LICENSE("GPL");

#define proc_entry_name "fifoproc"
#define MAX_CBUFFER_LEN 64
#define MAX_KBUFF 128

/* Número de procesos que abrieron y cerraron la entrada
/proc para escritura (productores) */
int prod_count = 0; 
int cons_count = 0; 

/* para garantizar Exclusión Mutua */ 
struct semaphore mtx; 

/* cola de espera para productor(es) */ 
struct semaphore sem_prod,sem_cons; 

/* Número de procesos productores esperando */
int nr_prod_waiting=0;  

/* Número de procesos consumidores esperando */
int nr_cons_waiting=0; 

static struct proc_dir_entry *proc_entry;
struct kfifo cbuffer;

static int fifoproc_open(struct inode *i, struct file *file){


	if (down_interruptible(&mtx))
			return -EINTR;


	if (file->f_mode & FMODE_READ)
	{
			/* Un consumidor abrio el fifo */
		cons_count++;

		if(nr_prod_waiting > 0){
			up(&sem_prod);	
			nr_prod_waiting--;
			trace_printk("Despierto al productor\n");
		}

		while(prod_count == 0){
			trace_printk("Estoy esperando a un productor\n");
			nr_cons_waiting++;
			up(&mtx); /* "Libera" el mutex */ 
			/* Se bloquea en la cola */
			if (down_interruptible(&sem_cons)){
				down(&mtx);
				nr_cons_waiting--; 
				up(&mtx); 
				return -EINTR;
			}
			/* "Adquiere" el mutex */
			if (down_interruptible(&mtx))
				return -EINTR;
		}
	} 
	else{
			/* Un productor abrió el FIFO */
		prod_count++;

		if(nr_cons_waiting > 0){
			up(&sem_cons);
			nr_cons_waiting--;
			trace_printk("Despierto a un consumidor\n");
		}

		while(cons_count == 0){
			trace_printk("Estoy esperando a un consumidor\n");
			nr_prod_waiting++;
			up(&mtx); /* "Libera" el mutex */ 
			/* Se bloquea en la cola */
			if (down_interruptible(&sem_prod)){
				down(&mtx);
				nr_prod_waiting--; 
				up(&mtx); 
				return -EINTR;
			}
			/* "Adquiere" el mutex */
			if (down_interruptible(&mtx))
				return -EINTR;
		}

	}

	up(&mtx);

	return 0;

}

static int fifoproc_release(struct inode *i, struct file *file){

	if (down_interruptible(&mtx))
			return -EINTR;

	if (file->f_mode & FMODE_READ){

		cons_count--;
   		
		if (nr_prod_waiting>0) {
		up(&sem_prod);
		nr_prod_waiting--;
		}
	}
	else{

		prod_count--;
		
		if (nr_cons_waiting>0) {
		up(&sem_cons);
		nr_cons_waiting--;
		}
	}

	if(cons_count == 0)
		kfifo_reset(&cbuffer); 
	

	up(&mtx);

	return 0;
}
static ssize_t fifoproc_read(struct file *file, char *buff, size_t len, loff_t *off){
	char kbuffer[MAX_KBUFF];
	int items_copy = 0;
	
	//Comprobar que el tamaño del buffer es mayor que el de kfifo 
	if (len> MAX_CBUFFER_LEN || len> MAX_KBUFF){ 
		printk(KERN_INFO "Error: user buffer lengh too big\n");
		return -ENOSPC;
	} 

	//bloquear SC
	if (down_interruptible(&mtx))
			return -EINTR;

	//Nos bloqueamos si no tenemos todos los datos que nos piden
	while(kfifo_len(&cbuffer)< len && prod_count>0){
		nr_cons_waiting++;
		up(&mtx); /* "Libera" el mutex */ 
		/* Se bloquea en la cola */
		if (down_interruptible(&sem_cons)){
			down(&mtx);
			nr_cons_waiting--; 
			up(&mtx); 
			return -EINTR;
		}
		/* "Adquiere" el mutex */
		if (down_interruptible(&mtx))
			return -EINTR;
	}

	//Si todos los productores se salen nos salimos
	if(prod_count == 0 && kfifo_is_empty(&cbuffer)){
		printk(KERN_INFO "Todos los productores se han cerrado.\n");
		up(&mtx);
		return 0;
	}

	items_copy = kfifo_out(&cbuffer,kbuffer,len);
	
	if (nr_prod_waiting>0) {
   		/* Despierta a uno de los hilos bloqueados */
		up(&sem_prod);
		nr_prod_waiting--;
	}

	up(&mtx);
	
	if(copy_to_user(buff, kbuffer, items_copy)){
		printk(KERN_INFO "Error: can not copy kfifo to user buffer\n");
		return -EFAULT;
	}

	return items_copy;

}
static ssize_t fifoproc_write(struct file *file, const char *buff, size_t len, loff_t *off){
	char kbuffer[MAX_KBUFF];
	unsigned int i;

	if (len> MAX_CBUFFER_LEN || len> MAX_KBUFF){ 
		printk(KERN_INFO "Error: user buffer lengh too big\n");
		return -ENOSPC;
	} 

	if (copy_from_user(kbuffer,buff,len)){ 
		printk(KERN_INFO "Error: imposible to copy from user buffer\n");
		return -EFAULT;
	}

	if (down_interruptible(&mtx))
		return -EINTR;

	while (kfifo_avail(&cbuffer)<len && cons_count>0){ 
		nr_prod_waiting++;
		up(&mtx); /* "Libera" el mutex */ 
		/* Se bloquea en la cola */
		if (down_interruptible(&sem_prod)){
			down(&mtx);
			nr_prod_waiting--; 
			up(&mtx); 
			return -EINTR;
		}
		/* "Adquiere" el mutex */
		if (down_interruptible(&mtx))
			return -EINTR;
	}


	if (cons_count==0) {
		printk(KERN_INFO "Productor: no hay consumidores\n");
		up(&mtx); 
		return -EPIPE;
	}

	kfifo_in(&cbuffer,kbuffer,len);
	
	if (nr_cons_waiting>0) {
   		/* Despierta a uno de los hilos bloqueados */
		up(&sem_cons);
		nr_cons_waiting--;
	}

	up(&mtx); 

	return len;
}



static const struct file_operations proc_entry_fops = {
	.open = fifoproc_open,
	.release = fifoproc_release,
	.read = fifoproc_read,
	.write = fifoproc_write
};


int init_module_fifo(void){
	int retval;

	retval=kfifo_alloc(&cbuffer, MAX_CBUFFER_LEN, GFP_KERNEL);

	if(retval)
		return -ENOMEM;

	sema_init(&sem_prod,0);
	sema_init(&sem_cons,0);
	sema_init(&mtx,1);

	proc_entry = proc_create_data(proc_entry_name,0666, NULL, &proc_entry_fops, NULL);
	if(proc_entry == NULL){
		kfifo_free(&cbuffer);
		//mostrar error
		return -ENOMEM;
	}
	return 0;
}

void exit_module_fifo(void){
	//borrar entrada de proc
	remove_proc_entry(proc_entry_name, NULL);
	//liberar memoria
	kfifo_free(&cbuffer);
}


module_init( init_module_fifo );
module_exit( exit_module_fifo );