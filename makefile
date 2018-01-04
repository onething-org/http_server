all:
	make -C ./server/src

clean:
	make -C ./server/src clean

live: clean all
