include ../include_tfc.mk

TARGET = http_mcd_run.so

CC = gcc
CXX = g++
CFLAGS = -Wall -Wno-invalid-offsetof -DSNACC_DEEP_COPY -DHAVE_VARIABLE_SIZED_AUTOMATIC_ARRAYS -Wno-deprecated -pipe -fPIC -m64
CFLAGS += -g3 -O3

SNACC = /usr/local/monitor_dev/public_64/tools/snacc-1.3/
STORAGE = /usr/local/monitor_dev/
HTTP_SERVER = $(STORAGE)http_server/
HTTP_SERVER_INC = $(HTTP_SERVER)server/include/
ASN = $(STORAGE)ajs/asn/
MTNC_LIB = /usr/local/monitor_dev/public_lib/agent_mtnc/mtnc_lib/

INC += -I$(ASN)src/ -I$(SNACC) -I$(SNACC)c++-lib/inc/ -I$(HTTP_SERVER_INC) -I$(MTNC_LIB)include/ -I/usr/local/include/mysql -I/usr/local/include/
INC += -I/usr/include/mysql -I/usr/local/mysqlpp/include/ -I/usr/include/jsoncpp -I/usr/local/curl64/include

LIB = -ldl -lz -lrt /usr/local/curl/lib/libcurl.a $(ASN)lib64/libajs_asn.a $(SNACC)c++-lib/lib64/libasn1c++.a -lpthread $(MTNC_LIB)lib64/libmtnc_lib.a -ljson_linux-gcc-4.4.7_libmt
LIB += -L/usr/local/mysqlpp/lib/ -L/usr/lib64/mysql/ -lmysqlpp -lmysqlclient

CPP_SOURCES = $(wildcard *.cpp)
C_SOURCES = $(wildcard *.c)

CPP_OBJS = $(patsubst %.cpp, ../obj/%.o, $(CPP_SOURCES))
C_OBJS = $(patsubst %.c, ../obj/%.o, $(C_SOURCES))

all: $(TARGET) 

$(TARGET):$(OBJ) $(CPP_OBJS) $(C_OBJS)
	$(CXX) $(CFLAGS) -shared -o $@ $^ $(LIB)
	cp $(TARGET) ../dist/http_server/bin/
	
../obj/%.o: %.cpp
	$(CXX) $(CFLAGS) $(INC) -fpic -c -o $@ $< 

../obj/%.o: %.c
	$(CC) $(CFLAGS) $(INC) -fpic -c -o $@ $<

clean:
	rm -frv $(CPP_OBJS) $(C_OBJS) $(TARGET)

#install:
#	cp $(CCD)http_complete.so ../dist/http_server/bin/ 
#	cp $(CCD)asn_complete.so ../dist/http_server/bin/ 
#	cp $(CCD)ccd ../dist/http_server/bin/http_server_ccd
#	cp $(MCD)mcd ../dist/http_server/bin/http_server_mcd
#	cp $(DCC)dcc ../dist/http_server/bin/http_server_dcc 
#	cp $(WATCHDOG)watchdog ../dist/http_server/bin/http_server_watchdog 

live: clean all
