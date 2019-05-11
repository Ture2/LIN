El fichero fifo_program.c contiene el programa para probar algunas funcionalidades 
del modulo del kernel.

Hay que compilarlo con gcc puesto que el Makefile es para generar el .ko para android.

Las funciones vienen explicadas al poner ./fifo_program -h
Son las siguientes:
-c : --create, crea un fifo o selecciona uno existente
-d : --delete, borra un fifo 
Las dos funciones anteriores van seguidas de un parametro que es el nombre del fifo.
-s : --send, abre un fifo de escritura, igual que en fifotest
-r : --recieve, abre un fifo de lectura, igual que en fifotest

En caso de poner -s y -r a secas se usara el fifo default, siempre y cuando no se haya 
borrado que saldra un error.
