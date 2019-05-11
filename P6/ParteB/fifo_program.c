#include <getopt.h>
#include <stdio.h>
#include <sys/types.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <err.h>
#include <errno.h>
#define MAX_MESSAGE_SIZE 32

char* nombre_programa=NULL;
char* path_control="/proc/fifo/control";
char* path="/proc/fifo/";

struct fifo_message {
	unsigned int nr_bytes;
	char data[MAX_MESSAGE_SIZE];
};


static int fifo_modify(const char *name, int flag){
  ssize_t nbytes=0;
  int fd=0;
  char fifo[16];
  char buff[30];

  if(sscanf(name, "%s", &fifo) != 1){
    printf("Name not valid\n");
    perror("Following error occurred");
  }
  else{

    if(flag == 0)
      strcpy(buff, "create");
    else 
      strcpy(buff, "delete");

    strcat(buff, name);
  }

  fd = open(path_control, O_WRONLY);

  nbytes = write(fd, buff, 20);

  if(fd > 0)
    perror("Following error occurred ");
  else
    if(nbytes != -1){
      close(fd);
      perror("Following error occurred ");
    }
  else
    printf("Operation completed successfully.\n", name);

  close(fd);

  return 0;
}

static void fifo_send (const char* path_fifo) {
  struct fifo_message message;
  int fd_fifo=0;
  int bytes=0,wbytes=0;
  const int size=sizeof(struct fifo_message);

    fd_fifo=open(path_fifo,O_WRONLY);

  if (fd_fifo<0) {
	perror(path_fifo);
	exit(1);
  }
  
 /* Bucle de envío de datos a través del FIFO
    - Leer de la entrada estandar hasta fin de fichero
 */
  while((bytes=read(0,message.data,MAX_MESSAGE_SIZE))>0) {
	message.nr_bytes=bytes;
	wbytes=write(fd_fifo,&message,size);

	if (wbytes > 0 && wbytes!=size) {
		fprintf(stderr,"Can't write the whole register\n");
		exit(1);
  	}else if (wbytes < 0){
		perror("Error when writing to the FIFO\n");
		exit(1);
  	}		
  }
  
  if (bytes < 0) {
	fprintf(stderr,"Error when reading from stdin\n");
	exit(1);
  }
  
  close(fd_fifo);
}

static void fifo_receive (const char* path_fifo) {
  struct fifo_message message;
  int fd_fifo=0;
  int bytes=0,wbytes=0;
  const int size=sizeof(struct fifo_message);

  
    fd_fifo=open(path_fifo,O_RDONLY);
  

  if (fd_fifo<0) {
	perror(path_fifo);
	exit(1);
  }


  while((bytes=read(fd_fifo,&message,size))==size) {
	/* Write to stdout */
	wbytes=write(1,message.data,message.nr_bytes);
	
	if (wbytes!=message.nr_bytes) {
		fprintf(stderr,"Can't write data to stdout\n");
		exit(1);
  	}	
 }

  if (bytes > 0){
	fprintf(stderr,"Can't read the whole register\n");
	exit(1);
  }else if (bytes < 0) {
	fprintf(stderr,"Error when reading from the FIFO\n");
	exit(1);
  }
	
   close(fd_fifo);
}

static void uso (int status)
{
  if (status != EXIT_SUCCESS)
    warnx("Pruebe `%s -h' para obtener mas informacion.\n", nombre_programa);
  else
    {
      printf ("Uso: %s [OPCIONES]\n", nombre_programa);
fputs ("\
  -c,  --create: crea un nuevo fifo, o permite seleccionar pasado como argumento\n\
  -d,  --delete: borra un fifo pasado como argumento \n\
  -s,  --send: abre un fifo en modo escritura, sin la opcion -c abre default\n\
  -r,  --receive: abre un fifo en modo lectura, sin la opcion -c abre default\n\
", stdout);
      fputs ("\
  -h,	Muestra este breve recordatorio de uso\n\
", stdout);
    }
    exit (status);
}

int
main (int argc, char **argv)
{
  int optc;
  char* name=NULL;
  char path_fifo[50];
  int receive=-1;
  int fd=-1;
  nombre_programa = argv[0];

  while ((optc = getopt (argc, argv, "c:d:rsh")) != -1){
        switch (optc){

          	case 'h':
          	  uso(EXIT_SUCCESS);
          	  break;
          	
          	case 'r':
          	  receive=0;
          	  break;

          	case 's':
          	  receive=1;
          	  break;	

            case 'c':
              name=optarg;
              fifo_modify(name, 0);
              break;

            case 'd':
              name=optarg;
              fifo_modify(name, 1);
              break;	

          	default:
          	  uso (EXIT_FAILURE);
  	}
    }

  if(name != NULL){
      strcpy(path_fifo, path);
      strcat(path_fifo, name);
  }else{
      strcpy(path_fifo, path);
      strcat(path_fifo, "default");
  }

  if (receive == 0)
     fifo_receive(path_fifo);
  else if (receive == 1)
	   fifo_send(path_fifo);

  exit (EXIT_SUCCESS);
}