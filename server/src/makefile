include ../include_tfc.mk

TARGET = ../bin/access_mcd_run.so

CC = gcc
CXX = g++
CFLAGS = -Wall -Wno-invalid-offsetof -DSNACC_DEEP_COPY -DHAVE_VARIABLE_SIZED_AUTOMATIC_ARRAYS -Wno-deprecated -pipe
CFLAGS += -g3 -O3 -static  -m32

SNACC = /data1/src/public/tools/snacc-1.3/
ROOT = /data1/var/http-dav/polopei/ajs/5.0/
ROOT_INC = $(ROOT)include/
ACCESS = $(ROOT)access/
ASN = $(ROOT)asn/
ACCESS_INC = $(ACCESS)include/
MTNC_LIB = /data1/var/http-dav/polopei/mtnc_lib/4.0/

INC += -I$(ASN)src/ -I$(SNACC) -I$(SNACC)c++-lib/inc/ -I$(ACCESS_INC) -I$(MTNC_LIB)include/ -I$(ROOT_INC) -I/usr/local/include/mysql -I/usr/local/include/
LIB = -ldl -lz -lrt /usr/local/lib/libcurl.a $(ASN)lib/libajs_asn.a $(SNACC)c++-lib/lib/libasn1c++.a -lpthread $(MTNC_LIB)lib/libmtnc_lib.a /usr/local/lib/libmysqlpp.a /usr/local/mysql/lib/libmysqlclient.a /usr/local/lib/libjson_linux-gcc-4.1.2_libmt.a

CPP_SOURCES = $(wildcard *.cpp)
C_SOURCES = $(wildcard *.c)

CPP_OBJS = $(patsubst %.cpp, ../obj/%.o, $(CPP_SOURCES))
C_OBJS = $(patsubst %.c, ../obj/%.o, $(C_SOURCES))

all: $(TARGET) 

$(TARGET):$(OBJ) $(CPP_OBJS) $(C_OBJS)
	$(CXX) $(CFLAGS) -shared -o $@ $^ $(LIB)
	cp $(TARGET) ../dist/access_server/bin/
	
../obj/%.o: %.cpp
	$(CXX) $(CFLAGS) $(INC) -fpic -c -o $@ $< 

../obj/%.o: %.c
	$(CC) $(CFLAGS) $(INC) -fpic -c -o $@ $<

clean:
	rm -frv $(CPP_OBJS) $(C_OBJS) $(TARGET)

install:
	cp $(CCD)http_complete.so ../dist/access_server/bin/ 
	cp $(CCD)asn_complete.so ../dist/access_server/bin/ 
	cp $(CCD)ccd ../dist/access_server/bin/access_server_ccd
	cp $(MCD)mcd ../dist/access_server/bin/access_server_mcd
	cp $(DCC)dcc ../dist/access_server/bin/access_server_dcc 
	cp $(WATCHDOG)watchdog ../dist/access_server/bin/access_server_watchdog 

live: clean all
