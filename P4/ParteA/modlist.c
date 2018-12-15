/*MODLIST ALBERTO TUREGANO*/

#include <linux/module.h>     //necesario en todos los modulos
#include <linux/kernel.h>     //necesario para KEER_INFO
#include <linux/proc_fs.h>    //necesario para dar operaciones al modulo en proc
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/uaccess.h>
#include <linux/list.h>
#include <linux/ftrace.h>
#include <linux/spinlock.h>

MODULE_LICENSE("GPL");

#define procfs_name "modlist_SMP-safe"
#define MAX_BUFFER_SIZE 100
DEFINE_SPINLOCK(sp);
//Entrada para añadir el modulo al sistema proc
struct proc_dir_entry *proc_file;

//Nuestra lista
struct list_head mylist;

//Estructura Nodo de la lista
struct list_item{
    int data;
    struct list_head links;
};

void drop_just_one(int n);
void drop_all_list(void);

static ssize_t myproc_read(struct file *filp, char __user *buf, size_t len, loff_t *off){
	struct list_item* item=NULL;
	struct list_head* cur_node=NULL;
	char kbuf[MAX_BUFFER_SIZE + 1];
	char *dst = kbuf;
    char aux[6];
	int num_bytes=0;
    int retval = 0;

	if ((*off) > 0) /* The application can write in this entry just once !! */
		return retval;		


	spin_lock(&sp);
	list_for_each(cur_node, &mylist){
		item = list_entry(cur_node, struct list_item, links);
		num_bytes = sprintf(aux, "%i\n", item->data);
        if((num_bytes + dst) > &kbuf[MAX_BUFFER_SIZE]){
            printk(KERN_INFO "Error: lengh list too big\n");
            retval=-ENOSPC;
            spin_unlock(&sp);
            goto exit;
        }
        else{
            strcpy(dst, aux);
            dst += num_bytes;
            memset(aux,0,strlen(aux));
        }
	}
	spin_unlock(&sp);

	num_bytes = dst - kbuf;

	if(copy_to_user(buf, kbuf, num_bytes)){
		printk(KERN_INFO "Error: copy to user\n");
        retval=-EFAULT;
	}


    (*off) += num_bytes;
    return num_bytes;

exit:
    return retval;
}

/*  buf: buffer por donde pasa datos el usuario,
    len: tamaño buffer,
    off: puntero de posicion en el buffer de entrada salida */

static ssize_t myproc_write(struct file *filp, const char __user *buf, size_t len, loff_t *off){
    int n = 0;
	char kbuf[MAX_BUFFER_SIZE];
	char clean_up[MAX_BUFFER_SIZE];
    struct list_item *nodo;
    
	if ((*off) > 0) /* The application can write in this entry just once !! */
		return 0;	

    /* Possible Err */
    if(len > MAX_BUFFER_SIZE){
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

    /*Devuelve el numero de palabras que han podido ser leidas*/
    if(sscanf(kbuf, "add %i", &n) == 1){
		nodo = vmalloc(sizeof(struct list_item));
		nodo->data = n;

		spin_lock(&sp);
		list_add_tail(&nodo->links, &mylist);
		spin_unlock(&sp);


   	}else 
   		if (sscanf(kbuf, "remove %i", &n) == 1){
			drop_just_one(n);
	}else 
		if (sscanf(kbuf, "%s", clean_up) == 1){
			if (strcmp(clean_up, "cleanup") == 0)
				drop_all_list();
	}else {
		printk(KERN_INFO "Comando no valido\n");
		return -EINVAL;
	}
    return len;
}


static const struct file_operations proc_entry_fops = {
    .read = myproc_read,
    .write = myproc_write
};

void drop_just_one(int n){
	struct list_item* item=NULL;
    struct list_head* cur_node=NULL;
   	struct list_head* aux_node=NULL;

   	spin_lock(&sp);
	list_for_each_safe(cur_node, aux_node, &mylist){
		item = list_entry(cur_node, struct list_item, links);
		if(item->data == n){	
			list_del(cur_node);
            vfree(item);
		}
	}
	spin_unlock(&sp);
}

void drop_all_list(void){
	struct list_item* item=NULL;
    struct list_head* cur_node=NULL;
   	struct list_head* aux_node=NULL;
    
	spin_lock(&sp);
        list_for_each_safe(cur_node, aux_node, &mylist){
        /* cur_node: posicion actual, tipo donde esta list_head, links_ variable list_head*/
            item = list_entry(cur_node, struct list_item, links);
            list_del(cur_node);
            vfree(item);
        }
        spin_unlock(&sp);    
}


int modlist_init(void){
    /*creamos la entrada /proc/file */
    /* dir padre = NULL*/
    proc_file = proc_create(procfs_name, 0666, NULL, &proc_entry_fops);
    if(proc_file == NULL){
        printk("Error: Imposible inicializar el /proc/%s\n", procfs_name);
        return -ENOMEM;
    }
    INIT_LIST_HEAD(&mylist);
    return 0;
}


void modlist_clean(void){
    drop_all_list();
    remove_proc_entry(procfs_name,NULL);
}

module_init( modlist_init );
module_exit( modlist_clean );

