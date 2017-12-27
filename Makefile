CFLAGS=-Wall -lm -pthread -g

.c.o:
	gcc -g -c $?
all: proxy
proxy: proxy.o 
	gcc $(CFLAGS) -o proxy proxy.o  
clean:
	rm -f *.o proxy
clean_all: clean
	rm *.css
	
