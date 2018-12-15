#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <linux/string.h>

#define path "/dev/usb/blinkstick0"
#define WALL "0x110000"
#define EMPTY "0x000000"
#define UCOLOR "0x202020"
#define MAX_LEVEL 4

#define INF "informatica"
#define ARQ "arquitectura"
#define COM "complutense"
#define LIN "linux"

static char *uncompleted_words[]={
		"_nf_r_a_ica",
		"_rqu_t_ct__a",
		"co_p_u_en_e",
		"_i__x"
	};

static char **words;
static char **state;
static unsigned int try_cnt=5;


int send_message(char**buff, int fd, unsigned int len)
{
	int retval=0;
	unsigned int i;
	char *message = malloc(sizeof(char)*90);

	sprintf(message,"%s", buff[0]);
	for(i=1;i<len;i++){
		strcat(message,",");
		strcat(message, buff[i]);
	}

    retval = write(fd, message, 90);

    /*if (retval < 0)
        fprintf(stderr, "an error occured: %d\n", retval);
        */

	free(message);
	return retval;

}

int check_word(char *user_word, unsigned int *level){
	if(strcmp(words[(*level)], user_word) == 0) {
		(*level) += 1;
		return 1;
	}
	else return 0;
}

void move(int pos, char **state){
	char *word = malloc(sizeof(char)*10);

	free(state[0]);
	sprintf(word, "%i:%s",pos+2, UCOLOR);
	state[0] = strdup(word);

	free(word);
}


static void win(int fd){
	char *win[] ={"0:0x001100",
		"1:0x001100",
		"2:0x001100",
		"3:0x001100",
		"4:0x001100",
		"5:0x001100",
		"6:0x001100",
		"7:0x001100"};
	printf("Felicidades, has ganado !\n");
	send_message(win, fd,8);
}

static void lose(int fd){
	char *lose[] ={"0:0x110000",
		"1:0x110000",
		"2:0x110000",
		"3:0x110000",
		"4:0x110000",
		"5:0x110000",
		"6:0x110000",
		"7:0x110000"};
	printf("Lo siento, has agotado todos los intentos, has perdido!\n");
	send_message(lose, fd,8);
}


void init_t();
void exit_t();

int main(int argc, char *argv[]){

	char *line=NULL;
	size_t line_size;
	char u_option;
	char *word=NULL;
	int tokens=0;
	int fd;

	ssize_t nread;
	int u_pos=0;
	unsigned int level=0;
	int retval=0;
	
	fd = open(path, O_WRONLY);


	if(fd == -1){
		printf("\n open() failed with error [%s]\n",strerror(errno));
        return 0;
	}

	init_t();
	send_message(state,fd,5);


	if (argc < 2)
        printf("\nusage: [-ews]\n\n  -w      enter a word\n  -s      stop\n");

	printf("\nPalabra: %s\n\nNivel actual: %i \nIntentos: %i \n", uncompleted_words[level], level, try_cnt);
    
    line = malloc(sizeof(char)*20);
    line_size = 20;
    word = malloc(sizeof(char)*(line_size - 2));

    
    if(line == NULL){
	    	printf("\n Line failed with error [%s]\n",strerror(errno));
	    	goto out_exit;
    }

    while((nread = getline(&line, &line_size, stdin)) != -1){
    	
    	tokens = scanf("-%s %s", &u_option, word);

    	if(tokens == 1 && u_option == 's')
    		goto out_exit;

    	if(tokens == 2 && u_option == 'w'){
    		
			if(check_word(word,&level)){
	        	if(level == MAX_LEVEL){
	        		win(fd);
	        		goto out_exit;
	        	}
	        	else{
    				move(u_pos,state);
					send_message(state, fd, 5);
					try_cnt = try_cnt+2;;
        			u_pos += 2;
        			printf("\n ¡Correcto!\n");
    			}
	        }else{
				try_cnt--;
				if(try_cnt == 0){
					lose(fd);
					goto out_exit;
				}
	        }
	    }
	    printf("\nPalabra: %s\n\nNivel actual: %i \nIntentos: %i \n", uncompleted_words[level], level, try_cnt);
    }

    out_exit: 
    	close(fd);
    	exit_t();
    	free(word);
    	free(line);
	return retval;	
}

void init_t(){
	unsigned int i;
	unsigned int j=1;
	char *word = malloc(sizeof(char)*10);

	words = malloc(sizeof(char*)*4);
	/*for(i=0;i<4;i++)
		words[i] = malloc(sizeof(char)*20);*/

	words[0] = strdup(INF);
	words[1] = strdup(ARQ);
	words[2] = strdup(COM);
	words[3] = strdup(LIN);

	state = malloc(sizeof(char*)*5);

	sprintf(word, "%i:%s", 0, UCOLOR);
	state[0]=strdup(word);

	for(i=1;i<8;i+=2){
		sprintf(word, "%i:%s", i, WALL);
		state[j] = strdup(word);
		j++;
	}

	free(word);
}

void exit_t(){
	unsigned int i;
	for(i=0;i<4;i++)
		free(words[i]);

	free(words);

	for(i=0;i<5;i++)
		free(state[i]);

	free(state);
}